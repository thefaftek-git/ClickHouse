

# ClickHouse Documentation

Welcome to the ClickHouse documentation. This documentation provides comprehensive guides, reference materials, and best practices for using ClickHouse.

## Getting Started

- [Introduction to ClickHouse](/operations/introduction)
- [Installation Guide](/operations/installation)

## Operations & Administration

- [Configuration Files](/operations/configuration-files)
- [Server Configuration Parameters](/operations/server-configuration-parameters)
- [Storage Configuration](/operations/storing-data)
- [Configuring ClickHouse with Azure Blob Storage](/operations/configuring-clickhouse-with-azure-blob-storage) (NEW!)

## SQL Reference

- [Data Types](/sql-reference/data-types)
- [Functions](/sql-reference/functions)
- [Statements](/sql-reference/statements)

## Table Engines

- [MergeTree Family](/engines/table-engines/mergetree-family)
- [Log Family](/engines/table-engines/log-family)
- [Integrations](/engines/table-engines/integrations)

## Interfaces

- [HTTP Interface](/interfaces/http)
- [SQL Client](/interfaces/cli)
- [JDBC Driver](/interfaces/jdbc)
- [ODBC Driver](/interfaces/odbc)

## Development

- [Build from Source](/development/build)
- [Contribution Guidelines](/development/contrib)

## NEW: Azure Blob Storage Integration

We've added comprehensive documentation for configuring ClickHouse with Azure Blob Storage backend, including multiple authentication methods and local testing options:

1. Legacy `azure_blob_storage` disk type
2. Modern object storage configuration
3. Connection string authentication
4. **Managed Identity** (recommended for Azure VMs)
5. **Azurite** (recommended for local development/testing)

Check out the new guide at:
[Configuring ClickHouse with Azure Blob Storage](/operations/configuring-clickhouse-with-azure-blob-storage)

