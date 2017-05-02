#pragma once

#include "public.h"

#include "functions_common.h"

#include <yt/core/misc/ref.h>

#ifdef YT_IN_ARCADIA
#define UDF_BC(name) TSharedRef::FromString(::NResource::Find(Stroka("/llvm_bc/") + #name))
#else
#define UDF_BC(name) TSharedRef(name ## _bc, name ## _bc_len, nullptr)
#endif

namespace NYT {
namespace NQueryClient {

////////////////////////////////////////////////////////////////////////////////

struct TFunctionRegistryBuilder
{
    TFunctionRegistryBuilder(
        const TTypeInferrerMapPtr& typeInferrers,
        const TFunctionProfilerMapPtr& functionProfilers,
        const TAggregateProfilerMapPtr& aggregateProfilers)
        : TypeInferrers_(typeInferrers)
        , FunctionProfilers_(functionProfilers)
        , AggregateProfilers_(aggregateProfilers)
    { }

    TTypeInferrerMapPtr TypeInferrers_;
    TFunctionProfilerMapPtr FunctionProfilers_;
    TAggregateProfilerMapPtr AggregateProfilers_;

    void RegisterFunction(
        const Stroka& functionName,
        const Stroka& symbolName,
        std::unordered_map<TTypeArgument, TUnionType> typeArgumentConstraints,
        std::vector<TType> argumentTypes,
        TType repeatedArgType,
        TType resultType,
        TSharedRef implementationFile,
        ICallingConventionPtr callingConvention);

    void RegisterFunction(
        const Stroka& functionName,
        std::vector<TType> argumentTypes,
        TType resultType,
        TSharedRef implementationFile,
        ECallingConvention callingConvention);

    void RegisterFunction(
        const Stroka& functionName,
        std::unordered_map<TTypeArgument, TUnionType> typeArgumentConstraints,
        std::vector<TType> argumentTypes,
        TType repeatedArgType,
        TType resultType,
        TSharedRef implementationFile);

    void RegisterAggregate(
        const Stroka& aggregateName,
        std::unordered_map<TTypeArgument, TUnionType> typeArgumentConstraints,
        TType argumentType,
        TType resultType,
        TType stateType,
        TSharedRef implementationFile,
        ECallingConvention callingConvention);

};

////////////////////////////////////////////////////////////////////////////////

} // namespace NQueryClient
} // namespace NYT
