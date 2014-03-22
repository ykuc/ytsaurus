#pragma once

#include "public.h"

#include <ytlib/new_table_client/unversioned_row.h>

#include <core/misc/chunked_memory_pool.h>

#include <llvm/IR/TypeBuilder.h>

#include <unordered_map>
#include <unordered_set>

// This file serves two purposes: first, to define types used during interaction
// of native and JIT'ed code; second, to map necessary C++ types to LLVM types.

namespace llvm {
    class LLVMContext;
    class Function;
    class FunctionType;
    class Module;
    class Twine;
    class Value;
    class Instruction;
} // namespace llvm

namespace NYT {
namespace NQueryClient {

////////////////////////////////////////////////////////////////////////////////

using NVersionedTableClient::TRowBuffer;

class TPlanFragment;
class TCGFragment;

struct TPassedFragmentParams
{
    IEvaluateCallbacks* Callbacks;
    TPlanContext* Context;
    std::vector<TDataSplits>* DataSplitsArray;
    TRowBuffer* RowBuffer;
    ISchemafulWriter* Writer;
    std::vector<TRow>* Batch;
};

namespace NDetail {
    class TGroupHasher;
    class TGroupComparer;
} // namespace NDetail

typedef
    std::unordered_set<TRow, NDetail::TGroupHasher, NDetail::TGroupComparer>
    TLookupRows;

struct TCGBinding
{
    std::unordered_map<const TExpression*, int> NodeToConstantIndex;
    std::unordered_map<const TOperator*, int> ScanOpToDataSplits;
};

struct TCGVariables
{
    std::vector<TValue> ConstantArray;
    std::vector<TDataSplits> DataSplitsArray;
};

typedef void (*TCodegenedFunction)(
    TRow constants,
    TPassedFragmentParams* passedFragmentParams);

typedef
    std::remove_pointer<TCodegenedFunction>::type
    TCodegenedFunctionSignature;

const int MaxRowsPerRead  = 512;
const int MaxRowsPerWrite = 512;

namespace NDetail {

template <class T>
size_t THashCombine(size_t seed, const T& value)
{
    ::hash<T> hasher;
    return seed ^ (hasher(value) + 0x9e3779b9 + (seed << 6) + (seed >> 2));
    // TODO(lukyan): Fix this function
}

class TGroupHasher
{
public:
    explicit TGroupHasher(int keySize)
        : KeySize_(keySize)
    { }

    size_t operator() (TRow key) const
    {
        size_t result = 0;
        for (int i = 0; i < KeySize_; ++i) {
            result = THashCombine(result, GetHash(key[i]));
        }
        return result;
    }

private:
    int KeySize_;

};

class TGroupComparer
{
public:
    explicit TGroupComparer(int keySize)
        : KeySize_(keySize)
    { }

    bool operator() (TRow lhs, TRow rhs) const
    {
        for (int i = 0; i < KeySize_; ++i) {
            if (CompareRowValues(lhs[i], rhs[i])) {
                return false;
            }
        }
        return true;
    }

private:
    int KeySize_;

};

} // namespace NDetail

////////////////////////////////////////////////////////////////////////////////

} // namespace NQueryClient
} // namespace NYT

namespace llvm {

////////////////////////////////////////////////////////////////////////////////

using NYT::NQueryClient::TRow;
using NYT::NQueryClient::TRowHeader;
using NYT::NQueryClient::TValue;
using NYT::NQueryClient::TValueData;
using NYT::NQueryClient::TLookupRows;
using NYT::NQueryClient::TPassedFragmentParams;

// Opaque types

template <bool Cross>
class TypeBuilder<std::vector<TRow>*, Cross>
    : public TypeBuilder<void*, Cross>
{ };

template <bool Cross>
class TypeBuilder<TLookupRows*, Cross>
    : public TypeBuilder<void*, Cross>
{ };

template <bool Cross>
class TypeBuilder<TPassedFragmentParams*, Cross>
    : public TypeBuilder<void*, Cross>
{ };

// Aggregate types

template <bool Cross>
class TypeBuilder<TValueData, Cross>
{
public:
    static Type* get(LLVMContext& context)
    {
        enum
        {
            UnionSize0 = sizeof(i64),
            UnionSize1 = UnionSize0 > sizeof(double) ? UnionSize0 : sizeof(double),
            UnionSize2 = UnionSize1 > sizeof(const char*) ? UnionSize1 : sizeof(const char*)
        };

        static_assert(UnionSize2 == sizeof(i64), "Unexpected union size");

        return TypeBuilder<i64, Cross>::get(context);
    }

    enum Fields
    {
        Integer,
        Double,
        String
    };

    static Type* getAs(Fields dataFields, LLVMContext& context)
    {
        switch (dataFields) {
            case Fields::Integer:
                return TypeBuilder<i64*, Cross>::get(context);
            case Fields::Double:
                return TypeBuilder<double*, Cross>::get(context);
            case Fields::String:
                return TypeBuilder<const char**, Cross>::get(context);
        }
        YUNREACHABLE();
    }
};

template <bool Cross>
class TypeBuilder<TValue, Cross>
{
public:
    static StructType* get(LLVMContext& context)
    {
        return StructType::get(
            TypeBuilder<ui16, Cross>::get(context),
            TypeBuilder<ui16, Cross>::get(context),
            TypeBuilder<ui32, Cross>::get(context),
            TypeBuilder<TValueData, Cross>::get(context),
            nullptr);
    }

    enum Fields
    {
        Id,
        Type,
        Length,
        Data
    };
};

template <bool Cross>
class TypeBuilder<TRowHeader, Cross>
{
public:
    static StructType* get(LLVMContext& context)
    {
        return StructType::get(
            TypeBuilder<ui32, Cross>::get(context),
            TypeBuilder<ui32, Cross>::get(context),
            nullptr);
    }

    enum Fields
    {
        Count,
        Padding
    };
};

template <bool Cross>
class TypeBuilder<TRow, Cross>
{
public:
    static StructType* get(LLVMContext& context)
    {
        return StructType::get(
            TypeBuilder<TRowHeader*, Cross>::get(context),
            nullptr);
    }

    enum Fields
    {
        Header
    };
};

////////////////////////////////////////////////////////////////////////////////

} // namespace llvm

