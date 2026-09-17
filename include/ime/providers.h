#pragma once
#include "ime/types.h"
namespace ime::providers {
// Local deterministic providers. No network or clipboard access; callers own values.
std::vector<Candidate> query(const InputContext& context);
std::vector<Candidate> smart_paste(std::u32string_view original);
}
