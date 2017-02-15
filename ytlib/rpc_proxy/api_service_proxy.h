#pragma once

#include "public.h"

#include <yt/ytlib/rpc_proxy/api_service.pb.h>

#include <yt/core/rpc/client.h>

namespace NYT {
namespace NRpcProxy {

////////////////////////////////////////////////////////////////////////////////

class TApiServiceProxy
    : public NRpc::TProxyBase
{
public:
    DEFINE_RPC_PROXY(TApiServiceProxy, RPC_PROXY_DESC(ApiService)
        .SetProtocolVersion(1));

    DEFINE_RPC_PROXY_METHOD(NRpcProxy::NProto, GetNode);
    DEFINE_RPC_PROXY_METHOD(NRpcProxy::NProto, LookupRows);
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NRpcProxy
} // namespace NYT
