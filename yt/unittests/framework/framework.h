#pragma once

// STLport does not have std::tr1::tuple, so we have to fallback to custom
// implementation.
#define GTEST_HAS_TR1_TUPLE 1
#define GTEST_USE_OWN_TR1_TUPLE 1

// Preconfigure all the namespaces; i. e. bind ::std to ::NStl
#include <util/private/stl/config.h>
#include <util/private/stl/stlport-5.1.4/stlport/stl/config/features.h>

namespace NStl {
    namespace tr1 {
    }
}

#include "framework/gtest.h"
#include "framework/gmock.h"
