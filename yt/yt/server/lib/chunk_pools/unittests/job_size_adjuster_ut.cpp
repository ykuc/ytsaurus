#include <yt/yt/core/test_framework/framework.h>

#include <yt/yt/server/controller_agent/config.h>
#include <yt/yt/server/lib/chunk_pools/job_size_adjuster.h>
#include <yt/yt/server/lib/chunk_pools/config.h>

namespace NYT::NChunkPools {
namespace {

////////////////////////////////////////////////////////////////////////////////

TEST(TJobSizeAdjusterTest, JobsShorterThanMinJobTime)
{
    i64 dataWeightPerJob = 128LL * 1024 * 1024;
    auto config = New<TJobSizeAdjusterConfig>();
    config->MinJobTime = TDuration::Seconds(20);
    config->ExecToPrepareTimeRatio = 10.0;
    auto jobSizeAdjuster = CreateJobSizeAdjuster(
        dataWeightPerJob,
        config);

    EXPECT_EQ(dataWeightPerJob, jobSizeAdjuster->GetDataWeightPerJob());
    i64 jobDataWeight = 150LL * 1024 * 1024;

    jobSizeAdjuster->UpdateStatistics(jobDataWeight, TDuration::MilliSeconds(20), TDuration::Seconds(19));
    EXPECT_LT(static_cast<i64>(jobDataWeight), jobSizeAdjuster->GetDataWeightPerJob());
    EXPECT_GT(static_cast<i64>(1.1 * jobDataWeight), jobSizeAdjuster->GetDataWeightPerJob());

    jobSizeAdjuster->UpdateStatistics(jobDataWeight, TDuration::MilliSeconds(20), TDuration::Seconds(1));
    EXPECT_EQ(static_cast<i64>(JobSizeBoostFactor * jobDataWeight), jobSizeAdjuster->GetDataWeightPerJob());
}

TEST(TJobSizeAdjusterTest, ShortJobsWithLongPrepare)
{
    i64 dataWeightPerJob = 128LL * 1024 * 1024;
    auto config = New<TJobSizeAdjusterConfig>();
    config->MinJobTime = TDuration::MilliSeconds(1);
    config->ExecToPrepareTimeRatio = 10.0;
    auto jobSizeAdjuster = CreateJobSizeAdjuster(
        dataWeightPerJob,
        config);

    EXPECT_EQ(dataWeightPerJob, jobSizeAdjuster->GetDataWeightPerJob());
    i64 jobDataWeight = 150LL * 1024 * 1024;

    jobSizeAdjuster->UpdateStatistics(jobDataWeight, TDuration::MilliSeconds(333), TDuration::Seconds(3));
    EXPECT_LT(static_cast<i64>(jobDataWeight), jobSizeAdjuster->GetDataWeightPerJob());
    EXPECT_GT(static_cast<i64>(1.2 * jobDataWeight), jobSizeAdjuster->GetDataWeightPerJob());

    auto dataWeightAfterFirstAdjustment = jobSizeAdjuster->GetDataWeightPerJob();

    jobSizeAdjuster->UpdateStatistics(jobDataWeight, TDuration::Seconds(1), TDuration::Seconds(3));
    EXPECT_EQ(static_cast<i64>(JobSizeBoostFactor * dataWeightAfterFirstAdjustment), jobSizeAdjuster->GetDataWeightPerJob());
}

TEST(TJobSizeAdjusterTest, FastJobThenSlowJob)
{
    i64 dataWeightPerJob = 128LL * 1024 * 1024;
    auto config = New<TJobSizeAdjusterConfig>();
    config->MinJobTime = TDuration::MilliSeconds(1);
    config->ExecToPrepareTimeRatio = 10.0;
    auto jobSizeAdjuster = CreateJobSizeAdjuster(
        dataWeightPerJob,
        config);

    EXPECT_EQ(dataWeightPerJob, jobSizeAdjuster->GetDataWeightPerJob());
    i64 jobDataWeight = 150LL * 1024 * 1024;

    jobSizeAdjuster->UpdateStatistics(jobDataWeight, TDuration::Seconds(1), TDuration::Seconds(2));
    EXPECT_EQ(static_cast<i64>(JobSizeBoostFactor * dataWeightPerJob), jobSizeAdjuster->GetDataWeightPerJob());

    auto dataWeightAfterFirstAdjustment = jobSizeAdjuster->GetDataWeightPerJob();

    jobSizeAdjuster->UpdateStatistics(jobDataWeight, TDuration::Seconds(1), TDuration::Seconds(100));
    EXPECT_EQ(static_cast<i64>(dataWeightAfterFirstAdjustment / JobSizeBoostFactor), jobSizeAdjuster->GetDataWeightPerJob());
}

////////////////////////////////////////////////////////////////////////////////

} // namespace
} // namespace NYT::NControllerAgent

