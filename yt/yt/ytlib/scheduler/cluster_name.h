#pragma once

#include <library/cpp/yt/misc/strong_typedef.h>

#include <util/str_stl.h>

namespace NYT::NScheduler {

////////////////////////////////////////////////////////////////////////////////

YT_DEFINE_STRONG_TYPEDEF(TClusterName, TString)

const TClusterName LocalClusterName{};

bool IsLocal(const TClusterName& clusterName);
void FormatValue(TStringBuilderBase* builder, const TClusterName& name, TStringBuf spec);

} // namespace NYT::NScheduler

////////////////////////////////////////////////////////////////////////////////

template <>
struct THash<NYT::NScheduler::TClusterName>
{
    size_t operator()(const NYT::NScheduler::TClusterName& name) const
    {
        return THash<TString>()(name.Underlying());
    }
};
