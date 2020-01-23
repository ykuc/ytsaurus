#pragma once

#include <mapreduce/yt/interface/logging/logger.h>
#include <mapreduce/yt/interface/client.h>

#include <mapreduce/yt/common/config.h>

#include <util/generic/bt_exception.h>

#include <util/datetime/base.h>

////////////////////////////////////////////////////////////////////////////////

template<>
void Out<NYT::TNode>(IOutputStream& s, const NYT::TNode& node);

template<>
void Out<TGUID>(IOutputStream& s, const TGUID& guid);

////////////////////////////////////////////////////////////////////////////////

namespace NYT {
namespace NTesting {

////////////////////////////////////////////////////////////////////////////////

IClientPtr CreateTestClient(TString proxy = "", const TCreateClientOptions& options = {});

// Create map node by unique path in Cypress and return that path.
TYPath CreateTestDirectory(const IClientBasePtr& client);

TString GenerateRandomData(size_t size, ui64 seed = 42);

TVector<TNode> ReadTable(const IClientBasePtr& client, const TString& tablePath);

////////////////////////////////////////////////////////////////////////////////

// TODO: should be removed, usages should be replaced with TConfigSaverGuard
class TZeroWaitLockPollIntervalGuard
{
public:
    TZeroWaitLockPollIntervalGuard();

    ~TZeroWaitLockPollIntervalGuard();

private:
    TDuration OldWaitLockPollInterval_;
};

////////////////////////////////////////////////////////////////////////////////

class TConfigSaverGuard
{
public:
    TConfigSaverGuard();
    ~TConfigSaverGuard();

private:
    TConfig Config_;
};

////////////////////////////////////////////////////////////////////////////////

class TDebugMetricDiff
{
public:
    TDebugMetricDiff(TString name);
    ui64 GetTotal() const;

private:
    TString Name_;
    ui64 InitialValue_;
};

////////////////////////////////////////////////////////////////////////////////

struct TOwningYaMRRow
{
    TString Key;
    TString SubKey;
    TString Value;

    TOwningYaMRRow(const TYaMRRow& row);
    TOwningYaMRRow(TString key, TString subKey, TString value);
};

bool operator == (const TOwningYaMRRow& row1, const TOwningYaMRRow& row2);

////////////////////////////////////////////////////////////////////////////////

class TTestFixture
{
public:
    explicit TTestFixture(const TCreateClientOptions& options = {});
    ~TTestFixture();

    IClientPtr GetClient() const;
    TYPath GetWorkingDir() const;

private:
    TConfigSaverGuard ConfigGuard_;
    IClientPtr Client_;
    TYPath WorkingDir_;
};

////////////////////////////////////////////////////////////////////////////////

class TTabletFixture
    : public TTestFixture
{
public:
    TTabletFixture();

private:
    void WaitForTabletCell();
};

////////////////////////////////////////////////////////////////////////////////

// Compares only columns and only "name" and "type" fields of columns.
bool AreSchemasEqual(const TTableSchema& lhs, const TTableSchema& rhs);

class TWaitFailedException
    : public TWithBackTrace<yexception>
{ };

void WaitForPredicate(const std::function<bool()>& predicate, TDuration timeout = TDuration::Seconds(30));

////////////////////////////////////////////////////////////////////////////////

// Redirects all the LOG_* calls with the corresponding level to `stream`.
// Moreover, the LOG_* calls are delegated to `oldLogger`.
class TStreamTeeLogger
    : public ILogger
{
public:
    TStreamTeeLogger(ELevel cutLevel, IOutputStream* stream, ILoggerPtr oldLogger);
    void Log(ELevel level, const TSourceLocation& sourceLocation, const char* format, va_list args) override;

private:
    ILoggerPtr OldLogger_;
    IOutputStream* Stream_;
    ELevel Level_;
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NTesting
} // namespace NYT

////////////////////////////////////////////////////////////////////////////////

template <>
void Out<NYT::NTesting::TOwningYaMRRow>(IOutputStream& out, const NYT::NTesting::TOwningYaMRRow& row);

////////////////////////////////////////////////////////////////////////////////
