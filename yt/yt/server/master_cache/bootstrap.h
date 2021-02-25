#pragma once

#include "private.h"

namespace NYT::NMasterCache {

////////////////////////////////////////////////////////////////////////////////

struct IBootstrap
{
    virtual ~IBootstrap() = default;

    virtual void Initialize() = 0;
    virtual void Run() = 0;
};

////////////////////////////////////////////////////////////////////////////////

std::unique_ptr<IBootstrap> CreateBootstrap(TMasterCacheConfigPtr config);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NMasterCache
