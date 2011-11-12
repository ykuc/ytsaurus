#pragma once

#include <util/stream/str.h>
#include <util/system/atexit.h>

namespace NYT {

////////////////////////////////////////////////////////////////////////////////

typedef yvector<char> TBlob;

class TRef
{
public:
    TRef()
        : Data(NULL)
        , Size_(0)
    { }

    TRef(void* data, size_t size)
        : Data(NULL)
        , Size_(0)
    {
        if (data != NULL && size != 0) {
            Data = reinterpret_cast<char*>(data);
            Size_ = size;
        }
    }

    TRef(const TBlob& blob)
        : Data(NULL)
        , Size_(0)
    {
        if (!blob.empty()) {
            Data = const_cast<char*>(blob.begin());
            Size_ = blob.size();
        }
    }

    const char* Begin() const
    {
        return Data;
    }

    char* Begin()
    {
        return Data;
    }

    const char* End() const
    {
        return Data + Size_;
    }

    char* End()
    {
        return Data + Size_;
    }

    size_t Size() const
    {
        return Size_;
    }

    // Let's hope your compiler supports RVO.
    TBlob ToBlob() const
    {
        return TBlob(Begin(), End());
    }

    bool operator == (const TRef& other) const
    {
        return Data == other.Data && Size_ == other.Size_;
    }

    bool operator != (const TRef& other) const
    {
        return !(*this == other);
    }

    static inline bool CompareContent(const TRef& lhs, const TRef& rhs)
    {
        if (lhs.Size() != rhs.Size())
            return false;
        if (lhs.Size() == 0)
            return true;
        return memcmp(lhs.Begin(), rhs.Begin(), lhs.Size()) == 0;
    }

private:
    char* Data;
    size_t Size_;

};

////////////////////////////////////////////////////////////////////////////////

class TSharedRef
{
public:
    TSharedRef()
    { }

    TSharedRef(TBlob&& blob)
        : Blob(new TBlob())
    {
        Blob->swap(blob);
        Ref = *Blob;
    }

    TSharedRef(const TSharedRef& sharedRef, const TRef& ref)
        : Blob(sharedRef.Blob)
        , Ref(ref)
    { }

    operator TRef() const
    {
        return Ref;
    }

    const char* Begin() const
    {
        return Ref.Begin();
    }

    char* Begin()
    {
        return Ref.Begin();
    }

    const char* operator ~ () const
    {
        return Begin();
    }

    const char* End() const
    {
        return Ref.End();
    }

    char* End()
    {
        return Ref.End();
    }

    size_t Size() const
    {
        return Ref.Size();
    }

    // Let's hope your compiler supports RVO.
    TBlob ToBlob() const
    {
        return Ref.ToBlob();
    }

    bool operator == (const TSharedRef& other) const
    {
        return Blob == other.Blob && Ref == other.Ref;
    }

    bool operator != (const TSharedRef& other) const
    {
        return !(*this == other);
    }

private:
    typedef TSharedPtr<TBlob, TAtomicCounter> TBlobPtr;

    TBlobPtr Blob;
    TRef Ref;

};


////////////////////////////////////////////////////////////////////////////////

} // namespace NYT
