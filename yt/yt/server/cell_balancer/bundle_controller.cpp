#include "bundle_controller.h"
#include "bundle_scheduler.h"

#include "bootstrap.h"
#include "config.h"
#include "cypress_bindings.h"

#include <yt/yt/server/lib/cypress_election/election_manager.h>

#include <yt/yt/client/cypress_client/public.h>

#include <yt/yt/ytlib/api/native/client.h>
#include <yt/yt/ytlib/api/native/transaction.h>

namespace NYT::NCellBalancer {

using namespace NApi;
using namespace NConcurrency;
using namespace NCypressClient;
using namespace NProfiling;
using namespace NTabletClient;
using namespace NTransactionClient;
using namespace NYTree;
using namespace NYson;

////////////////////////////////////////////////////////////////////////////////

static const auto& Logger = BundleController;
static const TYPath TabletCellBundlesPath("//sys/tablet_cell_bundles");
static const TYPath TabletCellBundlesDynamicConfigPath("//sys/tablet_cell_bundles/@config");
static const TYPath TabletNodesPath("//sys/tablet_nodes");
static const TYPath TabletCellsPath("//sys/tablet_cells");

////////////////////////////////////////////////////////////////////////////////

class TBundleController
    : public IBundleController
{
public:
    TBundleController(IBootstrap* bootstrap, TBundleControllerConfigPtr config)
        : Bootstrap_(bootstrap)
        , Config_(std::move(config))
        , Profiler("/bundle_controller")
        , SuccessfulScanBundleCounter_(Profiler.Counter("/successful_scan_bundles_count"))
        , FailedScanBundleCounter_(Profiler.Counter("/failed_scan_bundles_count"))
        , AlarmCounter_(Profiler.Counter("/scan_bundles_alarms_count"))
        , DynamicConfigUpdateCounter_(Profiler.Counter("/dynamic_config_update_counter"))
        , NodeAllocationCounter_(Profiler.Counter("/node_allocation_counter"))
        , NodeDeallocationCounter_(Profiler.Counter("/node_deallocation_counter"))
        , CellCreationCounter_(Profiler.Counter("/cell_creation_counter"))
        , CellRemovalCounter_(Profiler.Counter("/cell_removal_counter"))
        , ChangedNodeUserTagCounter_(Profiler.Counter("/changed_node_user_tag_counter"))
        , ChangedDecommissionedFlagCounter_(Profiler.Counter("/changed_decommissioned_flag_counter"))
        , ChangedChangeNodeAnnotationCounter_(Profiler.Counter("/changed_change_node_annotation_counter"))
        , InflightNodeAllocationCount_(Profiler.Gauge("/inflight_node_allocations_count"))
        , InflightNodeDeallocationCount_(Profiler.Gauge("/inflight_node_deallocations_count"))
        , InflightCellRemovalCount_(Profiler.Gauge("/inflight_cell_removal_count"))
        , NodeAllocationRequestAge_(Profiler.TimeGauge("/node_allocation_request_age"))
        , NodeDeallocationRequestAge_(Profiler.TimeGauge("/node_deallocation_request_age"))
        , RemovingCellsAge_(Profiler.TimeGauge("/removing_cells_age"))
    { }

    void Start() override
    {
        VERIFY_INVOKER_AFFINITY(Bootstrap_->GetControlInvoker());

        StartTime_ = TInstant::Now();

        YT_VERIFY(!PeriodicExecutor_);
        PeriodicExecutor_ = New<TPeriodicExecutor>(
            Bootstrap_->GetControlInvoker(),
            BIND(&TBundleController::ScanBundles, MakeWeak(this)),
            Config_->BundleScanPeriod);
        PeriodicExecutor_->Start();
    }

private:
    IBootstrap* const Bootstrap_;
    const TBundleControllerConfigPtr Config_;

    TCellTrackerImplPtr CellTrackerImpl_;

    TInstant StartTime_;
    TPeriodicExecutorPtr PeriodicExecutor_;

    const TProfiler Profiler;
    TCounter SuccessfulScanBundleCounter_;
    TCounter FailedScanBundleCounter_;
    TCounter AlarmCounter_;

    TCounter DynamicConfigUpdateCounter_;
    TCounter NodeAllocationCounter_;
    TCounter NodeDeallocationCounter_;
    TCounter CellCreationCounter_;
    TCounter CellRemovalCounter_;

    TCounter ChangedNodeUserTagCounter_;
    TCounter ChangedDecommissionedFlagCounter_;
    TCounter ChangedChangeNodeAnnotationCounter_;

    TGauge InflightNodeAllocationCount_;
    TGauge InflightNodeDeallocationCount_;
    TGauge InflightCellRemovalCount_;

    TTimeGauge NodeAllocationRequestAge_;
    TTimeGauge NodeDeallocationRequestAge_;
    TTimeGauge RemovingCellsAge_;

    void ScanBundles() const
    {
        VERIFY_INVOKER_AFFINITY(Bootstrap_->GetControlInvoker());

        if (!IsLeader()) {
            YT_LOG_DEBUG("Bundle Controller is not leading");
            return;
        }

        try {
            YT_PROFILE_TIMING("/bundle_controller/scan_bundles") {
                DoScanBundles();
                SuccessfulScanBundleCounter_.Increment();
            }
        } catch (const TErrorException& ex) {
            YT_LOG_ERROR(ex, "Scanning bundles failed");
            FailedScanBundleCounter_.Increment();
        }
    }

    static std::vector<TString> GetAliveAllocationsId(const TSchedulerInputState& inputState)
    {
        std::vector<TString> result;
        for (const auto& [_, bundleState] : inputState.BundleStates) {
            for (const auto& [allocId, _] : bundleState->Allocations) {
                result.push_back(allocId);
            }
        }

        return result;
    }

    static std::vector<TString> GetAliveDeallocationsId(const TSchedulerInputState& inputState)
    {
        std::vector<TString> result;
        for (const auto& [_, bundleState] : inputState.BundleStates) {
            for (const auto& [deallocId, _] : bundleState->Deallocations) {
                result.push_back(deallocId);
            }
        }

        return result;
    }

    void DoScanBundles() const
    {
        VERIFY_INVOKER_AFFINITY(Bootstrap_->GetControlInvoker());

        YT_LOG_DEBUG("Bundles scan started");

        auto transaction = CreateTransaction();
        auto inputState = GetInputState(transaction);
        TSchedulerMutations mutations;

        ScheduleBundles(inputState, &mutations);
        Mutate(transaction, mutations);

        WaitFor(transaction->Commit())
            .ThrowOnError();
    }

    inline static const TString  NodeAttributeBundleControllerAnnotations = "bundle_controller_annotations";
    inline static const TString  NodeAttributeUserTags = "user_tags";
    inline static const TString  NodeAttributeDecommissioned = "decommissioned";

    void Mutate(const ITransactionPtr& transaction, const TSchedulerMutations& mutations) const
    {
        VERIFY_INVOKER_AFFINITY(Bootstrap_->GetControlInvoker());

        CreateHulkRequests<TAllocationRequest>(transaction, Config_->HulkAllocationsPath, mutations.NewAllocations);
        CreateHulkRequests<TDeallocationRequest>(transaction, Config_->HulkDeallocationsPath, mutations.NewDeallocations);
        CypressSet(transaction, GetBundlesStatePath(), mutations.ChangedStates);

        SetNodeAttributes(transaction, NodeAttributeBundleControllerAnnotations, mutations.ChangeNodeAnnotations);
        SetNodeAttributes(transaction, NodeAttributeUserTags, mutations.ChangedNodeUserTags);
        SetNodeAttributes(transaction, NodeAttributeDecommissioned, mutations.ChangedDecommissionedFlag);

        CreateTabletCells(transaction, mutations.CellsToCreate);
        RemoveTabletCells(transaction, mutations.CellsToRemove);

        if (mutations.DynamicConfig) {
            DynamicConfigUpdateCounter_.Increment();
            SetBundlesDynamicConfig(transaction, *mutations.DynamicConfig);
        }

        // TODO(capone212): Fire alarms.

        AlarmCounter_.Increment(mutations.AlertsToFire.size());
        NodeAllocationCounter_.Increment(mutations.NewAllocations.size());
        NodeDeallocationCounter_.Increment(mutations.NewDeallocations.size());
        CellCreationCounter_.Increment(mutations.CellsToCreate.size());
        CellRemovalCounter_.Increment(mutations.CellsToRemove.size());
        ChangedNodeUserTagCounter_.Increment(mutations.ChangedNodeUserTags.size());
        ChangedDecommissionedFlagCounter_.Increment(mutations.ChangedDecommissionedFlag.size());
        ChangedChangeNodeAnnotationCounter_.Increment(mutations.ChangeNodeAnnotations.size());

        int nodeAllocationCount = 0;
        int nodeDeallocationCount = 0;
        int removingCellCount = 0;
        auto now = TInstant::Now();

        // TODO(capone212): think about per-bundle sensors.
        for (const auto& [_, state] : mutations.ChangedStates) {
            nodeAllocationCount += state->Allocations.size();
            nodeDeallocationCount += state->Deallocations.size();
            removingCellCount += state->RemovingCells.size();

            for (const auto& [_, allocation] : state->Allocations) {
                NodeAllocationRequestAge_.Update(now - allocation->CreationTime);
            }

            for (const auto& [_, deallocation] : state->Deallocations) {
                NodeDeallocationRequestAge_.Update(now - deallocation->CreationTime);
            }

            for (const auto& [_, removingCell] : state->RemovingCells) {
                RemovingCellsAge_.Update(now - removingCell->RemovedTime);
            }
        }

        InflightNodeAllocationCount_.Update(nodeAllocationCount);
        InflightNodeDeallocationCount_.Update(nodeDeallocationCount);
        InflightCellRemovalCount_.Update(removingCellCount);
    }

    ITransactionPtr CreateTransaction() const
    {
        TTransactionStartOptions transactionOptions;
        auto attributes = CreateEphemeralAttributes();
        attributes->Set("title", "Bundle Controller bundles scan");
        transactionOptions.Attributes = std::move(attributes);
        transactionOptions.Timeout = Config_->BundleScanTransactionTimeout;

        return WaitFor(Bootstrap_
            ->GetClient()
            ->StartTransaction(ETransactionType::Master, transactionOptions))
            .ValueOrThrow();
    }

    TSchedulerInputState GetInputState(const ITransactionPtr& transaction) const
    {
        TSchedulerInputState inputState{
            .Config = Config_,
        };

        inputState.Zones = CypressGet<TZoneInfo>(transaction, GetZonesPath());
        inputState.Bundles = CypressGet<TBundleInfo>(transaction, TabletCellBundlesPath);
        inputState.BundleStates = CypressGet<TBundleControllerState>(transaction, GetBundlesStatePath());
        inputState.TabletNodes = CypressGet<TTabletNodeInfo>(transaction, TabletNodesPath);
        inputState.TabletCells = CypressGet<TTabletCellInfo>(transaction, TabletCellsPath);

        inputState.AllocationRequests = LoadHulkRequests<TAllocationRequest>(
            transaction,
            {Config_->HulkAllocationsPath, Config_->HulkAllocationsHistoryPath},
            GetAliveAllocationsId(inputState));

        inputState.DeallocationRequests = LoadHulkRequests<TDeallocationRequest>(
            transaction,
            {Config_->HulkDeallocationsPath, Config_->HulkDeallocationsHistoryPath},
            GetAliveDeallocationsId(inputState));

        inputState.DynamicConfig = GetBundlesDynamicConfig(transaction);

        YT_LOG_DEBUG("Bundle Controller input state loaded "
            "(ZoneCount: %v, BundleCount: %v, BundleStatesCount: %v, TabletNodesCount %v, TabletCellsCount: %v, "
            "NodeAllocationRequestCount: %v, NodeDeallocationRequestCount: %v)",
            inputState.Zones.size(),
            inputState.Bundles.size(),
            inputState.BundleStates.size(),
            inputState.TabletNodes.size(),
            inputState.TabletCells.size(),
            inputState.AllocationRequests.size(),
            inputState.DeallocationRequests.size());

        return inputState;
    }

    template <typename TEntryInfo>
    static TIndexedEntries<TEntryInfo> CypressGet(const ITransactionPtr& transaction, const TYPath& path)
    {
        TListNodeOptions options;
        options.Attributes = TEntryInfo::GetAttributes();

        auto yson = WaitFor(transaction->ListNode(path, options))
            .ValueOrThrow();
        auto entryList = ConvertTo<IListNodePtr>(yson);

        TIndexedEntries<TEntryInfo> result;
        for (const auto& entry : entryList->GetChildren()) {
            if (entry->GetType() != ENodeType::String) {
                THROW_ERROR_EXCEPTION("Unexpected entry type")
                    << TErrorAttribute("parent_path", path)
                    << TErrorAttribute("expected_type", ENodeType::String)
                    << TErrorAttribute("actual_type", entry->GetType());
            }
            const auto& name = entry->AsString()->GetValue();
            result[name] = ConvertTo<TIntrusivePtr<TEntryInfo>>(&entry->Attributes());
        }

        return result;
    }

    template <typename TEntryInfo>
    static void CypressSet(
        const ITransactionPtr& transaction,
        const TYPath& basePath,
        const TIndexedEntries<TEntryInfo>& entries)
    {
        for (const auto& [name, entry] : entries) {
            CypressSet(transaction, basePath, name, entry);
        }
    }

    template <typename TEntryInfoPtr>
    static void CypressSet(
        const ITransactionPtr& transaction,
        const TYPath& basePath,
        const TString& name,
        const TEntryInfoPtr& entry)
    {
        auto path = Format("%v/%v", basePath, NYPath::ToYPathLiteral(name));

        TCreateNodeOptions createOptions;
        createOptions.IgnoreExisting = true;
        createOptions.Recursive = true;
        WaitFor(transaction->CreateNode(path, EObjectType::MapNode, createOptions))
            .ThrowOnError();

        TMultisetAttributesNodeOptions options;
        WaitFor(transaction->MultisetAttributesNode(
            path + "/@",
            ConvertTo<IMapNodePtr>(entry),
            options))
            .ThrowOnError();
    }

    template <typename TEntryInfo>
    static void CreateHulkRequests(
        const ITransactionPtr& transaction,
        const TYPath& basePath,
        const TIndexedEntries<TEntryInfo>& requests)
    {
        for (const auto& [requestId, requestBody] : requests) {
            auto path = Format("%v/%v", basePath, NYPath::ToYPathLiteral(requestId));

            TCreateNodeOptions createOptions;
            createOptions.Attributes = CreateEphemeralAttributes();
            createOptions.Attributes->Set("value", ConvertToYsonString(requestBody));
            createOptions.Recursive = true;
            WaitFor(transaction->CreateNode(path, EObjectType::Document, createOptions))
                .ThrowOnError();
        }
    }

    template <typename TAttribute>
    static void SetNodeAttributes(
        const ITransactionPtr& transaction,
        const TString& attributeName,
        const THashMap<TString, TAttribute>& attributes)
    {
        for (const auto& [nodeId, attribute] : attributes) {
            auto path = Format("%v/%v/@%v",
                TabletNodesPath,
                NYPath::ToYPathLiteral(nodeId),
                NYPath::ToYPathLiteral(attributeName));

            TSetNodeOptions setOptions;
            WaitFor(transaction->SetNode(path, ConvertToYsonString(attribute), setOptions))
                .ThrowOnError();
        }
    }

    template <typename TEntryInfo>
    static TIndexedEntries<TEntryInfo> LoadHulkRequests(
        const ITransactionPtr& transaction,
        const std::vector<TYPath>& basePaths,
        const std::vector<TString>& requests)
    {
        TIndexedEntries<TEntryInfo> results;
        using TEntryInfoPtr = TIntrusivePtr<TEntryInfo>;

        for (const auto& requestId : requests) {
            auto request = LoadHulkRequest<TEntryInfoPtr>(transaction, basePaths, requestId);
            if (request) {
                results[requestId] = request;
            }
        }
        return results;
    }

    template <typename TEntryInfoPtr>
    static TEntryInfoPtr LoadHulkRequest(
        const ITransactionPtr& transaction,
        const std::vector<TYPath>& basePaths,
        const TString& id)
    {
        for (const auto& basePath: basePaths) {
            auto path = Format("%v/%v", basePath,  NYPath::ToYPathLiteral(id));

            if (!WaitFor(transaction->NodeExists(path)).ValueOrThrow()) {
                continue;
            }

            auto yson = WaitFor(transaction->GetNode(path))
                .ValueOrThrow();

            return ConvertTo<TEntryInfoPtr>(yson);
        }

        return {};
    }

    static void CreateTabletCells(const ITransactionPtr& transaction, const THashMap<TString, int>& cellsToCreate)
    {
        for (const auto& [bundleName, cellCount] : cellsToCreate) {
            TCreateObjectOptions createOptions;
            createOptions.Attributes = CreateEphemeralAttributes();
            createOptions.Attributes->Set("tablet_cell_bundle", bundleName);

            for (int index = 0; index < cellCount; ++index) {
                WaitFor(transaction->CreateObject(EObjectType::TabletCell, createOptions))
                    .ThrowOnError();
            }
        }
    }

    static void RemoveTabletCells(const ITransactionPtr& transaction, const std::vector<TString>& cellsToRemove)
    {
        for (const auto& cellId : cellsToRemove) {
            WaitFor(transaction->RemoveNode(Format("%v/%v", TabletCellsPath, cellId)))
                .ThrowOnError();
        }
    }

    static void SetBundlesDynamicConfig(
        const ITransactionPtr& transaction,
        const TBundlesDynamicConfig& config)
    {
        TSetNodeOptions setOptions;
        WaitFor(transaction->SetNode(TabletCellBundlesDynamicConfigPath, ConvertToYsonString(config), setOptions))
            .ThrowOnError();
    }

    static TBundlesDynamicConfig GetBundlesDynamicConfig(const ITransactionPtr& transaction)
    {
        bool exists = WaitFor(transaction->NodeExists(TabletCellBundlesDynamicConfigPath))
            .ValueOrThrow();

        if (!exists) {
            return {};
        }

        auto yson = WaitFor(transaction->GetNode(TabletCellBundlesDynamicConfigPath))
            .ValueOrThrow();

        return ConvertTo<TBundlesDynamicConfig>(yson);
    }

    bool IsLeader() const
    {
        VERIFY_THREAD_AFFINITY_ANY();

        return Bootstrap_->GetElectionManager()->IsLeader();
    }

    TYPath GetZonesPath() const
    {
        VERIFY_THREAD_AFFINITY_ANY();

        return Format("%v/zones", Config_->RootPath);
    }

    TYPath GetBundlesStatePath() const
    {
        VERIFY_THREAD_AFFINITY_ANY();

        return Format("%v/bundles_state", Config_->RootPath);
    }
};

////////////////////////////////////////////////////////////////////////////////

IBundleControllerPtr CreateBundleController(IBootstrap* bootstrap, TBundleControllerConfigPtr config)
{
    return New<TBundleController>(bootstrap, std::move(config));
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NCellBalancer
