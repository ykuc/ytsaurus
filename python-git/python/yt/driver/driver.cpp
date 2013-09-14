#include "common.h"
#include "stream.h"
#include "serialize.h"

#include <core/misc/intrusive_ptr.h>

#include <core/concurrency/async_stream.h>

#include <core/formats/format.h>

#include <ytlib/driver/config.h>
#include <ytlib/driver/driver.h>

#include <core/logging/log_manager.h>

#include <core/ytree/convert.h>

// For at_exit
#include <core/profiling/profiling_manager.h>

#include <core/rpc/dispatcher.h>

#include <core/bus/tcp_dispatcher.h>

#include <ytlib/chunk_client/dispatcher.h>

#include <contrib/libs/pycxx/Objects.hxx>
#include <contrib/libs/pycxx/Extensions.hxx>

#include <iostream>

namespace NYT {
namespace NPython {

////////////////////////////////////////////////////////////////////////////////

using namespace NFormats;
using namespace NDriver;
using namespace NYson;
using namespace NYTree;
using namespace NConcurrency;

// TODO(babenko): ExtractArgument? move to the place where other helpers reside?
Py::Object ExtractArgument(Py::Tuple& args, Py::Dict& kwds, const std::string& name)
{
    Py::Object result;
    if (kwds.hasKey(name)) {
        result = kwds[name];
        kwds.delItem(name);
    } else {
        if (args.length() == 0) {
            throw Py::RuntimeError("Missing no argument '" + name + "'");
        }
        result = args.front();
        args = args.getSlice(1, args.length());
    }
    return result;
}

////////////////////////////////////////////////////////////////////////////////

// TODO(babenko): TDriver?
class Driver
    : public Py::PythonClass<Driver>
{
public:
    Driver(
        Py::PythonClassInstance* self,
        Py::Tuple& args,
        Py::Dict& kwds)
            : Py::PythonClass<Driver>::PythonClass(self, args, kwds)
    {
        Py::Object configDict = ExtractArgument(args, kwds, "config");
        if (args.length() > 0 || kwds.length() > 0) {
            throw Py::RuntimeError("Incorrect arguments passed to Driver ctor");
        }
        auto config = New<TDriverConfig>();
        auto configNode = ConvertToNode(configDict);
        try {
            config->Load(configNode);
        } catch(const TErrorException& error) {
            throw Py::RuntimeError("Fail while loading configuration: " + error.Error().GetMessage());
        }
        NLog::TLogManager::Get()->Configure(configNode->AsMap()->FindChild("logging"));
        DriverInstance_ = CreateDriver(config);
    }

    // TODO(babenko): is this needed?
    virtual ~Driver()
    { }

    static void InitType()
    {
        behaviors().name("Driver");
        behaviors().doc("Some documentation");
        behaviors().supportGetattro();
        behaviors().supportSetattro();

        PYCXX_ADD_KEYWORDS_METHOD(execute, Execute, "TODO(ignat): make documentation");

        behaviors().readyType();
    }

    // TODO(babenko): move to private?
    Py::Object Execute(Py::Tuple& args, Py::Dict& kwds)
    {
        auto pyRequest = ExtractArgument(args, kwds, "request");
        if (args.length() > 0 || kwds.length() > 0) {
            throw Py::RuntimeError("Invalid arguments passed to 'execute'");
        }

        TDriverRequest request;
        request.CommandName = ConvertToStroka(Py::String(GetAttr(pyRequest, "command_name")));
        request.Arguments = ConvertToNode(GetAttr(pyRequest, "arguments"))->AsMap();

        std::unique_ptr<TPythonInputStream> inputStream;
        std::unique_ptr<TPythonOutputStream> outputStream;

        if (pyRequest.hasAttr("input_stream")) {
            inputStream = std::unique_ptr<TPythonInputStream>(
                new TPythonInputStream(GetAttr(pyRequest, "input_stream")));
            request.InputStream = CreateAsyncInputStream(inputStream.get());
        }

        if (pyRequest.hasAttr("output_stream")) {
            outputStream = std::unique_ptr<TPythonOutputStream>(
                new TPythonOutputStream(GetAttr(pyRequest, "output_stream")));
            request.OutputStream = CreateAsyncOutputStream(outputStream.get());
        }

        auto response = DriverInstance_->Execute(request).Get();
        return ConvertToPythonString(ToString(response.Error));
    }

    PYCXX_KEYWORDS_METHOD_DECL(Driver, Execute)

private:
    // TODO(babenko): rename to e.g. Driver
    IDriverPtr DriverInstance_;

};

////////////////////////////////////////////////////////////////////////////////

// TODO(babenko): rename to something more meaningful
class ytlib_python_module
    : public Py::ExtensionModule<ytlib_python_module>
{
public:
    ytlib_python_module()
        : Py::ExtensionModule<ytlib_python_module>("ytlib_python")
    {
        Py_AtExit(ytlib_python_module::at_exit);

        Driver::InitType();

        initialize("Ytlib python bindings");

        Py::Dict moduleDict(moduleDictionary());
        moduleDict["Driver"] = Driver::type();
    }

    static void at_exit()
    {
        // TODO: refactor system shutdown
        // XXX(sandello): Keep in sync with server/main.cpp, driver/main.cpp and utmain.cpp, python_bindings/driver.cpp
        NLog::TLogManager::Get()->Shutdown();
        NBus::TTcpDispatcher::Get()->Shutdown();
        NRpc::TDispatcher::Get()->Shutdown();
        NChunkClient::TDispatcher::Get()->Shutdown();
        NProfiling::TProfilingManager::Get()->Shutdown();
        TDelayedExecutor::Shutdown();
    }

    // TODO(babenko): is this needed?
    virtual ~ytlib_python_module()
    { }

};

////////////////////////////////////////////////////////////////////////////////

} // namespace NPython
} // namespace NYT


#if defined( _WIN32 )
    #define EXPORT_SYMBOL __declspec(dllexport)
#else
    #define EXPORT_SYMBOL
#endif

extern "C" EXPORT_SYMBOL void initytlib_python()
{
    static auto* ytlib_python = new NYT::NPython::ytlib_python_module;
    UNUSED(ytlib_python);
}

// Required for the debug version.
extern "C" EXPORT_SYMBOL void initytlib_python_d()
{
    initytlib_python();
}
