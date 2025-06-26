#include <Client/CloudJwtProvider.h>
#include <Common/Exception.h>
#include <Common/StringUtils.h>
#include <config.h>

#include <Poco/Net/HTTPClientSession.h>
#include <Poco/Net/HTTPRequest.h>
#include <Poco/Net/HTTPResponse.h>
#include <Poco/Net/HTTPSClientSession.h>
#include <Poco/Net/Context.h>
#include <Poco/Net/SSLManager.h>
#include <Poco/StreamCopier.h>
#include <Poco/URI.h>
#include <Poco/JSON/Parser.h>
#include <Poco/JSON/Object.h>
#include <Poco/Dynamic/Var.h>

#include <thread>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <jwt-cpp/jwt.h>

namespace DB
{

namespace ErrorCodes
{
    extern const int BAD_ARGUMENTS;
    extern const int SUPPORT_IS_DISABLED;
    extern const int NETWORK_ERROR;
}

namespace
{

struct AuthEndpoints
{
    std::string auth_url;
    std::string client_id;
    std::string api_host;
};

static const std::map<std::string, AuthEndpoints> managed_service_endpoints = {
    {
        ".clickhouse-dev.com",
        {
            "https://auth.control-plane.clickhouse-dev.com",
            "dKv0XkTAw7rghGiAa5sjPFYGQUVtjzuz",
            "https://control-plane-internal.clickhouse-dev.com"
        }
    },
    {
        ".clickhouse-staging.com",
        {
            "https://ch-staging.us.auth0.com",
            "TODO: CREATE THIS",
            "https://control-plane-internal.clickhouse-staging.com"
        }
    },
    {
        ".clickhouse.cloud",
        {
            "https://ch-production.us.auth0.com",
            "TODO: CREATE THIS",
            "https://control-plane-internal.clickhouse.cloud"
        }
    }
};

inline const AuthEndpoints * getAuthEndpoints(const std::string & host)
{
    for (const auto & [suffix, endpoints] : managed_service_endpoints)
    {
        if (endsWith(host, suffix))
            return &endpoints;
    }
    return nullptr;
}

}

CloudJwtProvider::CloudJwtProvider(
    std::string auth_url, std::string client_id, std::string host,
    std::ostream & out, std::ostream & err)
    : JwtProvider(std::move(auth_url), std::move(client_id), out, err),
      host_str(std::move(host))
{
    if (auth_url_str.empty() || client_id_str.empty())
    {
        if (const auto * endpoints = getAuthEndpoints(host_str))
        {
            if (auth_url_str.empty())
                auth_url_str = endpoints->auth_url;
            if (client_id_str.empty())
                client_id_str = endpoints->client_id;
        }
    }
}

std::string CloudJwtProvider::getJWT()
{
    Poco::Timestamp now;
    Poco::Timestamp expiration_buffer = 30 * Poco::Timespan::SECONDS;

    if (!final_clickhouse_jwt.empty() && now < final_clickhouse_jwt_expires_at - expiration_buffer)
        return final_clickhouse_jwt;

    if (!idp_refresh_token.empty())
    {
        if (refreshIdPAccessToken() && swapIdPTokenForClickHouseJWT())
            return final_clickhouse_jwt;
    }

    if (initialLogin() && swapIdPTokenForClickHouseJWT())
        return final_clickhouse_jwt;

    return "";
}

bool CloudJwtProvider::swapIdPTokenForClickHouseJWT()
{
    const auto * endpoints = getAuthEndpoints(host_str);

    if (!endpoints)
    {
        error_stream << "Error: cannot determine token swap endpoint from hostname " << host_str
                     << ". Please use a managed ClickHouse hostname." << std::endl;
        return false;
    }

    std::string swap_url = endpoints->api_host + "/api/tokenSwap";

    output_stream << "Fetching credentials for " << host_str << "..." << std::endl;
    try
    {
        Poco::URI swap_uri(swap_url);
        auto session = createHTTPSession(swap_uri);
        Poco::Net::HTTPRequest request(Poco::Net::HTTPRequest::HTTP_POST, swap_uri.getPathAndQuery(), Poco::Net::HTTPMessage::HTTP_1_1);
        request.set("Authorization", "Bearer " + idp_access_token);

        Poco::JSON::Object body;
        body.set("hostname", host_str);
        std::stringstream body_stream;
        body.stringify(body_stream);
        std::string request_body = body_stream.str();

        request.setContentType("application/json; charset=utf-8");
        request.setContentLength(request_body.length());
        session->sendRequest(request) << request_body;

        Poco::Net::HTTPResponse response;
        std::istream & rs = session->receiveResponse(response);
        if (response.getStatus() != Poco::Net::HTTPResponse::HTTP_OK)
        {
            std::string error_body;
            Poco::StreamCopier::copyToString(rs, error_body);
            error_stream << "Error swapping token: " << response.getStatus() << " " << response.getReason() << "\nResponse: " << error_body << std::endl;
            return false;
        }

        std::string response_body;
        Poco::StreamCopier::copyToString(rs, response_body);

        Poco::JSON::Object::Ptr object = Poco::JSON::Parser().parse(response_body).extract<Poco::JSON::Object::Ptr>();
        final_clickhouse_jwt = object->getValue<std::string>("token");
        final_clickhouse_jwt_expires_at = jwt::decode(final_clickhouse_jwt).get_payload_claim("exp").as_integer();

        output_stream << "Successfully authenticated with ClickHouse Cloud" << std::endl;
        return true;
    }
    catch(const Poco::Exception & ex)
    {
        error_stream << "Exception during token swap: " << ex.displayText() << std::endl;
        return false;
    }
}

}
