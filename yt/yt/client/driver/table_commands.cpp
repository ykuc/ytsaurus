#include "table_commands.h"
#include "config.h"

#include <yt/yt/client/api/rowset.h>
#include <yt/yt/client/api/transaction.h>
#include <yt/yt/client/api/skynet.h>
#include <yt/yt/client/api/table_reader.h>

#include <yt/yt/client/query_client/query_statistics.h>

#include <yt/yt/client/table_client/adapters.h>
#include <yt/yt/client/table_client/table_output.h>
#include <yt/yt/client/table_client/blob_reader.h>
#include <yt/yt/client/table_client/row_buffer.h>
#include <yt/yt/client/table_client/unversioned_writer.h>
#include <yt/yt/client/table_client/versioned_writer.h>
#include <yt/yt/client/table_client/columnar_statistics.h>
#include <yt/yt/client/table_client/table_consumer.h>

#include <yt/yt/client/tablet_client/table_mount_cache.h>

#include <yt/yt/client/formats/config.h>
#include <yt/yt/client/formats/parser.h>

#include <yt/yt/core/concurrency/periodic_executor.h>
#include <yt/yt/core/misc/finally.h>

namespace NYT::NDriver {

using namespace NYson;
using namespace NYTree;
using namespace NFormats;
using namespace NChunkClient;
using namespace NQueryClient;
using namespace NConcurrency;
using namespace NTransactionClient;
using namespace NHiveClient;
using namespace NTableClient;
using namespace NTabletClient;
using namespace NApi;

////////////////////////////////////////////////////////////////////////////////

static NLogging::TLogger WithCommandTag(
    const NLogging::TLogger& logger,
    const ICommandContextPtr& context)
{
    return logger.WithTag("Command: %v",
        context->Request().CommandName);
}
////////////////////////////////////////////////////////////////////////////////

TReadTableCommand::TReadTableCommand()
{
    RegisterParameter("path", Path);
    RegisterParameter("table_reader", TableReader)
        .Default();
    RegisterParameter("control_attributes", ControlAttributes)
        .DefaultNew();
    RegisterParameter("unordered", Unordered)
        .Default(false);
    RegisterParameter("start_row_index_only", StartRowIndexOnly)
        .Default(false);
    RegisterParameter("omit_inaccessible_columns", Options.OmitInaccessibleColumns)
        .Default(false);

    RegisterPostprocessor([&] {
        Path = Path.Normalize();
    });
}

void TReadTableCommand::DoExecute(ICommandContextPtr context)
{
    YT_LOG_DEBUG("Executing \"read_table\" command (Path: %v, Unordered: %v, StartRowIndexOnly: %v, "
        "OmitInaccessibleColumns: %v)",
        Path,
        Unordered,
        StartRowIndexOnly,
        Options.OmitInaccessibleColumns);
    Options.Ping = true;
    Options.EnableTableIndex = ControlAttributes->EnableTableIndex;
    Options.EnableRowIndex = ControlAttributes->EnableRowIndex;
    Options.EnableRangeIndex = ControlAttributes->EnableRangeIndex;
    Options.EnableTabletIndex = ControlAttributes->EnableTabletIndex;
    Options.Config = UpdateYsonSerializable(
        context->GetConfig()->TableReader,
        TableReader);

    if (StartRowIndexOnly) {
        Options.Config->WindowSize = 1;
        Options.Config->GroupSize = 1;
    }

    auto reader = WaitFor(context->GetClient()->CreateTableReader(
        Path,
        Options))
        .ValueOrThrow();

    ProduceResponseParameters(context, [&] (IYsonConsumer* consumer) {
        BuildYsonMapFragmentFluently(consumer)
            .Item("approximate_row_count").Value(reader->GetTotalRowCount())
            .Item("omitted_inaccessible_columns").Value(reader->GetOmittedInaccessibleColumns())
            .DoIf(reader->GetTotalRowCount() > 0, [&](auto fluent) {
                fluent
                    .Item("start_row_index").Value(reader->GetStartRowIndex());
            });
    });

    if (StartRowIndexOnly) {
        return;
    }

    auto writer = CreateStaticTableWriterForFormat(
        context->GetOutputFormat(),
        reader->GetNameTable(),
        {reader->GetTableSchema()},
        context->Request().OutputStream,
        false,
        ControlAttributes,
        0);

    auto finally = Finally([&] () {
        auto dataStatistics = reader->GetDataStatistics();
        YT_LOG_DEBUG("Command statistics (RowCount: %v, WrittenSize: %v, "
            "ReadUncompressedDataSize: %v, ReadCompressedDataSize: %v, "
            "OmittedInaccessibleColumns: %v)",
            dataStatistics.row_count(),
            writer->GetWrittenSize(),
            dataStatistics.uncompressed_data_size(),
            dataStatistics.compressed_data_size(),
            reader->GetOmittedInaccessibleColumns());
    });

    TPipeReaderToWriterOptions options;
    options.BufferRowCount = context->GetConfig()->ReadBufferRowCount;
    PipeReaderToWriter(
        reader,
        writer,
        options);
}

bool TReadTableCommand::HasResponseParameters() const
{
    return true;
}

////////////////////////////////////////////////////////////////////////////////

TReadBlobTableCommand::TReadBlobTableCommand()
{
    RegisterParameter("path", Path);
    RegisterParameter("part_size", PartSize);
    RegisterParameter("table_reader", TableReader)
        .Default();
    RegisterParameter("part_index_column_name", PartIndexColumnName)
        .Default();
    RegisterParameter("data_column_name", DataColumnName)
        .Default();
    RegisterParameter("start_part_index", StartPartIndex)
        .Default(0);
    RegisterParameter("offset", Offset)
        .Default(0);

    RegisterPostprocessor([&] {
        Path = Path.Normalize();
    });
}

void TReadBlobTableCommand::DoExecute(ICommandContextPtr context)
{
    if (Offset < 0) {
        THROW_ERROR_EXCEPTION("Offset must be nonnegative");
    }

    if (PartSize <= 0) {
        THROW_ERROR_EXCEPTION("Part size must be positive");
    }
    Options.Ping = true;

    auto config = UpdateYsonSerializable(
        context->GetConfig()->TableReader,
        TableReader);

    Options.Config = config;

    auto reader = WaitFor(context->GetClient()->CreateTableReader(
        Path,
        Options))
        .ValueOrThrow();

    auto input = CreateBlobTableReader(
        std::move(reader),
        PartIndexColumnName,
        DataColumnName,
        StartPartIndex,
        Offset,
        PartSize);

    auto output = context->Request().OutputStream;

    // TODO(ignat): implement proper Pipe* function.
    while (true) {
        auto block = WaitFor(input->Read())
            .ValueOrThrow();

        if (!block)
            break;

        WaitFor(output->Write(block))
            .ThrowOnError();
    }
}

////////////////////////////////////////////////////////////////////////////////

TLocateSkynetShareCommand::TLocateSkynetShareCommand()
{
    RegisterParameter("path", Path);

    RegisterPostprocessor([&] {
        Path = Path.Normalize();
    });
}

void TLocateSkynetShareCommand::DoExecute(ICommandContextPtr context)
{
    Options.Config = context->GetConfig()->TableReader;

    auto asyncSkynetPartsLocations = context->GetClient()->LocateSkynetShare(
        Path,
        Options);

    auto skynetPartsLocations = WaitFor(asyncSkynetPartsLocations);

    auto format = context->GetOutputFormat();
    auto syncOutputStream = CreateBufferedSyncAdapter(context->Request().OutputStream);

    auto consumer = CreateConsumerForFormat(
        format,
        EDataType::Structured,
        syncOutputStream.get());

    Serialize(*skynetPartsLocations.ValueOrThrow(), consumer.get());
    consumer->Flush();
}

////////////////////////////////////////////////////////////////////////////////

TWriteTableCommand::TWriteTableCommand()
{
    RegisterParameter("path", Path);
    RegisterParameter("table_writer", TableWriter)
        .Default();
    RegisterParameter("max_row_buffer_size", MaxRowBufferSize)
        .Default(1_MB);
    RegisterPostprocessor([&] {
        Path = Path.Normalize();
    });
}

void TWriteTableCommand::DoExecute(ICommandContextPtr context)
{
    auto transaction = AttachTransaction(context, false);

    auto config = UpdateYsonSerializable(
        context->GetConfig()->TableWriter,
        TableWriter);

    // XXX(babenko): temporary workaround; this is how it actually works but not how it is intended to be.
    Options.PingAncestors = true;
    Options.Config = config;

    auto apiWriter = WaitFor(context->GetClient()->CreateTableWriter(
        Path,
        Options))
        .ValueOrThrow();

    auto schemalessWriter = CreateSchemalessFromApiWriterAdapter(std::move(apiWriter));

    TWritingValueConsumer valueConsumer(
        schemalessWriter,
        ConvertTo<TTypeConversionConfigPtr>(context->GetInputFormat().Attributes()),
        MaxRowBufferSize);

    TTableOutput output(CreateParserForFormat(
        context->GetInputFormat(),
        &valueConsumer));

    PipeInputToOutput(context->Request().InputStream, &output, MaxRowBufferSize);

    WaitFor(valueConsumer.Flush())
        .ThrowOnError();

    WaitFor(schemalessWriter->Close())
        .ThrowOnError();

    ProduceEmptyOutput(context);
}

////////////////////////////////////////////////////////////////////////////////

TGetTableColumnarStatisticsCommand::TGetTableColumnarStatisticsCommand()
{
    RegisterParameter("paths", Paths);
    RegisterParameter("fetcher_mode", FetcherMode)
        .Default(EColumnarStatisticsFetcherMode::FromNodes);
    RegisterParameter("max_chunks_per_node_fetch", MaxChunksPerNodeFetch)
        .Default();

    RegisterPostprocessor([&] {
        for (auto& path : Paths) {
            path = path.Normalize();
            const auto& columns = path.GetColumns();
            if (!columns) {
                THROW_ERROR_EXCEPTION("It is required to specify column selectors in YPath for getting columnar statistics")
                    << TErrorAttribute("path", path);
            }
        }
    });
}

void TGetTableColumnarStatisticsCommand::DoExecute(ICommandContextPtr context)
{
    Options.FetchChunkSpecConfig = context->GetConfig()->TableReader;
    Options.FetcherConfig = context->GetConfig()->Fetcher;

    if (MaxChunksPerNodeFetch) {
        Options.FetcherConfig = CloneYsonSerializable(Options.FetcherConfig);
        Options.FetcherConfig->MaxChunksPerNodeFetch = *MaxChunksPerNodeFetch;
    }

    std::vector<std::vector<TString>> allColumns;
    allColumns.reserve(Paths.size());
    for (int index = 0; index < std::ssize(Paths); ++index) {
        allColumns.push_back(*Paths[index].GetColumns());
    }

    Options.FetcherMode = FetcherMode;

    auto transaction = AttachTransaction(context, false);

    // NB(psushin): This keepalive is an ugly hack for a long-running command with structured output - YT-9713.
    // Remove once framing is implemented - YT-9838.
    static auto keepAliveSpace = TSharedRef::FromString(" ");

    // TODO(prime@): this code should be removed, once HTTP framing is deployed.
    auto writeLock = std::make_shared<TAdaptiveLock>();
    auto writeRequested = std::make_shared<bool>(false);
    auto writeStopped = NewPromise<void>();

    auto keepAliveCallback = [context, writeLock, writeStopped, writeRequested] () {
        auto guard = Guard(*writeLock);
        if (*writeRequested) {
            writeStopped.TrySet();
            return;
        }

        auto error = WaitFor(context->Request().OutputStream->Write(keepAliveSpace));
        // Ignore errors here. If user closed connection, it must be handled on the upper layer.
        Y_UNUSED(error);
    };

    auto keepAliveExecutor = New<TPeriodicExecutor>(
        GetSyncInvoker(),
        BIND(keepAliveCallback),
        TDuration::MilliSeconds(100));
    keepAliveExecutor->Start();

    auto allStatisticsOrError = WaitFor(context->GetClient()->GetColumnarStatistics(Paths, Options));

    {
        auto guard = Guard(*writeLock);
        *writeRequested = true;
    }

    WaitFor(writeStopped.ToFuture())
        .ThrowOnError();
    WaitFor(keepAliveExecutor->Stop())
        .ThrowOnError();

    auto allStatistics = allStatisticsOrError.ValueOrThrow();

    YT_VERIFY(allStatistics.size() == Paths.size());
    for (int index = 0; index < std::ssize(allStatistics); ++index) {
        YT_VERIFY(allColumns[index].size() == allStatistics[index].ColumnDataWeights.size());
    }

    ProduceOutput(context, [&] (IYsonConsumer* consumer) {
        BuildYsonFluently(consumer)
            .DoList([&] (TFluentList fluent) {
                for (int index = 0; index < std::ssize(Paths); ++index) {
                    const auto& columns = allColumns[index];
                    const auto& statistics = allStatistics[index];
                    fluent
                        .Item()
                        .BeginMap()
                            .Item("column_data_weights").DoMap([&] (TFluentMap fluent) {
                                for (int index = 0; index < std::ssize(columns); ++index) {
                                    fluent
                                        .Item(columns[index]).Value(statistics.ColumnDataWeights[index]);
                                }
                            })
                            .OptionalItem("timestamp_total_weight", statistics.TimestampTotalWeight)
                            .Item("legacy_chunks_data_weight").Value(statistics.LegacyChunkDataWeight)
                        .EndMap();
                }
            });
    });
}

////////////////////////////////////////////////////////////////////////////////

TMountTableCommand::TMountTableCommand()
{
    RegisterParameter("cell_id", Options.CellId)
        .Optional();
    RegisterParameter("freeze", Options.Freeze)
        .Optional();
    RegisterParameter("target_cell_ids", Options.TargetCellIds)
        .Optional();
}

void TMountTableCommand::DoExecute(ICommandContextPtr context)
{
    auto asyncResult = context->GetClient()->MountTable(
        Path.GetPath(),
        Options);
    WaitFor(asyncResult)
        .ThrowOnError();

    ProduceEmptyOutput(context);
}

////////////////////////////////////////////////////////////////////////////////

TUnmountTableCommand::TUnmountTableCommand()
{
    RegisterParameter("force", Options.Force)
        .Optional();
}

void TUnmountTableCommand::DoExecute(ICommandContextPtr context)
{
    auto asyncResult = context->GetClient()->UnmountTable(
        Path.GetPath(),
        Options);
    WaitFor(asyncResult)
        .ThrowOnError();

    ProduceEmptyOutput(context);
}

////////////////////////////////////////////////////////////////////////////////

void TRemountTableCommand::DoExecute(ICommandContextPtr context)
{
    auto asyncResult = context->GetClient()->RemountTable(
        Path.GetPath(),
        Options);
    WaitFor(asyncResult)
        .ThrowOnError();

    ProduceEmptyOutput(context);
}

////////////////////////////////////////////////////////////////////////////////

void TFreezeTableCommand::DoExecute(ICommandContextPtr context)
{
    auto asyncResult = context->GetClient()->FreezeTable(
        Path.GetPath(),
        Options);
    WaitFor(asyncResult)
        .ThrowOnError();

    ProduceEmptyOutput(context);
}

////////////////////////////////////////////////////////////////////////////////

void TUnfreezeTableCommand::DoExecute(ICommandContextPtr context)
{
    auto asyncResult = context->GetClient()->UnfreezeTable(
        Path.GetPath(),
        Options);
    WaitFor(asyncResult)
        .ThrowOnError();

    ProduceEmptyOutput(context);
}

////////////////////////////////////////////////////////////////////////////////

TReshardTableCommand::TReshardTableCommand()
{
    RegisterParameter("pivot_keys", PivotKeys)
        .Default();
    RegisterParameter("tablet_count", TabletCount)
        .Default()
        .GreaterThan(0);
    RegisterParameter("uniform", Options.Uniform)
        .Default();

    RegisterPostprocessor([&] () {
        if (PivotKeys && TabletCount) {
            THROW_ERROR_EXCEPTION("Cannot specify both \"pivot_keys\" and \"tablet_count\"");
        }
        if (!PivotKeys && !TabletCount) {
            THROW_ERROR_EXCEPTION("Must specify either \"pivot_keys\" or \"tablet_count\"");
        }
        if (Options.Uniform && PivotKeys) {
            THROW_ERROR_EXCEPTION("\"uniform\" can be specified only with \"tablet_count\"");
        }
    });
}

void TReshardTableCommand::DoExecute(ICommandContextPtr context)
{
    TFuture<void> asyncResult;
    if (PivotKeys) {
        asyncResult = context->GetClient()->ReshardTable(
            Path.GetPath(),
            *PivotKeys,
            Options);
    } else {
        asyncResult = context->GetClient()->ReshardTable(
            Path.GetPath(),
            *TabletCount,
            Options);
    }
    WaitFor(asyncResult)
        .ThrowOnError();

    ProduceEmptyOutput(context);
}

////////////////////////////////////////////////////////////////////////////////

TReshardTableAutomaticCommand::TReshardTableAutomaticCommand()
{
    RegisterParameter("keep_actions", Options.KeepActions)
        .Default(false);
}

void TReshardTableAutomaticCommand::DoExecute(ICommandContextPtr context)
{
    auto asyncResult = context->GetClient()->ReshardTableAutomatic(
        Path.GetPath(),
        Options);
    auto tabletActions = WaitFor(asyncResult)
        .ValueOrThrow();
    context->ProduceOutputValue(BuildYsonStringFluently().List(tabletActions));
}

////////////////////////////////////////////////////////////////////////////////

TAlterTableCommand::TAlterTableCommand()
{
    RegisterParameter("path", Path);
    RegisterParameter("schema", Options.Schema)
        .Optional();
    RegisterParameter("dynamic", Options.Dynamic)
        .Optional();
    RegisterParameter("upstream_replica_id", Options.UpstreamReplicaId)
        .Optional();
    RegisterParameter("schema_modification", Options.SchemaModification)
        .Optional();
}

void TAlterTableCommand::DoExecute(ICommandContextPtr context)
{
    auto asyncResult = context->GetClient()->AlterTable(
        Path.GetPath(),
        Options);
    WaitFor(asyncResult)
        .ThrowOnError();

    ProduceEmptyOutput(context);
}

////////////////////////////////////////////////////////////////////////////////

TSelectRowsCommand::TSelectRowsCommand()
{
    RegisterParameter("query", Query);
    RegisterParameter("input_row_limit", Options.InputRowLimit)
        .Optional();
    RegisterParameter("output_row_limit", Options.OutputRowLimit)
        .Optional();
    RegisterParameter("use_multijoin", Options.UseMultijoin)
        .Optional();
    RegisterParameter("allow_full_scan", Options.AllowFullScan)
        .Optional();
    RegisterParameter("allow_join_without_index", Options.AllowJoinWithoutIndex)
        .Optional();
    RegisterParameter("execution_pool", Options.ExecutionPool)
        .Optional();
    RegisterParameter("fail_on_incomplete_result", Options.FailOnIncompleteResult)
        .Optional();
    RegisterParameter("enable_code_cache", Options.EnableCodeCache)
        .Optional();
    RegisterParameter("workload_descriptor", Options.WorkloadDescriptor)
        .Optional();
    RegisterParameter("enable_statistics", EnableStatistics)
        .Optional();
}

bool TSelectRowsCommand::HasResponseParameters() const
{
    return true;
}

void TSelectRowsCommand::DoExecute(ICommandContextPtr context)
{
    auto clientBase = GetClientBase(context);
    auto result = WaitFor(clientBase->SelectRows(Query, Options))
        .ValueOrThrow();

    const auto& rowset = result.Rowset;
    const auto& statistics = result.Statistics;

    YT_LOG_INFO("Query result statistics (%v)", statistics);

    if (EnableStatistics) {
        ProduceResponseParameters(context, [&] (NYson::IYsonConsumer* consumer) {
            Serialize(statistics, consumer);
        });
    }

    auto format = context->GetOutputFormat();
    auto output = context->Request().OutputStream;
    // TODO(babenko): refcounted schema
    auto writer = CreateSchemafulWriterForFormat(format, New<TTableSchema>(rowset->GetSchema()), output);

    writer->Write(rowset->GetRows());

    WaitFor(writer->Close())
        .ThrowOnError();
}

////////////////////////////////////////////////////////////////////////////////

TExplainQueryCommand::TExplainQueryCommand()
{
    RegisterParameter("query", Query);
    RegisterParameter("verbose_output", Options.VerboseOutput)
        .Optional();
}

void TExplainQueryCommand::DoExecute(ICommandContextPtr context)
{
    auto clientBase = GetClientBase(context);
    auto result = WaitFor(clientBase->ExplainQuery(Query, Options))
        .ValueOrThrow();
    context->ProduceOutputValue(result);
}

////////////////////////////////////////////////////////////////////////////////

static std::vector<TUnversionedRow> ParseRows(
    ICommandContextPtr context,
    TBuildingValueConsumer* valueConsumer)
{
    TTableOutput output(CreateParserForFormat(
        context->GetInputFormat(),
        valueConsumer));

    auto input = CreateSyncAdapter(context->Request().InputStream);
    PipeInputToOutput(input.get(), &output, 64_KB);
    return valueConsumer->GetRows();
}

TInsertRowsCommand::TInsertRowsCommand()
{
    RegisterParameter("require_sync_replica", Options.RequireSyncReplica)
        .Optional();
    RegisterParameter("sequence_number", Options.SequenceNumber)
        .Optional();
    RegisterParameter("table_writer", TableWriter)
        .Default();
    RegisterParameter("path", Path);
    RegisterParameter("update", Update)
        .Default(false);
    RegisterParameter("aggregate", Aggregate)
        .Default(false);
}

void TInsertRowsCommand::DoExecute(ICommandContextPtr context)
{
    auto config = UpdateYsonSerializable(
        context->GetConfig()->TableWriter,
        TableWriter);

    auto tableMountCache = context->GetClient()->GetTableMountCache();
    auto tableInfo = WaitFor(tableMountCache->GetTableInfo(Path.GetPath()))
        .ValueOrThrow();

    tableInfo->ValidateDynamic();

    if (!tableInfo->IsSorted() && Update) {
        THROW_ERROR_EXCEPTION("Cannot use \"update\" mode for ordered tables");
    }

    struct TInsertRowsBufferTag
    { };

    auto insertRowsFormatConfig = ConvertTo<TInsertRowsFormatConfigPtr>(context->GetInputFormat().Attributes());
    auto typeConversionConfig = ConvertTo<TTypeConversionConfigPtr>(context->GetInputFormat().Attributes());
    // Parse input data.
    TBuildingValueConsumer valueConsumer(
        tableInfo->Schemas[ETableSchemaKind::Write],
        WithCommandTag(Logger, context),
        insertRowsFormatConfig->EnableNullToYsonEntityConversion,
        typeConversionConfig);
    valueConsumer.SetAggregate(Aggregate);
    valueConsumer.SetTreatMissingAsNull(!Update);

    auto rows = ParseRows(context, &valueConsumer);
    auto rowBuffer = New<TRowBuffer>(TInsertRowsBufferTag());
    auto capturedRows = rowBuffer->CaptureRows(rows);
    auto rowRange = MakeSharedRange(
        std::vector<TUnversionedRow>(capturedRows.begin(), capturedRows.end()),
        std::move(rowBuffer));

    // Run writes.
    auto transaction = GetTransaction(context);

    transaction->WriteRows(
        Path.GetPath(),
        valueConsumer.GetNameTable(),
        std::move(rowRange),
        Options);

    if (ShouldCommitTransaction()) {
        WaitFor(transaction->Commit())
            .ThrowOnError();
    }
}

////////////////////////////////////////////////////////////////////////////////

TLookupRowsCommand::TLookupRowsCommand()
{
    RegisterParameter("table_writer", TableWriter)
        .Default();
    RegisterParameter("path", Path);
    RegisterParameter("column_names", ColumnNames)
        .Default();
    RegisterParameter("versioned", Versioned)
        .Default(false);
    RegisterParameter("retention_config", RetentionConfig)
        .Optional();
    RegisterParameter("keep_missing_rows", Options.KeepMissingRows)
        .Optional();
    RegisterParameter("enable_partial_result", Options.EnablePartialResult)
        .Optional();
    RegisterParameter("use_lookup_cache", Options.UseLookupCache)
        .Optional();
    RegisterParameter("cached_sync_replicas_timeout", Options.CachedSyncReplicasTimeout)
        .Optional();
}

void TLookupRowsCommand::DoExecute(ICommandContextPtr context)
{
    auto tableMountCache = context->GetClient()->GetTableMountCache();
    auto asyncTableInfo = tableMountCache->GetTableInfo(Path.GetPath());
    auto tableInfo = WaitFor(asyncTableInfo)
        .ValueOrThrow();

    tableInfo->ValidateDynamic();

    auto config = UpdateYsonSerializable(
        context->GetConfig()->TableWriter,
        TableWriter);

    if (Path.GetColumns()) {
        THROW_ERROR_EXCEPTION("Columns cannot be specified with table path, use \"column_names\" instead")
            << TErrorAttribute("rich_ypath", Path);
    }
    if (Path.HasNontrivialRanges()) {
        THROW_ERROR_EXCEPTION("Ranges cannot be specified")
            << TErrorAttribute("rich_ypath", Path);
    }

    struct TLookupRowsBufferTag
    { };

    // Parse input data.
    TBuildingValueConsumer valueConsumer(
        tableInfo->Schemas[ETableSchemaKind::Lookup],
        WithCommandTag(Logger, context),
        /*convertNullToEntity*/ false,
        ConvertTo<TTypeConversionConfigPtr>(context->GetInputFormat().Attributes()));
    auto keys = ParseRows(context, &valueConsumer);
    auto rowBuffer = New<TRowBuffer>(TLookupRowsBufferTag());
    auto capturedKeys = rowBuffer->CaptureRows(keys);
    auto mutableKeyRange = MakeSharedRange(std::move(capturedKeys), std::move(rowBuffer));
    auto keyRange = TSharedRange<TUnversionedRow>(
        static_cast<const TUnversionedRow*>(mutableKeyRange.Begin()),
        static_cast<const TUnversionedRow*>(mutableKeyRange.End()),
        mutableKeyRange.GetHolder());
    auto nameTable = valueConsumer.GetNameTable();

    if (ColumnNames) {
        TColumnFilter::TIndexes columnFilterIndexes;
        for (const auto& name : *ColumnNames) {
            auto optionalIndex = nameTable->FindId(name);
            if (!optionalIndex) {
                if (!tableInfo->Schemas[ETableSchemaKind::Primary]->FindColumn(name)) {
                    THROW_ERROR_EXCEPTION("No such column %Qv",
                        name);
                }
                optionalIndex = nameTable->GetIdOrRegisterName(name);
            }
            columnFilterIndexes.push_back(*optionalIndex);
        }
        Options.ColumnFilter = TColumnFilter(std::move(columnFilterIndexes));
    }

    // Run lookup.
    auto format = context->GetOutputFormat();
    auto output = context->Request().OutputStream;

    auto clientBase = GetClientBase(context);

    if (Versioned) {
        TVersionedLookupRowsOptions versionedOptions;
        versionedOptions.ColumnFilter = Options.ColumnFilter;
        versionedOptions.KeepMissingRows = Options.KeepMissingRows;
        versionedOptions.UseLookupCache = Options.UseLookupCache;
        versionedOptions.Timestamp = Options.Timestamp;
        versionedOptions.CachedSyncReplicasTimeout = Options.CachedSyncReplicasTimeout;
        versionedOptions.RetentionConfig = RetentionConfig;
        auto asyncRowset = clientBase->VersionedLookupRows(Path.GetPath(), std::move(nameTable), std::move(keyRange), versionedOptions);
        auto rowset = WaitFor(asyncRowset)
            .ValueOrThrow();
        // TODO(babenko): refcounted schema
        auto writer = CreateVersionedWriterForFormat(format, New<TTableSchema>(rowset->GetSchema()), output);
        writer->Write(rowset->GetRows());
        WaitFor(writer->Close())
            .ThrowOnError();
    } else {
        auto asyncRowset = clientBase->LookupRows(Path.GetPath(), std::move(nameTable), std::move(keyRange), Options);
        auto rowset = WaitFor(asyncRowset)
            .ValueOrThrow();

        // TODO(babenko): refcounted schema
        auto writer = CreateSchemafulWriterForFormat(format, New<TTableSchema>(rowset->GetSchema()), output);
        writer->Write(rowset->GetRows());
        WaitFor(writer->Close())
            .ThrowOnError();
    }
}

////////////////////////////////////////////////////////////////////////////////

TGetInSyncReplicasCommand::TGetInSyncReplicasCommand()
{
    RegisterParameter("path", Path);
    RegisterParameter("timestamp", Options.Timestamp);
    RegisterParameter("all_keys", AllKeys)
        .Default(false);
}

void TGetInSyncReplicasCommand::DoExecute(ICommandContextPtr context)
{
    auto tableMountCache = context->GetClient()->GetTableMountCache();
    auto asyncTableInfo = tableMountCache->GetTableInfo(Path.GetPath());
    auto tableInfo = WaitFor(asyncTableInfo)
        .ValueOrThrow();

    tableInfo->ValidateDynamic();
    tableInfo->ValidateReplicated();

    TFuture<std::vector<NTabletClient::TTableReplicaId>> asyncReplicas;
    if (AllKeys) {
        asyncReplicas = context->GetClient()->GetInSyncReplicas(
            Path.GetPath(),
            Options);
    } else {
        struct TInSyncBufferTag
        { };

        // Parse input data.
        TBuildingValueConsumer valueConsumer(
            tableInfo->Schemas[ETableSchemaKind::Lookup],
            WithCommandTag(Logger, context),
            /*convertNullToEntity*/ false,
            ConvertTo<TTypeConversionConfigPtr>(context->GetInputFormat().Attributes()));
        auto keys = ParseRows(context, &valueConsumer);
        auto rowBuffer = New<TRowBuffer>(TInSyncBufferTag());
        auto capturedKeys = rowBuffer->CaptureRows(keys);
        auto mutableKeyRange = MakeSharedRange(std::move(capturedKeys), std::move(rowBuffer));
        auto keyRange = TSharedRange<TUnversionedRow>(
            static_cast<const TUnversionedRow*>(mutableKeyRange.Begin()),
            static_cast<const TUnversionedRow*>(mutableKeyRange.End()),
            mutableKeyRange.GetHolder());
        auto nameTable = valueConsumer.GetNameTable();
        asyncReplicas = context->GetClient()->GetInSyncReplicas(
            Path.GetPath(),
            std::move(nameTable),
            std::move(keyRange),
            Options);
    }

    auto replicas = WaitFor(asyncReplicas)
        .ValueOrThrow();

    context->ProduceOutputValue(BuildYsonStringFluently()
        .List(replicas));
}

////////////////////////////////////////////////////////////////////////////////

TDeleteRowsCommand::TDeleteRowsCommand()
{
    RegisterParameter("sequence_number", Options.SequenceNumber)
        .Optional();
    RegisterParameter("require_sync_replica", Options.RequireSyncReplica)
        .Optional();
    RegisterParameter("table_writer", TableWriter)
        .Default();
    RegisterParameter("path", Path);
}

void TDeleteRowsCommand::DoExecute(ICommandContextPtr context)
{
    auto config = UpdateYsonSerializable(
        context->GetConfig()->TableWriter,
        TableWriter);

    auto tableMountCache = context->GetClient()->GetTableMountCache();
    auto asyncTableInfo = tableMountCache->GetTableInfo(Path.GetPath());
    auto tableInfo = WaitFor(asyncTableInfo)
        .ValueOrThrow();

    tableInfo->ValidateDynamic();
    tableInfo->ValidateSorted();

    struct TDeleteRowsBufferTag
    { };

    // Parse input data.
    TBuildingValueConsumer valueConsumer(
        tableInfo->Schemas[ETableSchemaKind::Delete],
        WithCommandTag(Logger, context),
        /*convertNullToEntity*/ false,
        ConvertTo<TTypeConversionConfigPtr>(context->GetInputFormat().Attributes()));
    auto keys = ParseRows(context, &valueConsumer);
    auto rowBuffer = New<TRowBuffer>(TDeleteRowsBufferTag());
    auto capturedKeys = rowBuffer->CaptureRows(keys);
    auto keyRange = MakeSharedRange(
        std::vector<TLegacyKey>(capturedKeys.begin(), capturedKeys.end()),
        std::move(rowBuffer));

    // Run deletes.
    auto transaction = GetTransaction(context);

    transaction->DeleteRows(
        Path.GetPath(),
        valueConsumer.GetNameTable(),
        std::move(keyRange),
        Options);

    if (ShouldCommitTransaction()) {
        WaitFor(transaction->Commit())
            .ThrowOnError();
    }

    ProduceEmptyOutput(context);
}

////////////////////////////////////////////////////////////////////////////////

TLockRowsCommand::TLockRowsCommand()
{
    RegisterParameter("table_writer", TableWriter)
        .Default();
    RegisterParameter("path", Path);
    RegisterParameter("locks", Locks);
    RegisterParameter("lock_type", LockType)
        .Default(NTableClient::ELockType::SharedStrong);
}

void TLockRowsCommand::DoExecute(ICommandContextPtr context)
{
    auto config = UpdateYsonSerializable(
        context->GetConfig()->TableWriter,
        TableWriter);

    auto tableMountCache = context->GetClient()->GetTableMountCache();
    auto asyncTableInfo = tableMountCache->GetTableInfo(Path.GetPath());
    auto tableInfo = WaitFor(asyncTableInfo)
        .ValueOrThrow();

    tableInfo->ValidateDynamic();

    auto transactionPool = context->GetDriver()->GetStickyTransactionPool();
    auto transaction = transactionPool->GetTransactionAndRenewLeaseOrThrow(Options.TransactionId);

    struct TLockRowsBufferTag
    { };

    // Parse input data.
    TBuildingValueConsumer valueConsumer(
        tableInfo->Schemas[ETableSchemaKind::Write],
        WithCommandTag(Logger, context),
        /*convertNullToEntity*/ false,
        ConvertTo<TTypeConversionConfigPtr>(context->GetInputFormat().Attributes()));
    auto keys = ParseRows(context, &valueConsumer);
    auto rowBuffer = New<TRowBuffer>(TLockRowsBufferTag());
    auto capturedKeys = rowBuffer->CaptureRows(keys);
    auto keyRange = MakeSharedRange(
        std::vector<TLegacyKey>(capturedKeys.begin(), capturedKeys.end()),
        std::move(rowBuffer));

    // Run locks.
    transaction->LockRows(
        Path.GetPath(),
        valueConsumer.GetNameTable(),
        std::move(keyRange),
        Locks,
        LockType);

    ProduceEmptyOutput(context);
}

////////////////////////////////////////////////////////////////////////////////

TTrimRowsCommand::TTrimRowsCommand()
{
    RegisterParameter("path", Path);
    RegisterParameter("tablet_index", TabletIndex);
    RegisterParameter("trimmed_row_count", TrimmedRowCount);
}

void TTrimRowsCommand::DoExecute(ICommandContextPtr context)
{
    auto client = context->GetClient();
    auto asyncResult = client->TrimTable(
        Path.GetPath(),
        TabletIndex,
        TrimmedRowCount,
        Options);
    WaitFor(asyncResult)
        .ThrowOnError();

    ProduceEmptyOutput(context);
}

////////////////////////////////////////////////////////////////////////////////

TEnableTableReplicaCommand::TEnableTableReplicaCommand()
{
    RegisterParameter("replica_id", ReplicaId);
    Options.Enabled = true;
}

void TEnableTableReplicaCommand::DoExecute(ICommandContextPtr context)
{
    auto client = context->GetClient();
    auto asyncResult = client->AlterTableReplica(ReplicaId, Options);
    WaitFor(asyncResult)
        .ThrowOnError();

    ProduceEmptyOutput(context);
}

////////////////////////////////////////////////////////////////////////////////

TDisableTableReplicaCommand::TDisableTableReplicaCommand()
{
    RegisterParameter("replica_id", ReplicaId);
    Options.Enabled = false;
}

void TDisableTableReplicaCommand::DoExecute(ICommandContextPtr context)
{
    auto client = context->GetClient();
    auto asyncResult = client->AlterTableReplica(ReplicaId, Options);
    WaitFor(asyncResult)
        .ThrowOnError();

    ProduceEmptyOutput(context);
}

////////////////////////////////////////////////////////////////////////////////

TAlterTableReplicaCommand::TAlterTableReplicaCommand()
{
    RegisterParameter("replica_id", ReplicaId);
    RegisterParameter("enabled", Options.Enabled)
        .Optional();
    RegisterParameter("mode", Options.Mode)
        .Optional();
    RegisterParameter("preserve_timestamps", Options.PreserveTimestamps)
        .Optional();
    RegisterParameter("atomicity", Options.Atomicity)
        .Optional();
}

void TAlterTableReplicaCommand::DoExecute(ICommandContextPtr context)
{
    auto client = context->GetClient();
    auto asyncResult = client->AlterTableReplica(ReplicaId, Options);
    WaitFor(asyncResult)
        .ThrowOnError();

    ProduceEmptyOutput(context);
}

////////////////////////////////////////////////////////////////////////////////

TGetTablePivotKeysCommand::TGetTablePivotKeysCommand()
{
    RegisterParameter("path", Path);
}

void TGetTablePivotKeysCommand::DoExecute(ICommandContextPtr context)
{
    auto asyncResult = context->GetClient()->GetTablePivotKeys(Path, Options);
    auto result = WaitFor(asyncResult)
        .ValueOrThrow();
    context->ProduceOutputValue(result);
}

////////////////////////////////////////////////////////////////////////////////

TGetTabletInfosCommand::TGetTabletInfosCommand()
{
    RegisterParameter("path", Path);
    RegisterParameter("tablet_indexes", TabletIndexes);
}

void TGetTabletInfosCommand::DoExecute(ICommandContextPtr context)
{
    auto client = context->GetClient();
    auto asyncTablets = client->GetTabletInfos(Path, TabletIndexes, Options);
    auto tablets = WaitFor(asyncTablets)
        .ValueOrThrow();

    ProduceOutput(context, [&] (IYsonConsumer* consumer) {
        BuildYsonFluently(consumer)
            .BeginMap()
                .Item("tablets").DoListFor(tablets, [&] (auto fluent, const auto& tablet) {
                    fluent
                        .Item().BeginMap()
                            .Item("total_row_count").Value(tablet.TotalRowCount)
                            .Item("trimmed_row_count").Value(tablet.TrimmedRowCount)
                            .Item("barrier_timestamp").Value(tablet.BarrierTimestamp)
                            .Item("last_write_timestamp").Value(tablet.LastWriteTimestamp)
                            .DoIf(tablet.TableReplicaInfos.has_value(), [&] (TFluentMap fluent) {
                                fluent
                                    .Item("replica_infos").DoListFor(
                                        *tablet.TableReplicaInfos,
                                        [&] (TFluentList fluent, const auto& replicaInfo) {
                                            fluent
                                                .Item()
                                                .BeginMap()
                                                    .Item("replica_id").Value(replicaInfo.ReplicaId)
                                                    .Item("last_replication_timestamp").Value(replicaInfo.LastReplicationTimestamp)
                                                    .Item("mode").Value(replicaInfo.Mode)
                                                    .Item("current_replication_row_index").Value(replicaInfo.CurrentReplicationRowIndex)
                                                .EndMap();
                                        });
                            })
                        .EndMap();
                })
            .EndMap();
    });
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NDriver
