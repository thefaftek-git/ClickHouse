#include <Processors/QueryPlan/ExpressionStep.h>
#include <Processors/QueryPlan/FilterStep.h>
#include <Processors/QueryPlan/ITransformingStep.h>
#include <Processors/QueryPlan/JoinStep.h>
#include <Processors/QueryPlan/Optimizations/Optimizations.h>
#include <Processors/QueryPlan/Optimizations/actionsDAGUtils.h>
#include <Processors/QueryPlan/Optimizations/Utils.h>
#include <Processors/QueryPlan/ReadFromMergeTree.h>
#include <Processors/QueryPlan/SortingStep.h>
#include <Storages/StorageMemory.h>
#include <Processors/QueryPlan/ReadFromMemoryStorageStep.h>
#include <Processors/QueryPlan/LimitStep.h>
#include <Processors/QueryPlan/ReadFromSystemNumbersStep.h>
#include <Core/Settings.h>
#include <Interpreters/IJoin.h>
#include <Interpreters/HashJoin/HashJoin.h>

#include <Processors/QueryPlan/JoinStepLogical.h>
#include <Processors/QueryPlan/ReadFromPreparedSource.h>
#include <Interpreters/FullSortingMergeJoin.h>

#include <Interpreters/TableJoin.h>
#include <Processors/QueryPlan/CreateSetAndFilterOnTheFlyStep.h>

#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <Core/Joins.h>
#include <Interpreters/HashTablesStatistics.h>
#include <Common/logger_useful.h>
#include <Interpreters/ActionsDAG.h>
#include <Interpreters/JoinExpressionActions.h>
#include <Processors/QueryPlan/Optimizations/joinCost.h>
#include <Processors/QueryPlan/Optimizations/joinOrder.h>
#include <Processors/QueryPlan/QueryPlan.h>


namespace DB
{

namespace ErrorCodes
{
    extern const int LOGICAL_ERROR;
}

namespace Setting
{
    extern const SettingsMaxThreads max_threads;
    extern const SettingsNonZeroUInt64 max_block_size;
    extern const SettingsUInt64 min_joined_block_size_bytes;
}

RelationStats getDummyStats(ContextPtr context, const String & table_name);

namespace QueryPlanOptimizations
{

NameSet backTrackColumnsInDag(const String & input_name, const ActionsDAG & actions)
{
    NameSet output_names;

    std::unordered_set<const ActionsDAG::Node *> input_nodes;
    for (const auto * node : actions.getInputs())
    {
        if (input_name == node->result_name)
            input_nodes.insert(node);
    }

    std::unordered_set<const ActionsDAG::Node *> visited_nodes;
    for (const auto * out_node : actions.getOutputs())
    {
        const auto * node = out_node;
        while (true)
        {
            auto [_, inserted] = visited_nodes.insert(node);
            if (!inserted)
                break;

            if (input_nodes.contains(node))
            {
                output_names.insert(out_node->result_name);
                break;
            }

            if (node->type == ActionsDAG::ActionType::ALIAS && node->children.size() == 1)
                node = node->children[0];
        }
    }
    return output_names;
}

template <typename T>
std::unordered_map<String, T> remapColumnStats(const std::unordered_map<String, T> & original, const ActionsDAG & actions)
{
    std::unordered_map<String, T> mapped;
    for (const auto & [name, value] : original)
    {
        for (const auto & remapped : backTrackColumnsInDag(name, actions))
            mapped[remapped] = value;
    }
    return mapped;
}

struct StatisticsContext
{
    std::unordered_map<const QueryPlan::Node *, UInt64> cache_keys;
    StatsCollectingParams params;

    StatisticsContext(const QueryPlanOptimizationSettings & optimization_settings, const QueryPlan::Node & root_node)
        : params{
            /*key_=*/0,
            /*enable=*/ optimization_settings.collect_hash_table_stats_during_joins,
            optimization_settings.max_entries_for_hash_table_stats,
            optimization_settings.max_size_to_preallocate_for_joins}
    {
        if (optimization_settings.collect_hash_table_stats_during_joins)
        {
            cache_keys = calculateHashTableCacheKeys(root_node);
        }
    }

    size_t getCachedHint(const QueryPlan::Node * node)
    {
        if (auto it = cache_keys.find(node); it != cache_keys.end())
        {
            if (auto hint = getHashTablesStatistics<HashJoinEntry>().getSizeHint(params.setKey(it->second)))
                return hint->source_rows;
        }
        return std::numeric_limits<size_t>::max();
    }
};

static RelationStats estimateReadRowsCount(QueryPlan::Node & node, bool has_filter = false)
{
    IQueryPlanStep * step = node.step.get();
    if (const auto * reading = typeid_cast<const ReadFromMergeTree *>(step))
    {
        String table_diplay_name = reading->getStorageID().getTableName();

        if (auto dummy_stats = getDummyStats(reading->getContext(), table_diplay_name); !dummy_stats.table_name.empty())
            return dummy_stats;

        ReadFromMergeTree::AnalysisResultPtr analyzed_result = nullptr;
        analyzed_result = analyzed_result ? analyzed_result : reading->getAnalyzedResult();
        analyzed_result = analyzed_result ? analyzed_result : reading->selectRangesToRead();
        if (!analyzed_result)
            return RelationStats{.estimated_rows = 0, .table_name = table_diplay_name};

        bool is_filtered_by_index = false;
        UInt64 total_parts = 0;
        UInt64 total_granules = 0;
        for (const auto & idx_stat : analyzed_result->index_stats)
        {
            /// We expect the first element to be an index with None type, which is used to estimate the total amount of data in the table.
            /// Further index_stats are used to estimate amount of filtered data after applying the index.
            if (ReadFromMergeTree::IndexType::None == idx_stat.type)
            {
                total_parts = idx_stat.num_parts_after;
                total_granules = idx_stat.num_granules_after;
                continue;
            }

            is_filtered_by_index = is_filtered_by_index
                || (total_parts && idx_stat.num_parts_after < total_parts)
                || (total_granules && idx_stat.num_granules_after < total_granules);

            if (is_filtered_by_index)
                break;
        }
        has_filter = has_filter || reading->getPrewhereInfo();

        /// If any conditions are pushed down to storage but not used in the index,
        /// we cannot precisely estimate the row count
        if (has_filter && !is_filtered_by_index)
            return RelationStats{.estimated_rows = 0, .table_name = table_diplay_name};

        return RelationStats{.estimated_rows = analyzed_result->selected_rows, .table_name = table_diplay_name};
    }

    if (const auto * reading = typeid_cast<const ReadFromMemoryStorageStep *>(step))
    {
        UInt64 estimated_rows = reading->getStorage()->totalRows({}).value_or(0);
        String table_diplay_name = reading->getStorage()->getName();
        return RelationStats{.estimated_rows = estimated_rows, .table_name = table_diplay_name};
    }

    if (const auto * reading = typeid_cast<const ReadFromSystemNumbersStep *>(step))
    {
        RelationStats relation_stats;
        relation_stats.estimated_rows = reading->getNumberOfRows();

        auto column_name = reading->getColumnName();
        relation_stats.column_stats[column_name].num_distinct_values = relation_stats.estimated_rows;
        relation_stats.table_name = reading->getStorageID().getTableName();

        return relation_stats;
    }

    if (node.children.size() != 1)
        return {};

    if (const auto * limit_step = typeid_cast<const LimitStep *>(step))
    {
        auto estimated = estimateReadRowsCount(*node.children.front(), has_filter);
        auto limit = limit_step->getLimit();
        if (estimated.estimated_rows == 0 || estimated.estimated_rows > limit)
            estimated.estimated_rows = limit;
        return estimated;
    }

    if (const auto * expression_step = typeid_cast<const ExpressionStep *>(step))
    {
        auto stats = estimateReadRowsCount(*node.children.front(), has_filter);
        stats.column_stats = remapColumnStats(stats.column_stats, expression_step->getExpression());
        return stats;
    }

    if (const auto * expression_step = typeid_cast<const FilterStep *>(step))
    {
        auto stats = estimateReadRowsCount(*node.children.front(), true);
        stats.column_stats = remapColumnStats(stats.column_stats, expression_step->getExpression());
        return stats;
    }

    return {};
}


bool optimizeJoinLegacy(QueryPlan::Node & node, QueryPlan::Nodes &, const QueryPlanOptimizationSettings &)
{
    auto * join_step = typeid_cast<JoinStep *>(node.step.get());
    if (!join_step || node.children.size() != 2)
        return false;

    const auto & join = join_step->getJoin();
    if (join->pipelineType() != JoinPipelineType::FillRightFirst || !join->isCloneSupported())
        return true;

    const auto & table_join = join->getTableJoin();

    /// Algorithms other than HashJoin may not support all JOIN kinds, so changing from LEFT to RIGHT is not always possible
    bool allow_outer_join = typeid_cast<const HashJoin *>(join.get());
    if (table_join.kind() != JoinKind::Inner && !allow_outer_join)
        return true;

    /// fixme: USING clause handled specially in join algorithm, so swap breaks it
    /// fixme: Swapping for SEMI and ANTI joins should be alright, need to try to enable it and test
    if (table_join.hasUsing() || table_join.strictness() != JoinStrictness::All)
        return true;

    bool need_swap = false;
    if (!join_step->swap_join_tables.has_value())
    {
        auto lhs_extimation = estimateReadRowsCount(*node.children[0]).estimated_rows;
        auto rhs_extimation = estimateReadRowsCount(*node.children[1]).estimated_rows;
        LOG_TRACE(getLogger("optimizeJoinLegacy"), "Left table estimation: {}, right table estimation: {}", lhs_extimation, rhs_extimation);

        if (lhs_extimation && rhs_extimation && lhs_extimation < rhs_extimation)
            need_swap = true;
    }
    else if (join_step->swap_join_tables.value())
    {
        need_swap = true;
    }

    if (!need_swap)
        return true;

    const auto & headers = join_step->getInputHeaders();
    if (headers.size() != 2)
        return true;

    const auto & left_stream_input_header = headers.front();
    const auto & right_stream_input_header = headers.back();

    auto updated_table_join = std::make_shared<TableJoin>(table_join);
    updated_table_join->swapSides();
    auto updated_join = join->clone(updated_table_join, right_stream_input_header, left_stream_input_header);
    join_step->setJoin(std::move(updated_join), /* swap_streams= */ true);
    return true;
}

void addSortingForMergeJoin(
    const FullSortingMergeJoin * join_ptr,
    QueryPlan::Node *& left_node,
    QueryPlan::Node *& right_node,
    QueryPlan::Nodes & nodes,
    const SortingStep::Settings & sort_settings,
    const JoinSettings & join_settings,
    const JoinOperator & join_operator)
{
    auto join_kind = join_operator.kind;
    auto join_strictness = join_operator.strictness;
    auto add_sorting = [&] (QueryPlan::Node *& node, const Names & key_names, JoinTableSide join_table_side)
    {
        SortDescription sort_description;
        sort_description.reserve(key_names.size());
        for (const auto & key_name : key_names)
            sort_description.emplace_back(key_name);

        auto sorting_step = std::make_unique<SortingStep>(
            node->step->getOutputHeader(), std::move(sort_description), 0 /*limit*/, sort_settings, true /*is_sorting_for_merge_join*/);
        sorting_step->setStepDescription(fmt::format("Sort {} before JOIN", join_table_side));
        node = &nodes.emplace_back(QueryPlan::Node{std::move(sorting_step), {node}});
    };

    auto crosswise_connection = CreateSetAndFilterOnTheFlyStep::createCrossConnection();
    auto add_create_set = [&](QueryPlan::Node *& node, const Names & key_names, JoinTableSide join_table_side)
    {
        auto creating_set_step = std::make_unique<CreateSetAndFilterOnTheFlyStep>(
            node->step->getOutputHeader(), key_names, join_settings.max_rows_in_set_to_optimize_join, crosswise_connection, join_table_side);
        creating_set_step->setStepDescription(fmt::format("Create set and filter {} joined stream", join_table_side));

        auto * step_raw_ptr = creating_set_step.get();
        node = &nodes.emplace_back(QueryPlan::Node{std::move(creating_set_step), {node}});
        return step_raw_ptr;
    };

    const auto & join_clause = join_ptr->getTableJoin().getOnlyClause();

    bool join_type_allows_filtering = (join_strictness == JoinStrictness::All || join_strictness == JoinStrictness::Any)
                                    && (isInner(join_kind) || isLeft(join_kind) || isRight(join_kind));


    auto has_non_const = [](const Block & block, const auto & keys)
    {
        for (const auto & key : keys)
        {
            const auto & column = block.getByName(key).column;
            if (column && !isColumnConst(*column))
                return true;
        }
        return false;
    };

    /// This optimization relies on the sorting that should buffer data from both streams before emitting any rows.
    /// Sorting on a stream with const keys can start returning rows immediately and pipeline may stuck.
    /// Note: it's also doesn't work with the read-in-order optimization.
    /// No checks here because read in order is not applied if we have `CreateSetAndFilterOnTheFlyStep` in the pipeline between the reading and sorting steps.
    bool has_non_const_keys = has_non_const(left_node->step->getOutputHeader(), join_clause.key_names_left)
        && has_non_const(right_node->step->getOutputHeader() , join_clause.key_names_right);

    if (join_settings.max_rows_in_set_to_optimize_join > 0 && join_type_allows_filtering && has_non_const_keys)
    {
        auto * left_set = add_create_set(left_node, join_clause.key_names_left, JoinTableSide::Left);
        auto * right_set = add_create_set(right_node, join_clause.key_names_right, JoinTableSide::Right);

        if (isInnerOrLeft(join_kind))
            right_set->setFiltering(left_set->getSet());

        if (isInnerOrRight(join_kind))
            left_set->setFiltering(right_set->getSet());
    }

    add_sorting(left_node, join_clause.key_names_left, JoinTableSide::Left);
    add_sorting(right_node, join_clause.key_names_right, JoinTableSide::Right);
}

bool convertLogicalJoinToPhysical(
    QueryPlan::Node & node,
    QueryPlan::Nodes & nodes,
    const QueryPlanOptimizationSettings & optimization_settings,
    std::optional<UInt64> )
{
    bool keep_logical = optimization_settings.keep_logical_steps;
    UNUSED(keep_logical);
    if (!typeid_cast<JoinStepLogical *>(node.step.get()))
        return false;
    if (node.children.size() != 2)
        throw Exception(ErrorCodes::LOGICAL_ERROR, "JoinStepLogical should have exactly 2 children, but has {}", node.children.size());

    JoinStepLogical::buildPhysicalJoin(node, optimization_settings, nodes);

    return true;
}

struct QueryGraphBuilder
{
    JoinExpressionActions expression_actions;

    std::vector<RelationStats> relation_stats;
    std::vector<QueryPlan::Node *> inputs;

    std::vector<JoinActionRef> join_edges;

    std::vector<std::tuple<BitSet, BitSet, JoinKind>> dependencies;
    std::unordered_map<JoinActionRef, BitSet> pinned;

    struct BuilderContext
    {
        const QueryPlanOptimizationSettings & optimization_settings;
        StatisticsContext statistics_context;

        BuilderContext(const QueryPlanOptimizationSettings & optimization_settings_, const QueryPlan::Node & root_node)
            : optimization_settings(optimization_settings_)
            , statistics_context(optimization_settings_, root_node)
        {}
    };
    std::shared_ptr<BuilderContext> context;

    explicit QueryGraphBuilder(std::shared_ptr<BuilderContext> context_)
        : context(context_) {}

    QueryGraphBuilder(const QueryPlanOptimizationSettings & optimization_settings_, const QueryPlan::Node & root_node)
        : context(std::make_shared<BuilderContext>(optimization_settings_, root_node)) {}
};

void uniteGraphs(QueryGraphBuilder & lhs, QueryGraphBuilder rhs)
{
    size_t shift = lhs.relation_stats.size();

    auto rhs_edges_raw = std::ranges::to<std::vector>(rhs.join_edges | std::views::transform([](const auto & e) { return e.getNode(); }));
    auto rhs_pinned_raw = std::ranges::to<std::unordered_map>(rhs.pinned | std::views::transform([](const auto & e) { return std::make_pair(e.first.getNode(), e.second); }));

    auto [rhs_actions_dag, rhs_expression_sources] = rhs.expression_actions.detachActionsDAG();

    lhs.expression_actions.getActionsDAG()->unite(std::move(rhs_actions_dag));
    for (auto & [node, sources] : rhs_expression_sources)
        sources.shift(shift);
    lhs.expression_actions.setNodeSources(rhs_expression_sources);

    lhs.relation_stats.append_range(std::move(rhs.relation_stats));
    lhs.inputs.append_range(std::move(rhs.inputs));

    lhs.join_edges.append_range(rhs_edges_raw | std::views::transform([&](auto p) { return JoinActionRef(p, lhs.expression_actions); }));
    for (auto && dep : rhs.dependencies)
    {
        auto [left_mask, right_mask, join_kind] = dep;
        left_mask.shift(shift);
        right_mask.shift(shift);
        lhs.dependencies.push_back(std::make_tuple(std::move(left_mask), std::move(right_mask), join_kind));
    }

    for (auto & [action, pin] : rhs_pinned_raw)
    {
        pin.shift(shift);
        lhs.pinned[JoinActionRef(action, lhs.expression_actions)] = pin;
    }
}

void buildQueryGraph(QueryGraphBuilder & query_graph, QueryPlan::Node & node, QueryPlan::Nodes & nodes);

size_t addChildQueryGraph(QueryGraphBuilder & graph, QueryPlan::Node * node, QueryPlan::Nodes & nodes)
{
    if (typeid_cast<JoinStepLogical *>(node->step.get()))
    {
        QueryGraphBuilder child_graph(graph.context);
        buildQueryGraph(child_graph, *node, nodes);
        size_t count = child_graph.inputs.size();
        uniteGraphs(graph, std::move(child_graph));
        return count;
    }

    graph.inputs.push_back(node);
    RelationStats stats = estimateReadRowsCount(*node);
    LOG_TRACE(getLogger("optimizeJoin"), "Estimated statistics for {}({}): {} rows", node->step->getName(), stats.table_name, stats.estimated_rows);
    graph.relation_stats.push_back(stats);
    return 1;
}

void buildQueryGraph(QueryGraphBuilder & query_graph, QueryPlan::Node & node, QueryPlan::Nodes & nodes)
{
    auto * join_step = typeid_cast<JoinStepLogical *>(node.step.get());
    if (!join_step)
        throw Exception(ErrorCodes::LOGICAL_ERROR, "JoinStepLogical expected");
    if (node.children.size() != 2)
        throw Exception(ErrorCodes::LOGICAL_ERROR, "JoinStepLogical should have exactly 2 children, but has {}", node.children.size());

    QueryPlan::Node * lhs_plan = node.children[0];
    QueryPlan::Node * rhs_plan = node.children[1];

    size_t lhs_count = addChildQueryGraph(query_graph, lhs_plan, nodes);
    size_t rhs_count = addChildQueryGraph(query_graph, rhs_plan, nodes);
    size_t total_inputs = query_graph.inputs.size();

    chassert(lhs_count && rhs_count && lhs_count + rhs_count == total_inputs);

    auto [expression_actions, join_operator] = join_step->detachExpressions();

    auto get_raw_nodes = std::views::transform([](const auto & ref) { return ref.getNode(); });
    auto join_expression = std::ranges::to<std::vector>(join_operator.expression | get_raw_nodes);
    auto residual_filter = std::ranges::to<std::vector>(join_operator.residual_filter | get_raw_nodes);

    auto [expression_actions_dag, expression_actions_sources] = expression_actions.detachActionsDAG();

    ActionsDAG::NodeMapping node_mapping;
    query_graph.expression_actions.getActionsDAG()->mergeInplace(std::move(expression_actions_dag), node_mapping);
    JoinExpressionActions::NodeToSourceMapping new_sources;
    for (const auto & [old_node, sources] : expression_actions_sources)
    {
        const auto & new_node_entry = node_mapping.try_emplace(old_node, old_node);
        const auto * new_node = new_node_entry.first->second;
        if (BitSet(sources).set(0, false).set(1, false))
            throw Exception(ErrorCodes::LOGICAL_ERROR, "Expression node {} is not a binary join: {}", old_node->result_name, toString(sources));
        if (lhs_count == 1 && sources.count() == 1 && sources.test(0))
            new_sources[new_node] = BitSet().set(0);
        if (rhs_count == 1 && sources.count() == 1 && sources.test(1))
            new_sources[new_node] = BitSet().set(total_inputs - 1);
    }
    query_graph.expression_actions.setNodeSources(new_sources);

    BitSet left_mask = BitSet::allSet(lhs_count);
    BitSet right_mask = BitSet::allSet(rhs_count);
    right_mask.shift(lhs_count);

    /// Non-reorderable joins
    if (isLeftOrFull(join_operator.kind))
    {
        query_graph.dependencies.emplace_back(right_mask, left_mask, join_operator.kind);
    }
    if (isRightOrFull(join_operator.kind))
    {
        query_graph.dependencies.emplace_back(left_mask, right_mask, reverseJoinKind(join_operator.kind));
    }

    for (const auto * old_node : join_expression)
    {
        const auto & new_node_entry = node_mapping.try_emplace(old_node, old_node);
        const auto * new_node = new_node_entry.first->second;
        auto & edge = query_graph.join_edges.emplace_back(new_node, query_graph.expression_actions);
        auto sources = edge.getSourceRelations();
        for (auto & [null_side, non_null_side, join_kind] : query_graph.dependencies)
        {
            if (sources & null_side)
            {
                auto & pinned = query_graph.pinned[edge];
                pinned = pinned | null_side;
            }
        }
    }
    UNUSED(residual_filter);
}

QueryPlan::Node chooseJoinOrder(QueryGraphBuilder query_graph_builder, QueryPlan::Nodes & nodes)
{
    QueryGraph query_graph;
    query_graph.relation_stats = std::move(query_graph_builder.relation_stats);
    query_graph.edges = std::move(query_graph_builder.join_edges);
    query_graph.dependencies = std::move(query_graph_builder.dependencies);
    query_graph.pinned = std::move(query_graph_builder.pinned);

    LOG_DEBUG(&Poco::Logger::get("XXXX"), "{}:{}: dag:\n{}", __FILE__, __LINE__, query_graph_builder.expression_actions.getActionsDAG()->dumpDAG());

    size_t i = 0;
    for (auto & e: query_graph.edges)
    {
        i++;
        LOG_DEBUG(&Poco::Logger::get("XXXX"), "{}:{}: {}/{}) {} {}", __FILE__, __LINE__, i, query_graph.edges.size(), e.getColumnName(), toString(e.getSourceRelations()));
        auto [op, lhs, rhs] = e.asBinaryPredicate();
        if (op != JoinConditionOperator::Unknown)
            LOG_DEBUG(&Poco::Logger::get("XXXX"), "{}:{}: >>> {}({}) {} {}({})", __FILE__, __LINE__,
                lhs.getColumnName(), toString(lhs.getSourceRelations()), op, rhs.getColumnName(), toString(rhs.getSourceRelations()));
    }


    auto optimized = optimizeJoinOrder(std::move(query_graph));
    UNUSED(nodes);
    UNUSED(optimized);


    throw Exception(ErrorCodes::LOGICAL_ERROR, "Not implemented");
}

void optimizeJoinLogical(QueryPlan::Node & node, QueryPlan::Nodes & nodes, const QueryPlanOptimizationSettings & optimization_settings)
{
    UNUSED(nodes);

    auto * join_step = typeid_cast<JoinStepLogical *>(node.step.get());
    if (!join_step)
        return;

    if (auto * lookup_step = typeid_cast<JoinStepLogicalLookup *>(node.children.back()->step.get()))
    {
        lookup_step->optimize(optimization_settings);
    }

    if (node.children.size() != 2)
        throw Exception(ErrorCodes::LOGICAL_ERROR, "JoinStepLogical should have exactly 2 children, but has {}", node.children.size());

    QueryGraphBuilder query_graph_builder(optimization_settings, node);
    buildQueryGraph(query_graph_builder, node, nodes);
    node = chooseJoinOrder(std::move(query_graph_builder), nodes);
}

}

}
