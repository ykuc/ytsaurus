#pragma once

#include "common.h"
#include "election_manager.pb.h"

#include "../rpc/service.h"
#include "../rpc/client.h"

namespace NYT {

////////////////////////////////////////////////////////////////////////////////

class TElectionManagerProxy
    : public NRpc::TProxyBase
{
public:
    typedef TIntrusivePtr<TElectionManagerProxy> TPtr;

    DECLARE_ENUM(EState,
        (Stopped)
        (Voting)
        (Leading)
        (Following)
    );

    DECLARE_DERIVED_ENUM(NRpc::EErrorCode, EErrorCode,
        ((InvalidState)(1))
        ((InvalidLeader)(2))
        ((InvalidEpoch)(3))
    );

    static Stroka GetServiceName()
    {
        return "ElectionManager";
    }

    TElectionManagerProxy(NRpc::TChannel::TPtr channel)
        : TProxyBase(channel, GetServiceName())
    { }

    RPC_PROXY_METHOD(NRpcElectionManager, PingFollower)
    RPC_PROXY_METHOD(NRpcElectionManager, GetStatus)

};

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT
