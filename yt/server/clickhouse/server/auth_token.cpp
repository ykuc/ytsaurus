#include "auth_token.h"

#include <yt/client/api/connection.h>

#include <yt/core/misc/nullable.h>

namespace NYT {
namespace NClickHouse {

using namespace NYT::NApi;

namespace {

////////////////////////////////////////////////////////////////////////////////

TNullable<TString> GetValue(const NInterop::TStringMap& attrs, TStringBuf name)
{
    auto it = attrs.find(name);
    if (it != attrs.end()) {
        return it->second;
    }
    return {};
}

}   // namespace

////////////////////////////////////////////////////////////////////////////////

class TClientAuthToken
    : public NInterop::IAuthorizationToken
    , public TClientOptions
{
public:
    TClientAuthToken() = default;

    TClientAuthToken(const TClientOptions& options)
        : TClientOptions(options)
    {}
};

////////////////////////////////////////////////////////////////////////////////

const TClientOptions& UnwrapAuthToken(const NInterop::IAuthorizationToken& token)
{
    const auto* ptr = dynamic_cast<const TClientOptions*>(&token);
    if (!ptr) {
        THROW_ERROR_EXCEPTION("Invalid authorization token");
    }
    return *ptr;
}

////////////////////////////////////////////////////////////////////////////////

class TAuthTokenService
    : public NInterop::IAuthorizationTokenService
{
    NInterop::IAuthorizationTokenPtr CreateToken(
        const NInterop::TStringMap& attrs) override
    {
        auto user = GetValue(attrs, "user");
        if (!user) {
            THROW_ERROR_EXCEPTION("Invalid client credentials: expected user login");
        }

        TClientOptions options;
        options.PinnedUser = user.Get();
        options.Token = GetValue(attrs, "token");
        options.SessionId = GetValue(attrs, "sessionId");
        options.SslSessionId = GetValue(attrs, "sessionId2");

        return std::make_shared<TClientAuthToken>(options);
    }
};

////////////////////////////////////////////////////////////////////////////////

NInterop::IAuthorizationTokenService* GetAuthTokenService()
{
    static TAuthTokenService instance;
    return &instance;
}

}   // namespace NClickHouse
}   // namespace NYT
