#include "two_level_fair_share_thread_pool.h"
#include "private.h"
#include "invoker_queue.h"
#include "profiling_helpers.h"
#include "scheduler_thread.h"
#include "thread_pool_detail.h"

#include <yt/yt/core/actions/invoker_util.h>

#include <yt/yt/core/misc/heap.h>
#include <yt/yt/core/misc/ring_queue.h>
#include <yt/yt/core/misc/weak_ptr.h>

#include <yt/yt/library/profiling/sensor.h>

#include <util/generic/xrange.h>

#include <util/system/yield.h>

namespace NYT::NConcurrency {

using namespace NProfiling;

static const auto& Logger = ConcurrencyLogger;

////////////////////////////////////////////////////////////////////////////////

namespace {

struct THeapItem;
class TTwoLevelFairShareQueue;

DECLARE_REFCOUNTED_STRUCT(TBucket)

struct TBucket
    : public IInvoker
{
    TBucket(size_t poolId, TFairShareThreadPoolTag tag, TWeakPtr<TTwoLevelFairShareQueue> parent)
        : PoolId(poolId)
        , Tag(std::move(tag))
        , Parent(std::move(parent))
    { }

    void RunCallback(const TClosure& callback)
    {
        TCurrentInvokerGuard currentInvokerGuard(this);
        callback.Run();
    }

    virtual void Invoke(TClosure callback) override;

    void Drain()
    {
        Queue.clear();
    }

#ifdef YT_ENABLE_THREAD_AFFINITY_CHECK
    virtual NConcurrency::TThreadId GetThreadId() const
    {
        return InvalidThreadId;
    }

    virtual bool CheckAffinity(const IInvokerPtr& invoker) const
    {
        return invoker.Get() == this;
    }
#endif

    ~TBucket();

    const size_t PoolId;
    const TFairShareThreadPoolTag Tag;
    TWeakPtr<TTwoLevelFairShareQueue> Parent;
    TRingQueue<TEnqueuedAction> Queue;
    THeapItem* HeapIterator = nullptr;
    NProfiling::TCpuDuration WaitTime = 0;

    TCpuDuration ExcessTime = 0;
    int CurrentExecutions = 0;
};

DEFINE_REFCOUNTED_TYPE(TBucket)

struct THeapItem
{
    TBucketPtr Bucket;

    THeapItem(const THeapItem&) = delete;
    THeapItem& operator=(const THeapItem&) = delete;

    explicit THeapItem(TBucketPtr bucket)
        : Bucket(std::move(bucket))
    {
        AdjustBackReference(this);
    }

    THeapItem(THeapItem&& other) noexcept
        : Bucket(std::move(other.Bucket))
    {
        AdjustBackReference(this);
    }

    THeapItem& operator=(THeapItem&& other) noexcept
    {
        Bucket = std::move(other.Bucket);
        AdjustBackReference(this);

        return *this;
    }

    void AdjustBackReference(THeapItem* iterator)
    {
        if (Bucket) {
            Bucket->HeapIterator = iterator;
        }
    }

    ~THeapItem()
    {
        if (Bucket) {
            Bucket->HeapIterator = nullptr;
        }
    }
};

bool operator < (const THeapItem& lhs, const THeapItem& rhs)
{
    return lhs.Bucket->ExcessTime < rhs.Bucket->ExcessTime;
}

////////////////////////////////////////////////////////////////////////////////

static constexpr auto LogDurationThreshold = TDuration::Seconds(1);

DECLARE_REFCOUNTED_TYPE(TTwoLevelFairShareQueue)

class TTwoLevelFairShareQueue
    : public TRefCounted
    , public IShutdownable
{
public:
    TTwoLevelFairShareQueue(
        std::shared_ptr<TEventCount> callbackEventCount,
        const TString& threadNamePrefix)
        : CallbackEventCount_(std::move(callbackEventCount))
        , ThreadNamePrefix_(threadNamePrefix)
    {
        Profiler_ = TProfiler{"/fair_share_queue"}
            .WithHot();
    }

    ~TTwoLevelFairShareQueue()
    {
        Shutdown();
    }

    void Configure(int threadCount)
    {
        ThreadCount_.store(threadCount);
    }

    IInvokerPtr GetInvoker(const TString& poolName, double weight, const TFairShareThreadPoolTag& tag)
    {
        while (true) {
            auto guard = Guard(SpinLock_);

            auto poolIt = NameToPoolId_.find(poolName);
            if (poolIt == NameToPoolId_.end()) {
                auto newPoolId = GetLowestEmptyPoolId();

                auto profiler = Profiler_.WithTags(GetBucketTags(ThreadNamePrefix_, poolName));
                auto newPool = std::make_unique<TExecutionPool>(poolName, profiler);
                if (newPoolId >= IdToPool_.size()) {
                    IdToPool_.emplace_back();
                }
                IdToPool_[newPoolId] = std::move(newPool);
                poolIt = NameToPoolId_.emplace(poolName, newPoolId).first;
            }

            auto poolId = poolIt->second;
            const auto& pool = IdToPool_[poolId];
            pool->Weight = weight;

            TBucketPtr bucket;
            auto bucketIt = pool->TagToBucket.find(tag);
            if (bucketIt == pool->TagToBucket.end()) {
                bucket = New<TBucket>(poolId, tag, MakeWeak(this));
                YT_VERIFY(pool->TagToBucket.emplace(tag, bucket.Get()).second);
                pool->BucketCounter.Update(pool->TagToBucket.size());
            } else {
                bucket = DangerousGetPtr<TBucket>(bucketIt->second);
                if (!bucket) {
                    // Bucket is already being destroyed; backoff and retry.
                    guard.Release();
                    ThreadYield();
                    continue;
                }
            }

            return bucket;
        }
    }

    void Invoke(TClosure callback, TBucket* bucket)
    {
        auto guard = Guard(SpinLock_);
        const auto& pool = IdToPool_[bucket->PoolId];

        pool->SizeCounter.Record(++pool->Size);

        if (!bucket->HeapIterator) {
            // Otherwise ExcessTime will be recalculated in AccountCurrentlyExecutingBuckets.
            if (bucket->CurrentExecutions == 0 && !pool->Heap.empty()) {
                bucket->ExcessTime = pool->Heap.front().Bucket->ExcessTime;
            }

            pool->Heap.emplace_back(bucket);
            AdjustHeapBack(pool->Heap.begin(), pool->Heap.end());
            YT_VERIFY(bucket->HeapIterator);
        }

        YT_ASSERT(callback);

        TEnqueuedAction action;
        action.Finished = false;
        action.EnqueuedAt = GetCpuInstant();
        action.Callback = BIND(&TBucket::RunCallback, MakeStrong(bucket), std::move(callback));
        bucket->Queue.push(std::move(action));

        guard.Release();

        CallbackEventCount_->NotifyOne();
    }

    void RemoveBucket(TBucket* bucket)
    {
        auto guard = Guard(SpinLock_);

        auto& pool = IdToPool_[bucket->PoolId];

        auto it = pool->TagToBucket.find(bucket->Tag);
        YT_VERIFY(it != pool->TagToBucket.end());
        YT_VERIFY(it->second == bucket);
        pool->TagToBucket.erase(it);
        pool->BucketCounter.Update(pool->TagToBucket.size());

        if (pool->TagToBucket.empty()) {
            YT_VERIFY(NameToPoolId_.erase(pool->PoolName) == 1);
            pool.reset();
        }
    }

    virtual void Shutdown() override
    {
        Drain();
    }

    void Drain()
    {
        auto guard = Guard(SpinLock_);

        for (const auto& pool : IdToPool_) {
            if (pool) {
                for (const auto& item : pool->Heap) {
                    item.Bucket->Drain();
                }
            }
        }
    }

    TClosure BeginExecute(TEnqueuedAction* action, int index)
    {
        auto& threadState = ThreadStates_[index];

        YT_ASSERT(!threadState.Bucket);
        YT_ASSERT(action && action->Finished);

        auto tscp = NProfiling::TTscp::Get();

        TBucketPtr bucket;
        {
            auto guard = Guard(SpinLock_);
            bucket = GetStarvingBucket(action);

            if (!bucket) {
                return TClosure();
            }

            ++bucket->CurrentExecutions;

            threadState.Bucket = bucket;
            threadState.AccountedAt = tscp.Instant;

            action->StartedAt = tscp.Instant;
            bucket->WaitTime = action->StartedAt - action->EnqueuedAt;
        }

        YT_ASSERT(action && !action->Finished);

        {
            auto guard = Guard(SpinLock_);
            auto& pool = IdToPool_[bucket->PoolId];

            pool->WaitTimeCounter.Record(CpuDurationToDuration(bucket->WaitTime));
        }

        return std::move(action->Callback);
    }

    void EndExecute(TEnqueuedAction* action, int index)
    {
        auto& threadState = ThreadStates_[index];
        if (!threadState.Bucket) {
            return;
        }

        YT_ASSERT(action);

        if (action->Finished) {
            return;
        }

        auto tscp = NProfiling::TTscp::Get();

        action->FinishedAt = tscp.Instant;

        auto timeFromStart = CpuDurationToDuration(action->FinishedAt - action->StartedAt);
        auto timeFromEnqueue = CpuDurationToDuration(action->FinishedAt - action->EnqueuedAt);

        {
            auto guard = Guard(SpinLock_);
            const auto& pool = IdToPool_[threadState.Bucket->PoolId];
            pool->SizeCounter.Record(--pool->Size);
            pool->ExecTimeCounter.Record(timeFromStart);
            pool->TotalTimeCounter.Record(timeFromEnqueue);
        }

        if (timeFromStart > LogDurationThreshold) {
            YT_LOG_DEBUG("Callback execution took too long (Wait: %v, Execution: %v, Total: %v)",
                CpuDurationToDuration(action->StartedAt - action->EnqueuedAt),
                timeFromStart,
                timeFromEnqueue);
        }

        auto waitTime = CpuDurationToDuration(action->StartedAt - action->EnqueuedAt);

        if (waitTime > LogDurationThreshold) {
            YT_LOG_DEBUG("Callback wait took too long (Wait: %v, Execution: %v, Total: %v)",
                waitTime,
                timeFromStart,
                timeFromEnqueue);
        }

        action->Finished = true;

        // Remove outside lock because of lock inside RemoveBucket.
        TBucketPtr bucket;
        {
            auto guard = Guard(SpinLock_);
            bucket = std::move(threadState.Bucket);

            UpdateExcessTime(bucket.Get(), tscp.Instant - threadState.AccountedAt);
            threadState.AccountedAt = tscp.Instant;

            YT_VERIFY(bucket->CurrentExecutions-- > 0);
        }
    }

private:
    struct TThreadState
    {
        TCpuInstant AccountedAt = 0;
        TBucketPtr Bucket;
    };

    struct TExecutionPool
    {
        TExecutionPool(const TString& poolName, const TProfiler& profiler)
            : PoolName(poolName)
            , BucketCounter(profiler.Gauge("/buckets"))
            , SizeCounter(profiler.Summary("/size"))
            , WaitTimeCounter(profiler.Timer("/time/wait"))
            , ExecTimeCounter(profiler.Timer("/time/exec"))
            , TotalTimeCounter(profiler.Timer("/time/total"))
        { }

        TBucketPtr GetStarvingBucket(TEnqueuedAction* action)
        {
            if (!Heap.empty()) {
                auto bucket = Heap.front().Bucket;
                YT_VERIFY(!bucket->Queue.empty());
                *action = std::move(bucket->Queue.front());
                bucket->Queue.pop();

                if (bucket->Queue.empty()) {
                    ExtractHeap(Heap.begin(), Heap.end());
                    Heap.pop_back();
                }

                return bucket;
            }

            return nullptr;
        }

        const TString PoolName;

        TGauge BucketCounter;
        std::atomic<i64> Size{0};
        NProfiling::TSummary SizeCounter;
        TEventTimer WaitTimeCounter;
        TEventTimer ExecTimeCounter;
        TEventTimer TotalTimeCounter;

        double Weight = 1.0;

        TCpuDuration ExcessTime = 0;
        std::vector<THeapItem> Heap;
        THashMap<TFairShareThreadPoolTag, TBucket*> TagToBucket;
    };

    const std::shared_ptr<TEventCount> CallbackEventCount_;
    const TString ThreadNamePrefix_;

    TProfiler Profiler_;

    YT_DECLARE_SPINLOCK(TAdaptiveLock, SpinLock_);
    std::vector<std::unique_ptr<TExecutionPool>> IdToPool_;
    THashMap<TString, int> NameToPoolId_;

    std::atomic<int> ThreadCount_ = 0;
    std::array<TThreadState, TThreadPoolBase::MaxThreadCount> ThreadStates_;


    size_t GetLowestEmptyPoolId()
    {
        VERIFY_SPINLOCK_AFFINITY(SpinLock_);

        size_t id = 0;
        while (id < IdToPool_.size() && IdToPool_[id]) {
            ++id;
        }
        return id;
    }

    void AccountCurrentlyExecutingBuckets()
    {
        VERIFY_SPINLOCK_AFFINITY(SpinLock_);

        auto currentInstant = GetCpuInstant();
        auto threadCount = ThreadCount_.load();
        for (int index = 0; index < threadCount; ++index) {
            auto& threadState = ThreadStates_[index];
            if (!threadState.Bucket) {
                continue;
            }

            auto duration = currentInstant - threadState.AccountedAt;
            threadState.AccountedAt = currentInstant;

            UpdateExcessTime(threadState.Bucket.Get(), duration);
        }
    }

    void UpdateExcessTime(TBucket* bucket, TCpuDuration duration)
    {
        VERIFY_SPINLOCK_AFFINITY(SpinLock_);

        const auto& pool = IdToPool_[bucket->PoolId];

        pool->ExcessTime += duration / pool->Weight;
        bucket->ExcessTime += duration;

        auto positionInHeap = bucket->HeapIterator;
        if (!positionInHeap) {
            return;
        }

        size_t indexInHeap = positionInHeap - pool->Heap.data();
        YT_VERIFY(indexInHeap < pool->Heap.size());
        SiftDown(pool->Heap.begin(), pool->Heap.end(), pool->Heap.begin() + indexInHeap, std::less<>());
    }

    TBucketPtr GetStarvingBucket(TEnqueuedAction* action)
    {
        VERIFY_SPINLOCK_AFFINITY(SpinLock_);

        // For each currently evaluating buckets recalculate excess time.
        AccountCurrentlyExecutingBuckets();

        // Compute min excess over non-empty queues.
        auto minExcessTime = std::numeric_limits<NProfiling::TCpuDuration>::max();

        int minPoolIndex = -1;
        for (int index = 0; index < static_cast<int>(IdToPool_.size()); ++index) {
            const auto& pool = IdToPool_[index];
            if (pool && !pool->Heap.empty() && pool->ExcessTime < minExcessTime) {
                minExcessTime = pool->ExcessTime;
                minPoolIndex = index;
            }
        }

        YT_LOG_TRACE("Buckets: %v",
            MakeFormattableView(
                xrange(size_t(0), IdToPool_.size()),
                [&] (auto* builder, auto index) {
                    const auto& pool = IdToPool_[index];
                    if (!pool) {
                        builder->AppendString("<null>");
                        return;
                    }
                    builder->AppendFormat("[%v %v ", index, pool->ExcessTime);
                    for (const auto& [tagId, rawBucket] : pool->TagToBucket) {
                        if (auto bucket = DangerousGetPtr<TBucket>(rawBucket)) {
                            auto excess = CpuDurationToDuration(bucket->ExcessTime).MilliSeconds();
                            builder->AppendFormat("(%v %v) ", tagId, excess);
                        } else {
                            builder->AppendFormat("(%v ?) ", tagId);
                        }
                    }
                    builder->AppendFormat("]");
                }));

        if (minPoolIndex >= 0) {
            // Reduce excesses (with truncation).
            auto delta = IdToPool_[minPoolIndex]->ExcessTime;
            for (const auto& pool : IdToPool_) {
                if (pool) {
                    pool->ExcessTime = std::max<NProfiling::TCpuDuration>(pool->ExcessTime - delta, 0);
                }
            }
            return IdToPool_[minPoolIndex]->GetStarvingBucket(action);
        }

        return nullptr;
    }

};

DEFINE_REFCOUNTED_TYPE(TTwoLevelFairShareQueue)

////////////////////////////////////////////////////////////////////////////////

void TBucket::Invoke(TClosure callback)
{
    if (auto parent = Parent.Lock()) {
        parent->Invoke(std::move(callback), this);
    }
}

TBucket::~TBucket()
{
    if (auto parent = Parent.Lock()) {
        parent->RemoveBucket(this);
    }
}

////////////////////////////////////////////////////////////////////////////////

class TFairShareThread
    : public TSchedulerThread
{
public:
    TFairShareThread(
        TTwoLevelFairShareQueuePtr queue,
        std::shared_ptr<TEventCount> callbackEventCount,
        const TString& threadGroupName,
        const TString& threadName,
        int index)
        : TSchedulerThread(
            std::move(callbackEventCount),
            threadGroupName,
            threadName)
        , Queue_(std::move(queue))
        , Index_(index)
    { }

protected:
    const TTwoLevelFairShareQueuePtr Queue_;
    const int Index_;

    TEnqueuedAction CurrentAction;

    virtual TClosure BeginExecute() override
    {
        return Queue_->BeginExecute(&CurrentAction, Index_);
    }

    virtual void EndExecute() override
    {
        Queue_->EndExecute(&CurrentAction, Index_);
    }
};

DEFINE_REFCOUNTED_TYPE(TFairShareThread)

////////////////////////////////////////////////////////////////////////////////

class TTwoLevelFairShareThreadPool
    : public ITwoLevelFairShareThreadPool
    , public TThreadPoolBase
{
public:
    TTwoLevelFairShareThreadPool(
        int threadCount,
        const TString& threadNamePrefix)
        : TThreadPoolBase(threadNamePrefix)
        , Queue_(New<TTwoLevelFairShareQueue>(
            CallbackEventCount_,
            ThreadNamePrefix_))
    {
        Configure(threadCount);
    }

    ~TTwoLevelFairShareThreadPool()
    {
        Shutdown();
    }

    virtual void Configure(int threadCount) override
    {
        TThreadPoolBase::Configure(threadCount);
    }

    virtual IInvokerPtr GetInvoker(
        const TString& poolName,
        double weight,
        const TFairShareThreadPoolTag& tag) override
    {
        EnsureStarted();
        return Queue_->GetInvoker(poolName, weight, tag);
    }

    virtual void Shutdown() override
    {
        TThreadPoolBase::Shutdown();
    }

private:
    const std::shared_ptr<TEventCount> CallbackEventCount_ = std::make_shared<TEventCount>();
    const TTwoLevelFairShareQueuePtr Queue_;


    virtual void DoShutdown() override
    {
        Queue_->Shutdown();
        TThreadPoolBase::DoShutdown();
    }

    virtual TClosure MakeFinalizerCallback() override
    {
        return BIND([queue = Queue_, callback = TThreadPoolBase::MakeFinalizerCallback()] {
            callback();
            queue->Drain();
        });
    }

    virtual void DoConfigure(int threadCount) override
    {
        Queue_->Configure(threadCount);
        TThreadPoolBase::DoConfigure(threadCount);
    }

    virtual TSchedulerThreadPtr SpawnThread(int index) override
    {
        return New<TFairShareThread>(
            Queue_,
            CallbackEventCount_,
            ThreadNamePrefix_,
            MakeThreadName(index),
            index);
    }
};

} // namespace

////////////////////////////////////////////////////////////////////////////////

ITwoLevelFairShareThreadPoolPtr CreateTwoLevelFairShareThreadPool(
    int threadCount,
    const TString& threadNamePrefix)
{
    return New<TTwoLevelFairShareThreadPool>(
        threadCount,
        threadNamePrefix);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NConcurrency
