#pragma once
#include "common.h"
#include <atomic>

namespace ime::windows {
extern HINSTANCE module_instance;
extern std::atomic<long> live_objects;
HRESULT create_text_service(REFIID iid, void** result) noexcept;
HRESULT create_attribute(REFGUID guid, ITfDisplayAttributeInfo** result) noexcept;
HRESULT enumerate_attributes(IEnumTfDisplayAttributeInfo** result) noexcept;
}
