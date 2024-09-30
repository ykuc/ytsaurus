#ifndef ESTIMATE_SIZE_HELPERS_INL_H_
#error "Direct inclusion of this file is not allowed, include estimate_size_helpers.h"
// For the sake of sane code completion.
#include "estimate_size_helpers.h"
#endif

namespace NYT {

////////////////////////////////////////////////////////////////////////////////

template <typename I>
    requires std::integral<I>
size_t EstimateSize(const I& /*value*/)
{
    return EstimatedValueSize;
}

template <typename F>
    requires std::floating_point<F>
size_t EstimateSize(const F& /*value*/)
{
    return EstimatedValueSize * 2;
}

template <typename T>
size_t EstimateSize(const std::optional<T>& v)
{
    return v ? EstimateSize(*v) : 0;
}

template <typename C>
    requires std::ranges::range<C>
size_t EstimateSize(const C& value)
{
    size_t result = EstimatedValueSize;
    for (const auto& elem : value) {
        result += EstimateSize(elem);
    }
    return result;
}

template <typename E>
    requires TEnumTraits<E>::IsEnum
size_t EstimateSize(E /*value*/)
{
    return EstimatedValueSize;
}

////////////////////////////////////////////////////////////////////////////////

template <typename... Ts>
size_t EstimateSizes(Ts&&...values)
{
    return (EstimateSize(std::forward<Ts>(values)) + ... + 0);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT
