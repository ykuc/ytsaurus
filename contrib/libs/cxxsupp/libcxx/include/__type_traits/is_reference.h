//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___TYPE_TRAITS_IS_REFERENCE_H
#define _LIBCPP___TYPE_TRAITS_IS_REFERENCE_H

#include <__config>
#include <__type_traits/integral_constant.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if __has_keyword(__is_lvalue_reference) && \
    __has_keyword(__is_rvalue_reference) && \
    __has_keyword(__is_reference) && !defined(__CUDACC__)

template<class _Tp>
struct _LIBCPP_TEMPLATE_VIS is_lvalue_reference : _BoolConstant<__is_lvalue_reference(_Tp)> { };

template<class _Tp>
struct _LIBCPP_TEMPLATE_VIS is_rvalue_reference : _BoolConstant<__is_rvalue_reference(_Tp)> { };

template<class _Tp>
struct _LIBCPP_TEMPLATE_VIS is_reference : _BoolConstant<__is_reference(_Tp)> { };

#if _LIBCPP_STD_VER > 14
template <class _Tp>
inline constexpr bool is_reference_v = __is_reference(_Tp);
template <class _Tp>
inline constexpr bool is_lvalue_reference_v = __is_lvalue_reference(_Tp);
template <class _Tp>
inline constexpr bool is_rvalue_reference_v = __is_rvalue_reference(_Tp);
#endif

#else // __has_keyword(__is_lvalue_reference) && etc...

template <class _Tp> struct _LIBCPP_TEMPLATE_VIS is_lvalue_reference       : public false_type {};
template <class _Tp> struct _LIBCPP_TEMPLATE_VIS is_lvalue_reference<_Tp&> : public true_type {};

template <class _Tp> struct _LIBCPP_TEMPLATE_VIS is_rvalue_reference        : public false_type {};
template <class _Tp> struct _LIBCPP_TEMPLATE_VIS is_rvalue_reference<_Tp&&> : public true_type {};

template <class _Tp> struct _LIBCPP_TEMPLATE_VIS is_reference        : public false_type {};
template <class _Tp> struct _LIBCPP_TEMPLATE_VIS is_reference<_Tp&>  : public true_type {};
template <class _Tp> struct _LIBCPP_TEMPLATE_VIS is_reference<_Tp&&> : public true_type {};

#if _LIBCPP_STD_VER > 14
template <class _Tp>
inline constexpr bool is_reference_v = is_reference<_Tp>::value;

template <class _Tp>
inline constexpr bool is_lvalue_reference_v = is_lvalue_reference<_Tp>::value;

template <class _Tp>
inline constexpr bool is_rvalue_reference_v = is_rvalue_reference<_Tp>::value;
#endif

#endif // __has_keyword(__is_lvalue_reference) && etc...

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___TYPE_TRAITS_IS_REFERENCE_H
