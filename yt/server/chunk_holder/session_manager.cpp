#include "stdafx.h"
#include "session_manager.h"
#include "private.h"
#include "config.h"
#include "location.h"
#include "block_store.h"
#include "chunk.h"
#include "chunk_store.h"
#include "bootstrap.h"

#include <ytlib/chunk_client/chunk.pb.h>
#include <ytlib/chunk_client/file_writer.h>

#include <ytlib/misc/fs.h>
#include <ytlib/misc/sync.h>

namespace NYT {
namespace NChunkHolder {

using namespace NRpc;
using namespace NChunkClient;
using namespace NChunkClient::NProto;

////////////////////////////////////////////////////////////////////////////////

static NLog::TLogger& Logger = DataNodeLogger;
static NProfiling::TProfiler& Profiler = DataNodeProfiler;

static NProfiling::TRateCounter WriteThroughputCounter("/chunk_io/write_throughput");

////////////////////////////////////////////////////////////////////////////////

TSession::TSession(
    TBootstrap* bootstrap,
    const TChunkId& chunkId,
    TLocationPtr location)
    : Bootstrap(bootstrap)
    , ChunkId(chunkId)
    , Location(location)
    , WindowStartIndex(0)
    , WriteIndex(0)
    , Size(0)
    , WriteInvoker(CreateSerializedInvoker(Location->GetWriteInvoker()))
    , Logger(DataNodeLogger)
{
    YCHECK(bootstrap);
    YCHECK(location);

    Logger.AddTag(Sprintf("LocationId: %s, ChunkId: %s",
        ~Location->GetId(),
        ~ChunkId.ToString()));

    Location->UpdateSessionCount(+1);
    FileName = Location->GetChunkFileName(ChunkId);
}

TSession::~TSession()
{
    Location->UpdateSessionCount(-1);
}

void TSession::Start()
{
    LOG_DEBUG("Session started");

    WriteInvoker->Invoke(BIND(&TSession::DoOpenFile, MakeStrong(this)));
}

void TSession::DoOpenFile()
{
    LOG_DEBUG("Started opening chunk writer");
    
    PROFILE_TIMING ("/chunk_io/chunk_writer_open_time") {
        try {
            Writer = New<TFileWriter>(FileName);
            Writer->Open();
        }
        catch (const std::exception& ex) {
            LOG_FATAL(ex, "Error opening chunk writer");
        }
    }

    LOG_DEBUG("Finished opening chunk writer");
}

void TSession::SetLease(TLeaseManager::TLease lease)
{
    Lease = lease;
}

void TSession::RenewLease()
{
    TLeaseManager::RenewLease(Lease);
}

void TSession::CloseLease()
{
    TLeaseManager::CloseLease(Lease);
}

TChunkId TSession::GetChunkId() const
{
    return ChunkId;
}

TLocationPtr TSession::GetLocation() const
{
    return Location;
}

i64 TSession::GetSize() const
{
    return Size;
}

int TSession::GetWrittenBlockCount() const
{
    return WindowStartIndex;
}

TChunkInfo TSession::GetChunkInfo() const
{
    return Writer->GetChunkInfo();
}

TSharedRef TSession::GetBlock(i32 blockIndex)
{
    VerifyInWindow(blockIndex);

    RenewLease();

    const auto& slot = GetSlot(blockIndex);
    if (slot.State == ESlotState::Empty) {
        THROW_ERROR_EXCEPTION(
            EErrorCode::WindowError,
            "Trying to retrieve a block %d that is not received yet (WindowStart: %d)",
            blockIndex,
            WindowStartIndex);
    }

    LOG_DEBUG("Chunk block %d retrieved", blockIndex);

    return slot.Block;
}

void TSession::PutBlock(
    i32 blockIndex,
    const TSharedRef& data,
    bool enableCaching)
{
    TBlockId blockId(ChunkId, blockIndex);

    VerifyInWindow(blockIndex);

    RenewLease();

    if (!Location->HasEnoughSpace(data.Size())) {
        THROW_ERROR_EXCEPTION(
            EErrorCode::OutOfSpace,
            "No enough space left on node");
    }

    auto& slot = GetSlot(blockIndex);
    if (slot.State != ESlotState::Empty) {
        if (TRef::CompareContent(slot.Block, data)) {
            LOG_WARNING("Block %d is already received", blockIndex);
            return;
        }

        THROW_ERROR_EXCEPTION(
            EErrorCode::BlockContentMismatch,
            "Block %d with a different content already received (WindowStart: %d)",
            blockIndex,
            WindowStartIndex);
    }

    slot.State = ESlotState::Received;
    slot.Block = data;

    if (enableCaching) {
        Bootstrap->GetBlockStore()->PutBlock(blockId, data, Null);
    }

    Location->UpdateUsedSpace(data.Size());
    Size += data.Size();

    LOG_DEBUG("Chunk block %d received", blockIndex);

    EnqueueWrites();
}

void TSession::EnqueueWrites()
{
    while (WriteIndex < Window.size()) {
        const auto& slot = GetSlot(WriteIndex);
        YCHECK(slot.State == ESlotState::Received || slot.State == ESlotState::Empty);
        if (slot.State == ESlotState::Empty)
            break;

        BIND(
            &TSession::DoWriteBlock,
            MakeStrong(this),
            slot.Block, 
            WriteIndex)
        .AsyncVia(WriteInvoker)
        .Run()
        .Subscribe(BIND(
            &TSession::OnBlockWritten,
            MakeStrong(this),
            WriteIndex)
        .Via(Bootstrap->GetControlInvoker()));
        ++WriteIndex;
    }
}

TVoid TSession::DoWriteBlock(const TSharedRef& block, i32 blockIndex)
{
    LOG_DEBUG("Started writing block %d", blockIndex);

    PROFILE_TIMING ("/chunk_io/block_write_time") {
        try {
            if (!Writer->TryWriteBlock(block)) {
                Sync(~Writer, &TFileWriter::GetReadyEvent);
                YUNREACHABLE();
            }
        } catch (const std::exception& ex) {
            TBlockId blockId(ChunkId, blockIndex);
            LOG_FATAL(ex, "Error writing chunk block %s",
                ~blockId.ToString());
        }
    }

    LOG_DEBUG("Finished writing block %d", blockIndex);

    Profiler.Enqueue("/chunk_io/block_write_size", block.Size());
    Profiler.Increment(WriteThroughputCounter, block.Size());

    return TVoid();
}

void TSession::OnBlockWritten(i32 blockIndex, TVoid)
{
    auto& slot = GetSlot(blockIndex);
    YASSERT(slot.State == ESlotState::Received);
    slot.State = ESlotState::Written;
    slot.IsWritten.Set(TVoid());
}

TFuture<void> TSession::FlushBlock(i32 blockIndex)
{
    // TODO: verify monotonicity of blockIndex

    VerifyInWindow(blockIndex);

    RenewLease();

    const auto& slot = GetSlot(blockIndex);
    if (slot.State == ESlotState::Empty) {
        THROW_ERROR_EXCEPTION(
            EErrorCode::WindowError,
            "Attempt to flush an unreceived block %d (WindowStart: %d, WindowSize: %" PRISZT ")",
            blockIndex,
            WindowStartIndex,
            Window.size());
    }

    // IsWritten is set in the control thread, hence no need for AsyncVia.
    return slot.IsWritten.ToFuture().Apply(BIND(
        &TSession::OnBlockFlushed,
        MakeStrong(this),
        blockIndex));
}

void TSession::OnBlockFlushed(i32 blockIndex, TVoid)
{
    ReleaseBlocks(blockIndex);
}

TFuture<TChunkPtr> TSession::Finish(const TChunkMeta& chunkMeta)
{
    CloseLease();

    for (i32 blockIndex = WindowStartIndex; blockIndex < Window.size(); ++blockIndex) {
        const TSlot& slot = GetSlot(blockIndex);
        if (slot.State != ESlotState::Empty) {
            THROW_ERROR_EXCEPTION(
                EErrorCode::WindowError,
                "Attempt to finish a session with an unflushed block %d (WindowStart: %d, WindowSize: %" PRISZT ")",
                blockIndex,
                WindowStartIndex,
                Window.size());
        }
    }

    LOG_DEBUG("Session finished");

    return CloseFile(chunkMeta).Apply(
        BIND(&TSession::OnFileClosed, MakeStrong(this))
        .AsyncVia(Bootstrap->GetControlInvoker()));
}

void TSession::Cancel(const TError& error)
{
    LOG_DEBUG("Session canceled\n%s", ~ToString(error));

    CloseLease();
    AbortWriter()
        .Apply(BIND(&TSession::OnWriterAborted, MakeStrong(this))
        .AsyncVia(Bootstrap->GetControlInvoker()));
}

TFuture<TVoid> TSession::AbortWriter()
{
    return
        BIND(&TSession::DoAbortWriter, MakeStrong(this))
        .AsyncVia(WriteInvoker)
        .Run();
}

TVoid TSession::DoAbortWriter()
{
    LOG_DEBUG("Started aborting chunk writer");

    PROFILE_TIMING ("/chunk_io/chunk_abort_time") {
        Writer->Abort();
        Writer.Reset();
    }

    LOG_DEBUG("Finished aborting chunk writer");

    return TVoid();
}

TVoid TSession::OnWriterAborted(TVoid)
{
    ReleaseSpaceOccupiedByBlocks();
    return TVoid();
}

TFuture<TVoid> TSession::CloseFile(const TChunkMeta& chunkMeta)
{
    return
        BIND(&TSession::DoCloseFile,MakeStrong(this), chunkMeta)
        .AsyncVia(WriteInvoker)
        .Run();
}

TVoid TSession::DoCloseFile(const TChunkMeta& chunkMeta)
{
    LOG_DEBUG("Started closing chunk writer");

    PROFILE_TIMING ("/chunk_io/chunk_writer_close_time") {
        try {
            Sync(~Writer, &TFileWriter::AsyncClose, chunkMeta);
        } catch (const std::exception& ex) {
            LOG_FATAL(ex, "Error closing chunk writer");
        }
    }

    LOG_DEBUG("Finished closing chunk writer");

    return TVoid();
}

TChunkPtr TSession::OnFileClosed(TVoid)
{
    ReleaseSpaceOccupiedByBlocks();
    auto chunk = New<TStoredChunk>(
        Location, 
        ChunkId, 
        Writer->GetChunkMeta(), 
        Writer->GetChunkInfo(),
        Bootstrap->GetMemoryUsageTracker());
    Bootstrap->GetChunkStore()->RegisterChunk(chunk);
    return chunk;
}

void TSession::ReleaseBlocks(i32 flushedBlockIndex)
{
    YASSERT(WindowStartIndex <= flushedBlockIndex);

    while (WindowStartIndex <= flushedBlockIndex) {
        auto& slot = GetSlot(WindowStartIndex);
        YASSERT(slot.State == ESlotState::Written);
        slot.Block = TSharedRef();
        slot.IsWritten.Reset();
        ++WindowStartIndex;
    }

    LOG_DEBUG("Released blocks (WindowStart: %d)",
        WindowStartIndex);
}

bool TSession::IsInWindow(i32 blockIndex)
{
    return blockIndex >= WindowStartIndex;
}

void TSession::VerifyInWindow(i32 blockIndex)
{
    if (!IsInWindow(blockIndex)) {
        THROW_ERROR_EXCEPTION(
            EErrorCode::WindowError,
            "Block %d is out of the window (WindowStart: %d, WindowSize: %" PRISZT ")",
            blockIndex,
            WindowStartIndex,
            Window.size());
    }
}

TSession::TSlot& TSession::GetSlot(i32 blockIndex)
{
    YASSERT(IsInWindow(blockIndex));
    
    Window.reserve(blockIndex + 1);

    while (Window.size() <= blockIndex) {
        // NB: do not use resize here! 
        // Newly added slots must get a fresh copy of IsWritten promise.
        // Using resize would cause all of these slots to share a single promise.
        Window.push_back(TSlot());
    }

    return Window[blockIndex];
}

void TSession::ReleaseSpaceOccupiedByBlocks()
{
    Location->UpdateUsedSpace(-Size);
}

////////////////////////////////////////////////////////////////////////////////

TSessionManager::TSessionManager(
    TDataNodeConfigPtr config,
    TBootstrap* bootstrap)
    : Config(config)
    , Bootstrap(bootstrap)
    , SessionCount(0)
{
    YCHECK(config);
    YCHECK(bootstrap);
}

TSessionPtr TSessionManager::FindSession(const TChunkId& chunkId) const
{
    auto it = SessionMap.find(chunkId);
    if (it == SessionMap.end())
        return NULL;
    
    auto session = it->second;
    session->RenewLease();
    return session;
}

TSessionPtr TSessionManager::StartSession(const TChunkId& chunkId)
{
    auto location = Bootstrap->GetChunkStore()->GetNewChunkLocation();

    auto session = New<TSession>(Bootstrap, chunkId, location);
    session->Start();

    auto lease = TLeaseManager::CreateLease(
        Config->SessionTimeout,
        BIND(
            &TSessionManager::OnLeaseExpired,
            MakeStrong(this),
            session)
        .Via(Bootstrap->GetControlInvoker()));
    session->SetLease(lease);

    AtomicIncrement(SessionCount);
    YCHECK(SessionMap.insert(MakePair(chunkId, session)).second);

    return session;
}

void TSessionManager::CancelSession(TSessionPtr session, const TError& error)
{
    auto chunkId = session->GetChunkId();

    YCHECK(SessionMap.erase(chunkId) == 1);
    AtomicDecrement(SessionCount);

    session->Cancel(error);

    LOG_INFO("Session %s canceled\n%s",
        ~chunkId.ToString(),
        ~ToString(error));
}

TFuture<TChunkPtr> TSessionManager::FinishSession(
    TSessionPtr session, 
    const TChunkMeta& chunkMeta)
{
    auto chunkId = session->GetChunkId();

    return session
        ->Finish(chunkMeta)
        .Apply(BIND(
            &TSessionManager::OnSessionFinished,
            MakeStrong(this),
            session));
}

TChunkPtr TSessionManager::OnSessionFinished(TSessionPtr session, TChunkPtr chunk)
{
    YCHECK(SessionMap.erase(chunk->GetId()) == 1);
    AtomicDecrement(SessionCount);
    LOG_INFO("Session finished (ChunkId: %s)", ~session->GetChunkId().ToString());
    return chunk;
}

void TSessionManager::OnLeaseExpired(TSessionPtr session)
{
    if (SessionMap.find(session->GetChunkId()) != SessionMap.end()) {
        LOG_INFO("Session %s lease expired", ~session->GetChunkId().ToString());
        CancelSession(session, TError("Session lease expired"));
    }
}

int TSessionManager::GetSessionCount() const
{
    return SessionCount;
}

TSessionManager::TSessions TSessionManager::GetSessions() const
{
    TSessions result;
    YCHECK(SessionMap.ysize() == SessionCount);
    result.reserve(SessionMap.ysize());
    FOREACH (const auto& pair, SessionMap) {
        result.push_back(pair.second);
    }
    return result;
}

////////////////////////////////////////////////////////////////////////////////


} // namespace NChunkHolder
} // namespace NYT
