#pragma once

#include "common.h"
#include "master_state_manager_rpc.h"
#include "decorated_master_state.h"
#include "change_log_cache.h"

#include "../election/election_manager.h"

namespace NYT {

////////////////////////////////////////////////////////////////////////////////

// TODO: split into TLeaderCommitter and TFollowerCommitter
class TChangeCommitter
    : public TRefCountedBase
{
public:
    typedef TIntrusivePtr<TChangeCommitter> TPtr;

    DECLARE_ENUM(EResult,
        (Committed)
        (MaybeCommitted)
        (InvalidVersion)
    );

    typedef TAsyncResult<EResult> TResult;

    struct TConfig
    {
        TConfig()
            : RpcTimeout(TDuration::Seconds(3))
        { }

        TDuration RpcTimeout;
    };

    TChangeCommitter(
        const TConfig& config,
        TCellManager::TPtr cellManager,
        TDecoratedMetaState::TPtr metaState,
        TChangeLogCache::TPtr changeLogCache,
        IInvoker::TPtr serviceInvoker,
        const TEpoch& epoch);

    void Stop();

    TResult::TPtr CommitLeader(
        IAction::TPtr changeAction,
        TSharedRef changeData);

    TResult::TPtr CommitFollower(
        TMetaVersion version,
        TSharedRef changeData);

    void SetOnApplyChange(IAction::TPtr onApplyChange);

private:
    class TSession;
    typedef TMetaStateManagerProxy TProxy;

    TResult::TPtr DoCommitLeader(
        IAction::TPtr changeAction,
        TSharedRef changeData);
    TResult::TPtr DoCommitFollower(
        TMetaVersion version,
        TSharedRef changeData);
    static EResult OnAppend(TVoid);

    void DelayedFinalize(TIntrusivePtr<TSession> session);
    void Finalize(TIntrusivePtr<TSession> session);

    TConfig Config;
    TCellManager::TPtr CellManager;
    TDecoratedMetaState::TPtr MetaState;
    TChangeLogCache::TPtr ChangeLogCache;
    TCancelableInvoker::TPtr CancelableServiceInvoker;
    TEpoch Epoch;
    IAction::TPtr OnApplyChange;

    TIntrusivePtr<TSession> CurrentSession;
    TSpinLock SpinLock; // for work with session
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT
