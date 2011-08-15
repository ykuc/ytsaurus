#include "block_store.h"
#include "chunk_store.h"

#include "../chunk_client/file_chunk_reader.h"

#include "../misc/assert.h"

namespace NYT {
namespace NChunkHolder {

////////////////////////////////////////////////////////////////////////////////

static NLog::TLogger& Logger = ChunkHolderLogger;

////////////////////////////////////////////////////////////////////////////////

TCachedBlock::TCachedBlock(const TBlockId& blockId, const TSharedRef& data)
    : TCacheValueBase<TBlockId, TCachedBlock, TBlockIdHash>(blockId)
    , Data(data)
{ }

TSharedRef TCachedBlock::GetData() const
{
    return Data;
}

////////////////////////////////////////////////////////////////////////////////

class TBlockStore::TCachedReader
    : public TCacheValueBase<TChunkId, TCachedReader, TChunkIdHash>
    , public TFileChunkReader
{
public:
    typedef TIntrusivePtr<TCachedReader> TPtr;

    TCachedReader(const TChunkId& chunkId, Stroka fileName)
        : TCacheValueBase<TChunkId, TCachedReader, TGuidHash>(chunkId)
        , TFileChunkReader(fileName)
    { }

};

////////////////////////////////////////////////////////////////////////////////

class TBlockStore::TReaderCache
    : public TCapacityLimitedCache<TChunkId, TCachedReader, TGuidHash>
{
public:
    typedef TIntrusivePtr<TReaderCache> TPtr;

    TReaderCache(
        const TChunkHolderConfig& config,
        TChunkStore::TPtr chunkStore)
        : TCapacityLimitedCache<TChunkId, TCachedReader, TGuidHash>(config.MaxCachedFiles)
        , ChunkStore(chunkStore)
    { }

    TCachedReader::TPtr Get(TChunk::TPtr chunk)
    {
        TInsertCookie cookie(chunk->GetId());
        if (BeginInsert(&cookie)) {
            // TODO: IO exceptions and error checking
            TCachedReader::TPtr file = new TCachedReader(
                chunk->GetId(),
                ChunkStore->GetChunkFileName(chunk->GetId(), chunk->GetLocation()));
            EndInsert(file, &cookie);
        }
        return cookie.GetAsyncResult()->Get();
    }

private:
    TChunkStore::TPtr ChunkStore;

};

////////////////////////////////////////////////////////////////////////////////

class TBlockStore::TBlockCache 
    : public TCapacityLimitedCache<TBlockId, TCachedBlock, TBlockIdHash>
{
public:
    typedef TIntrusivePtr<TBlockCache> TPtr;

    TBlockCache(
        const TChunkHolderConfig& config,
        TChunkStore::TPtr chunkStore,
        TReaderCache::TPtr fileCache)
        : TCapacityLimitedCache<TBlockId, TCachedBlock, TBlockIdHash>(config.MaxCachedBlocks)
        , ChunkStore(chunkStore)
        , ReaderCache(fileCache)
    { }

    TCachedBlock::TPtr Put(const TBlockId& blockId, const TSharedRef& data)
    {
        TInsertCookie cookie(blockId);
        YVERIFY(BeginInsert(&cookie));
        TCachedBlock::TPtr block = new TCachedBlock(blockId, data);
        EndInsert(block, &cookie);
        return block;
    }

    TCachedBlock::TAsync::TPtr Find(const TBlockId& blockId)
    {
        TAutoPtr<TInsertCookie> cookie = new TInsertCookie(blockId);
        if (!BeginInsert(~cookie)) {
            LOG_DEBUG("Got cached block from store (BlockId: %s)",
                ~blockId.ToString());
            return cookie->GetAsyncResult();
        }

        TChunk::TPtr chunk = ChunkStore->FindChunk(blockId.ChunkId);
        if (~chunk == NULL)
            return NULL;
        
        LOG_DEBUG("Loading block into cache (BlockId: %s)",
            ~blockId.ToString());

        TCachedBlock::TAsync::TPtr result = cookie->GetAsyncResult();

        int location = chunk->GetLocation();
        IInvoker::TPtr invoker = ChunkStore->GetIOInvoker(location);
        invoker->Invoke(FromMethod(
            &TBlockCache::ReadBlock,
            TPtr(this),
            chunk,
            blockId,
            cookie));

        return result;
    }

private:
    TChunkStore::TPtr ChunkStore;
    TReaderCache::TPtr ReaderCache;

    void ReadBlock(
        TChunk::TPtr chunk,
        const TBlockId& blockId,
        TAutoPtr<TInsertCookie> cookie)
    {
        try {
            TCachedReader::TPtr reader = ReaderCache->Get(chunk);
            TSharedRef data = reader->ReadBlock(blockId.BlockIndex);
            if (data != TSharedRef()) {
                TCachedBlock::TPtr cachedBlock = new TCachedBlock(blockId, data);
                EndInsert(cachedBlock, ~cookie);

                LOG_DEBUG("Finished loading block into cache (BlockId: %s)", ~blockId.ToString());
            } else {
                LOG_WARNING("Attempt to read a non-existing block (BlockId: %s)", ~blockId.ToString());
            }
        } catch (...) {
            LOG_FATAL("Error loading block into cache (BlockId: %s, What: %s)",
                ~blockId.ToString(),
                ~CurrentExceptionMessage());
        }
    }
};

////////////////////////////////////////////////////////////////////////////////

TBlockStore::TBlockStore(
    const TChunkHolderConfig& config,
    TChunkStore::TPtr chunkStore)
    : FileCache(new TReaderCache(config, chunkStore))
    , BlockCache(new TBlockCache(config, chunkStore, FileCache))
{ }

TCachedBlock::TAsync::TPtr TBlockStore::FindBlock(const TBlockId& blockId)
{
    LOG_DEBUG("Getting block from store (BlockId: %s)", ~blockId.ToString());

    return BlockCache->Find(blockId);
}

TCachedBlock::TPtr TBlockStore::PutBlock(const TBlockId& blockId, const TSharedRef& data)
{
    LOG_DEBUG("Putting block into store (BlockId: %s, BlockSize: %d)",
        ~blockId.ToString(),
        static_cast<int>(data.Size()));

    return BlockCache->Put(blockId, data);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NChunkHolder
} // namespace NYT
