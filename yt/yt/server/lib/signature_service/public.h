#pragma once

#include <yt/yt/core/logging/log.h>

namespace NYT::NSignatureService {

////////////////////////////////////////////////////////////////////////////////

YT_DEFINE_STRONG_TYPEDEF(TKeyId, TGuid)
YT_DEFINE_STRONG_TYPEDEF(TOwnerId, TString)

////////////////////////////////////////////////////////////////////////////////

DECLARE_REFCOUNTED_CLASS(TKeyInfo)
DECLARE_REFCOUNTED_CLASS(TSignature)

////////////////////////////////////////////////////////////////////////////////

class TSignature;
class TSignatureGenerator;
class TSignatureValidator;

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NSignatureService
