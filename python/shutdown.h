#pragma once

#include <yt/core/actions/callback.h>

namespace NYT {
namespace NPython {

////////////////////////////////////////////////////////////////////////////////

void RegisterShutdown(TCallback<void()> additionalCallback = BIND([] () {}));

////////////////////////////////////////////////////////////////////////////////

} // namespace NPython
} // namespace NYT

