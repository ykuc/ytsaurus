#pragma once

#include "private.h"

#include <yt/yt/server/lib/cypress_election/config.h>

#include <yt/yt/server/lib/dynamic_config/config.h>

#include <yt/yt/server/lib/misc/config.h>

#include <yt/yt/client/ypath/public.h>

#include <yt/yt/ytlib/api/native/public.h>

#include <yt/yt/core/ytree/yson_struct.h>

namespace NYT::NQueueAgent {

////////////////////////////////////////////////////////////////////////////////

class TCypressSynchronizerConfig
    : public NYTree::TYsonStruct
{
public:
    REGISTER_YSON_STRUCT(TCypressSynchronizerConfig)

    static void Register(TRegistrar registrar);
};

DEFINE_REFCOUNTED_TYPE(TCypressSynchronizerConfig)

////////////////////////////////////////////////////////////////////////////////

class TCypressSynchronizerDynamicConfig
    : public NYTree::TYsonStruct
{
public:
    //! Cypress poll period.
    TDuration PollPeriod;

    //! Flag for disabling cypress synchronizer entirely; used primarily for tests.
    bool Enable;

    REGISTER_YSON_STRUCT(TCypressSynchronizerDynamicConfig);

    static void Register(TRegistrar registrar);
};

DEFINE_REFCOUNTED_TYPE(TCypressSynchronizerDynamicConfig)

////////////////////////////////////////////////////////////////////////////////

class TQueueAgentConfig
    : public NYTree::TYsonStruct
{
public:
    //! Used to create channels to other queue agents.
    NBus::TTcpBusConfigPtr BusClient;

    REGISTER_YSON_STRUCT(TQueueAgentConfig)

    static void Register(TRegistrar registrar);
};

DEFINE_REFCOUNTED_TYPE(TQueueAgentConfig)

////////////////////////////////////////////////////////////////////////////////

class TQueueControllerDynamicConfig
    : public NYTree::TYsonStruct
{
public:
    //! Controller pass period. Defines the period of monitoring information exporting, automatic
    //! trimming and the maximum age of cached Orchid state.
    TDuration PassPeriod;

    REGISTER_YSON_STRUCT(TQueueControllerDynamicConfig)

    static void Register(TRegistrar registrar);
};

DEFINE_REFCOUNTED_TYPE(TQueueControllerDynamicConfig)

////////////////////////////////////////////////////////////////////////////////

class TQueueAgentDynamicConfig
    : public NYTree::TYsonStruct
{
public:
    //! State table poll period.
    TDuration PollPeriod;

    //! Controller thread pool thread count.
    int ControllerThreadCount;

    //! Configuration of queue controllers.
    TQueueControllerDynamicConfigPtr Controller;

    REGISTER_YSON_STRUCT(TQueueAgentDynamicConfig);

    static void Register(TRegistrar registrar);
};

DEFINE_REFCOUNTED_TYPE(TQueueAgentDynamicConfig)

////////////////////////////////////////////////////////////////////////////////

class TQueueAgentServerConfig
    : public TServerConfig
{
public:
    TQueueAgentConfigPtr QueueAgent;

    TCypressSynchronizerConfigPtr CypressSynchronizer;

    NApi::NNative::TConnectionConfigPtr ClusterConnection;

    bool AbortOnUnrecognizedOptions;

    //! User for native clients in queue agent and cypress synchronizer.
    TString User;

    //! The path of directory containing queue agent state.
    NYPath::TYPath Root;

    NYTree::IMapNodePtr CypressAnnotations;

    NCypressElection::TCypressElectionManagerConfigPtr ElectionManager;

    NDynamicConfig::TDynamicConfigManagerConfigPtr DynamicConfigManager;
    TString DynamicConfigPath;

    REGISTER_YSON_STRUCT(TQueueAgentServerConfig)

    static void Register(TRegistrar registrar);
};

DEFINE_REFCOUNTED_TYPE(TQueueAgentServerConfig)

////////////////////////////////////////////////////////////////////////////////

class TQueueAgentServerDynamicConfig
    : public TSingletonsDynamicConfig
{
public:
    TQueueAgentDynamicConfigPtr QueueAgent;
    TCypressSynchronizerDynamicConfigPtr CypressSynchronizer;

    REGISTER_YSON_STRUCT(TQueueAgentServerDynamicConfig);

    static void Register(TRegistrar registrar);
};

DEFINE_REFCOUNTED_TYPE(TQueueAgentServerDynamicConfig)

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NQueueAgent
