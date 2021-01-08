#include "resource_tree_element.h"
#include "resource_tree.h"

namespace NYT::NScheduler {

using namespace NConcurrency;

////////////////////////////////////////////////////////////////////////////////

TResourceTreeElement::TResourceTreeElement(
    TResourceTree* resourceTree,
    const TString& id,
    EResourceTreeElementKind elementKind)
    : ResourceTree_(resourceTree)
    , Id_(id)
    , Kind_(elementKind)
{ }

TJobResources TResourceTreeElement::GetResourceUsage()
{
    auto guard = ReaderGuard(ResourceUsageLock_);

    ResourceTree_->IncrementUsageLockReadCount();

    return ResourceUsage_;
}

TJobResources TResourceTreeElement::GetResourceUsageWithPrecommit()
{
    auto guard = ReaderGuard(ResourceUsageLock_);

    ResourceTree_->IncrementUsageLockReadCount();

    return ResourceUsage_ + ResourceUsagePrecommit_;
}

bool TResourceTreeElement::CheckDemand(
    const TJobResources& delta,
    const TJobResources& resourceDemand,
    const TJobResources& resourceDiscount)
{
    auto guard = ReaderGuard(ResourceUsageLock_);

    ResourceTree_->IncrementUsageLockReadCount();

    auto availableDemand = ComputeAvailableResources(
        resourceDemand,
        ResourceUsage_ + ResourceUsagePrecommit_,
        resourceDiscount);

    return Dominates(availableDemand, delta);
}

void TResourceTreeElement::SetResourceLimits(TJobResources resourceLimits)
{
    auto guard = WriterGuard(ResourceUsageLock_);

    ResourceTree_->IncrementUsageLockWriteCount();

    ResourceLimits_ = resourceLimits;
    ResourceLimitsSpecified_ = (resourceLimits != TJobResources::Infinite());
    if (!ResourceLimitsSpecified_ && Kind_ != EResourceTreeElementKind::Operation) {
        ResourceUsagePrecommit_ = TJobResources();
    }
}

bool TResourceTreeElement::IncreaseLocalResourceUsagePrecommit(const TJobResources& delta)
{
    if (Kind_ != EResourceTreeElementKind::Operation && !ResourceLimitsSpecified_) {
        return true;
    }

    auto guard = WriterGuard(ResourceUsageLock_);

    if (!Alive_) {
        return false;
    }

    ResourceTree_->IncrementUsageLockWriteCount();

    ResourceUsagePrecommit_ += delta;

    return true;
}

bool TResourceTreeElement::CommitLocalResourceUsage(
    const TJobResources& resourceUsageDelta,
    const TJobResources& precommittedResources)
{
    auto guard = WriterGuard(ResourceUsageLock_);

    if (!Alive_) {
        return false;
    }

    ResourceTree_->IncrementUsageLockWriteCount();

    ResourceUsage_ += resourceUsageDelta;
    if (Kind_ == EResourceTreeElementKind::Operation || ResourceLimitsSpecified_) {
        // We can try to subtract some excessive resource usage precommit, if precommit was added before resource limits were set.
        ResourceUsagePrecommit_ = Max(TJobResources(), ResourceUsagePrecommit_ - precommittedResources);
    }
    
    YT_VERIFY(Dominates(ResourceUsage_, TJobResources()));

    return true;
}

bool TResourceTreeElement::IncreaseLocalResourceUsage(const TJobResources& delta)
{
    auto guard = WriterGuard(ResourceUsageLock_);

    if (!Alive_) {
        return false;
    }

    ResourceTree_->IncrementUsageLockWriteCount();

    ResourceUsage_ += delta;

    YT_VERIFY(Dominates(ResourceUsage_, TJobResources()));

    return true;
}

void TResourceTreeElement::ReleaseResources(TJobResources* usagePrecommit, TJobResources* usage)
{
    auto guard = WriterGuard(ResourceUsageLock_);

    YT_VERIFY(!Alive_);

    *usagePrecommit = ResourceUsagePrecommit_;
    *usage = ResourceUsage_;

    ResourceUsagePrecommit_ = TJobResources();
    ResourceUsage_ = TJobResources();
}

TJobResources TResourceTreeElement::GetResourceUsagePrecommit()
{
    auto guard = ReaderGuard(ResourceUsageLock_);

    ResourceTree_->IncrementUsageLockReadCount();

    return ResourceUsagePrecommit_;
}

bool TResourceTreeElement::IncreaseLocalResourceUsagePrecommitWithCheck(
    const TJobResources& delta,
    TJobResources* availableResourceLimitsOutput)
{
    if (Kind_ != EResourceTreeElementKind::Operation && !ResourceLimitsSpecified_) {
        *availableResourceLimitsOutput = TJobResources::Infinite();
        return true;
    }

    auto guard = WriterGuard(ResourceUsageLock_);

    if (!Alive_) {
        return false;
    }

    // NB: Actually tree elements has resource usage discounts (used for scheduling with preemption)
    // that should be considered in this check. But concurrent nature of this shared tree makes hard to consider
    // these discounts here. The only consequence of discounts ignorance is possibly redundant jobs that would
    // be aborted just after being scheduled.
    auto availableResourceLimits = ComputeAvailableResources(
        ResourceLimits_,
        ResourceUsage_ + ResourceUsagePrecommit_,
        {});

    if (!Dominates(availableResourceLimits, delta)) {
        return false;
    }

    ResourceUsagePrecommit_ += delta;

    *availableResourceLimitsOutput = availableResourceLimits;

    return true;
}

void TResourceTreeElement::MarkInitialized()
{
    Initialized_ = true;
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NScheduler
