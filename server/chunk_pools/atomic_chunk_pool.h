#pragma once

#include "private.h"
#include "chunk_pool.h"

namespace NYT {
namespace NChunkPools {

////////////////////////////////////////////////////////////////////////////////

std::unique_ptr<IChunkPool> CreateAtomicChunkPool();

////////////////////////////////////////////////////////////////////////////////

} // namespace NChunkPools
} // namespace NYT