#pragma once

#include "chunk.h"

#include "../misc/cache.h"
#include "../misc/config.h"
#include "../misc/ptr.h"

#include <util/generic/intrlist.h>

namespace NYT {

////////////////////////////////////////////////////////////////////////////////

class TCachedBlock
    : public TCacheValueBase<TBlockId, TCachedBlock, TBlockIdHash>
{
public:
    TCachedBlock(TBlockId blockId, const TSharedRef& data);

    TSharedRef GetData() const
    {
        return Data;
    }

private:
    const TSharedRef Data;

};

////////////////////////////////////////////////////////////////////////////////

// TODO: replace with TBlockCache::TConfig
struct TBlockCacheConfig
{
    i32 Capacity;

    explicit TBlockCacheConfig()
        : Capacity(1024)
    { }

    void Read(const TJsonObject* jsonConfig);
};

////////////////////////////////////////////////////////////////////////////////

class TBlockCache 
    : public TCapacityLimitedCache<TBlockId, TCachedBlock, TBlockIdHash>
{
public:
    TBlockCache(const TBlockCacheConfig& config);

    void Put(TCachedBlock::TPtr blockHolder);
    TCachedBlock::TPtr Put(TBlockId blockId, TSharedRef data);
    TCachedBlock::TPtr Get(TBlockId blockId); // returns NULL if missed
};

////////////////////////////////////////////////////////////////////////////////

}
