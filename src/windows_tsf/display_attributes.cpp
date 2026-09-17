#include "server.h"
#include <algorithm>
#include <new>

namespace ime::windows {
namespace {
class Attribute final : public ITfDisplayAttributeInfo {
public:
    explicit Attribute(bool selected) : selected_(selected) { ++live_objects; Reset(); }
    ~Attribute() { --live_objects; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (iid != IID_IUnknown && iid != IID_ITfDisplayAttributeInfo) return E_NOINTERFACE;
        *out = static_cast<ITfDisplayAttributeInfo*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override { const auto n = --refs_; if (!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE GetGUID(GUID* guid) override { if (!guid) return E_POINTER; *guid = selected_ ? kSelectedAttribute : kInputAttribute; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetDescription(BSTR* text) override {
        if (!text) return E_POINTER;
        *text = SysAllocString(selected_ ? L"Active converted Bopomofo segment" : L"Bopomofo composition");
        return *text ? S_OK : E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE GetAttributeInfo(TF_DISPLAYATTRIBUTE* attribute) override { if (!attribute) return E_POINTER; *attribute = value_; return S_OK; }
    HRESULT STDMETHODCALLTYPE SetAttributeInfo(const TF_DISPLAYATTRIBUTE* attribute) override { if (!attribute) return E_POINTER; value_ = *attribute; return S_OK; }
    HRESULT STDMETHODCALLTYPE Reset() override {
        value_ = {};
        value_.lsStyle = selected_ ? TF_LS_SOLID : TF_LS_DOT;
        value_.fBoldLine = selected_;
        value_.bAttr = selected_ ? TF_ATTR_TARGET_CONVERTED : TF_ATTR_INPUT;
        if (selected_) {
            value_.crText.type = TF_CT_SYSCOLOR; value_.crText.nIndex = COLOR_HIGHLIGHTTEXT;
            value_.crBk.type = TF_CT_SYSCOLOR; value_.crBk.nIndex = COLOR_HIGHLIGHT;
        }
        return S_OK;
    }
private:
    std::atomic<ULONG> refs_{1};
    bool selected_{};
    TF_DISPLAYATTRIBUTE value_{};
};
class AttributeEnumerator final : public IEnumTfDisplayAttributeInfo {
public:
    explicit AttributeEnumerator(ULONG index = 0) : index_(index) { ++live_objects; }
    ~AttributeEnumerator() { --live_objects; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (iid != IID_IUnknown && iid != IID_IEnumTfDisplayAttributeInfo) return E_NOINTERFACE;
        *out = static_cast<IEnumTfDisplayAttributeInfo*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override { const auto n = --refs_; if (!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE Clone(IEnumTfDisplayAttributeInfo** out) override {
        if (!out) return E_POINTER;
        *out = new(std::nothrow) AttributeEnumerator(index_); return *out ? S_OK : E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE Next(ULONG count, ITfDisplayAttributeInfo** out, ULONG* fetched) override {
        if (!out || (!fetched && count != 1)) return E_POINTER;
        ULONG n{};
        while (n < count && index_ < 2) {
            out[n] = new(std::nothrow) Attribute(index_ == 1);
            if (!out[n]) { if (fetched) *fetched = n; return E_OUTOFMEMORY; }
            ++n; ++index_;
        }
        if (fetched) *fetched = n;
        return n == count ? S_OK : S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE Reset() override { index_ = 0; return S_OK; }
    HRESULT STDMETHODCALLTYPE Skip(ULONG count) override {
        const ULONG skip = std::min(count, 2UL - index_); index_ += skip; return skip == count ? S_OK : S_FALSE;
    }
private:
    std::atomic<ULONG> refs_{1};
    ULONG index_{};
};
}
HRESULT create_attribute(REFGUID guid, ITfDisplayAttributeInfo** result) noexcept {
    if (!result) return E_POINTER;
    *result = nullptr;
    if (guid != kInputAttribute && guid != kSelectedAttribute) return E_INVALIDARG;
    *result = new(std::nothrow) Attribute(guid == kSelectedAttribute);
    return *result ? S_OK : E_OUTOFMEMORY;
}
HRESULT enumerate_attributes(IEnumTfDisplayAttributeInfo** result) noexcept {
    if (!result) return E_POINTER;
    *result = new(std::nothrow) AttributeEnumerator;
    return *result ? S_OK : E_OUTOFMEMORY;
}
}
