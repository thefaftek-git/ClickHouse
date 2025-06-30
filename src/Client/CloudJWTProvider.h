#pragma once

#if USE_JWT_CPP && USE_SSL

#include <Client/JWTProvider.h>
#include <Poco/Timestamp.h>
#include <string>
#include <iosfwd>

namespace DB
{

/// JWT Provider for the ClickHouse managed service flow, which involves a token swap.
class CloudJWTProvider : public JWTProvider
{
public:
    CloudJWTProvider(
        std::string auth_url,
        std::string client_id,
        std::string host,
        std::ostream & out,
        std::ostream & err);

    std::string getJWT() override;

private:
    bool swapIdPTokenForClickHouseJWT(bool show_messages = true);

    // Configuration
    std::string host_str;

    // Token State
    std::string clickhouse_jwt;
    Poco::Timestamp clickhouse_jwt_expires_at{0};
};

}

#endif
