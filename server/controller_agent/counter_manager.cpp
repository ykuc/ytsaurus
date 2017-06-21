#include "counter_manager.h"

#include "private.h"

#include <yt/core/profiling/profile_manager.h>

namespace NYT {
namespace NControllerAgent {

using namespace NScheduler;
using namespace NProfiling;

////////////////////////////////////////////////////////////////////////////////

TControllerAgentCounterManager::TControllerAgentCounterManager()
{
    for (auto type : TEnumTraits<EOperationType>::GetDomainValues()) {
        auto tag = TProfileManager::Get()->RegisterTag("operation_type", FormatEnum(type));
        AssertionsFailed_[type] = TSimpleCounter("/assertions_failed", {tag});
    }
}

TControllerAgentCounterManager* TControllerAgentCounterManager::Get()
{
    return Singleton<TControllerAgentCounterManager>();
}

void TControllerAgentCounterManager::IncrementAssertionsFailed(EOperationType operationType)
{
    ControllerAgentProfiler.Increment(AssertionsFailed_[operationType]);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NControllerAgent
} // namespace NYT
