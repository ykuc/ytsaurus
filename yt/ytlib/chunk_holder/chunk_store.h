#pragma once

#include "common.h"

#include "../misc/cache.h"
#include "../actions/action_queue.h"
#include "../chunk_client/file_chunk_reader.h"

namespace NYT {
namespace NChunkHolder {

class TChunkStore;

////////////////////////////////////////////////////////////////////////////////

//! Describes chunk meta-information.
/*!
 *  This class holds some useful pieces of information that
 *  is impossible to fetch during holder startup since it requires reading chunk files.
 */
class TChunkMeta
    : public TRefCountedBase
{
public:
    typedef TIntrusivePtr<TChunkMeta> TPtr;

    TChunkMeta(TFileChunkReader::TPtr reader)
        : BlockCount(reader->GetBlockCount())
    { }

    i32 GetBlockCount() const
    {
        return BlockCount;
    }

private:
    friend class TChunkStore;

    i32 BlockCount;

};

////////////////////////////////////////////////////////////////////////////////

class TLocation
    : public TRefCountedBase
{
public:
    typedef TIntrusivePtr<TLocation> TPtr;

    TLocation(const Stroka& path);

    //! Updates UsedSpace and AvailalbleSpace
    void RegisterChunk(i64 size);

    //! Updates AvailalbleSpace with system call
    i64 GetAvailableSpace();

    IInvoker::TPtr GetInvoker() const
    {
        return Invoker;
    }

    i64 GetUsedSpace() const
    {
        return UsedSpace;
    }

    Stroka GetPath() const
    {
        return Path;
    }

    float GetLoad() const
    {
        return (float)UsedSpace / (UsedSpace + AvailableSpace);
    }

private:
    Stroka Path;
    i64 AvailableSpace;
    i64 UsedSpace;

    //! Actions queue that handle IO requests to this location.
    IInvoker::TPtr Invoker;
};

////////////////////////////////////////////////////////////////////////////////

//! Describes chunk at a chunk holder.
class TChunk
    : public TRefCountedBase
{
public:
    typedef TIntrusivePtr<TChunk> TPtr;

    TChunk(
        const TChunkId& id,
        i64 size,
        TLocation::TPtr location)
        : Id(id)
        , Size(size)
        , Location(location)
    { }

    //! Returns chunk id.
    TChunkId GetId() const
    {
        return Id;
    }

    //! Returns chunk size.
    i64 GetSize() const
    {
        return Size;
    }

    //! Returns chunk storage location.
    TLocation::TPtr GetLocation()
    {
        return Location;
    }

private:
    friend class TChunkStore;

    TChunkId Id;
    i64 Size;
    TLocation::TPtr Location;
    TChunkMeta::TPtr Meta;

};

////////////////////////////////////////////////////////////////////////////////

//! Manages uploaded chunks.
class TChunkStore
    : public TRefCountedBase
{
public:
    typedef TIntrusivePtr<TChunkStore> TPtr;
    typedef yvector<TChunk::TPtr> TChunks;

    //! Constructs a new instance.
    TChunkStore(const TChunkHolderConfig& config);

    //! Registers a just-uploaded chunk for further usage.
    TChunk::TPtr RegisterChunk(
        const TChunkId& chunkId,
        i64 size,
        TLocation::TPtr location);
    
    //! Finds chunk by id. Returns NULL if no chunk exists.
    TChunk::TPtr FindChunk(const TChunkId& chunkId);

    //! Fetches meta-information for a given chunk.
    TAsyncResult<TChunkMeta::TPtr>::TPtr GetChunkMeta(TChunk::TPtr chunk);

    //! Returns a (cached) chunk reader.
    /*!
     *  This call is thread-safe but may block since it actually opens the file.
     *  A common rule is to invoke it only from IO thread.
     */
    TFileChunkReader::TPtr GetChunkReader(TChunk::TPtr chunk);

    //! Physically removes the chunk.
    /*!
     *  This call also evicts the reader from the cache thus hopefully closing the file.
     */
    void RemoveChunk(TChunk::TPtr chunk);

    //! Calculates a storage location for a new chunk.
    TLocation::TPtr GetNewChunkLocation();

    //! Returns a full path to a chunk file.
    Stroka GetChunkFileName(const TChunkId& chunkId, TLocation::TPtr location);

    //! Returns a full path to a chunk file.
    Stroka GetChunkFileName(TChunk::TPtr chunk);

    //! Returns current statistics.
    THolderStatistics GetStatistics() const;

    //! Returns the list of all registered chunks.
    TChunks GetChunks();

    //! Raised when a chunk is added.
    TParamSignal<TChunk::TPtr>& ChunkAdded();

    //! Raised when a chunk is removed.
    TParamSignal<TChunk::TPtr>& ChunkRemoved();

private:
    class TCachedReader;
    class TReaderCache;

    TChunkHolderConfig Config;
    yvector<TLocation::TPtr> Locations;

    typedef yhash_map<TChunkId, TChunk::TPtr> TChunkMap;
    TChunkMap ChunkMap;

    //! Caches opened chunk files.
    TIntrusivePtr<TReaderCache> ReaderCache;

    TParamSignal<TChunk::TPtr> ChunkAdded_;
    TParamSignal<TChunk::TPtr> ChunkRemoved_;

    void ScanChunks();
    void InitLocations();
    TChunkMeta::TPtr DoGetChunkMeta(TChunk::TPtr chunk);

};

////////////////////////////////////////////////////////////////////////////////

} // namespace NChunkHolder
} // namespace NYT

