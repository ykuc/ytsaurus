﻿#pragma once

#include <core/logging/log.h>

#include <core/profiling/profiler.h>

namespace NYT {
namespace NMonitoring {

////////////////////////////////////////////////////////////////////////////////

extern const NLogging::TLogger MonitoringLogger;
extern NProfiling::TProfiler MonitoringProfiler;

////////////////////////////////////////////////////////////////////////////////

} // namespace NJournalClient
} // namespace NYT
