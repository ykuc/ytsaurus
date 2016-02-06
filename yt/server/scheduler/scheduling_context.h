#pragma once

#include "public.h"

#include <yt/ytlib/object_client/public.h>

#include <yt/ytlib/node_tracker_client/public.h>

namespace NYT {
namespace NScheduler {

////////////////////////////////////////////////////////////////////////////////

struct ISchedulingContext
{
    virtual ~ISchedulingContext() = default;

    virtual const TExecNodeDescriptor& GetNodeDescriptor() const = 0;

    virtual const TJobResources& ResourceLimits() const = 0;
    virtual TJobResources& ResourceUsage() = 0;
    //! Used during preemption to allow second-chance scheduling.
    virtual TJobResources& ResourceUsageDiscount() = 0;

    virtual const std::vector<TJobPtr>& StartedJobs() const = 0;
    virtual const std::vector<TJobPtr>& PreemptedJobs() const = 0;
    virtual const std::vector<TJobPtr>& RunningJobs() const = 0;

    virtual TJobPtr GetStartedJob(const TJobId& jobId) const = 0;

    //! Returns |true| if any more new jobs can be scheduled at this node.
    virtual bool CanStartMoreJobs() const = 0;
    //! Returns |true| if the node can handle jobs demanding a certain #tag.
    virtual bool CanSchedule(const TNullable<Stroka>& tag) const = 0;

    virtual void StartJob(TOperationPtr operation, TJobStartRequestPtr jobStartRequest) = 0;

    virtual void PreemptJob(TJobPtr job) = 0;

    virtual TInstant GetNow() const = 0;

    //! Called by a controller to generate id for new job.
    /*!
     *  \note Thread affinity: any
     */
    virtual TJobId GenerateJobId() = 0;
};

std::unique_ptr<ISchedulingContext> CreateSchedulingContext(
    TSchedulerConfigPtr config,
    TExecNodePtr node,
    const std::vector<TJobPtr>& runningJobs,
    NObjectClient::TCellTag cellTag);

////////////////////////////////////////////////////////////////////////////////

} // namespace NScheduler
} // namespace NYT
