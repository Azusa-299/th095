#pragma once
// Fixed input script consumed by the dynamic baseline.
//
// The file is a plain text list of `count tokens` lines so the same recording
// can be replayed by the port and by an original-side capture tool. Token
// names match platform::InputState fields; `none` or `-` clears every key.

#include <string>
#include <vector>
#include "../platform/input.hpp"

namespace th95::sim {

bool loadInputScript(const std::string &path,
                     std::vector<th95::platform::InputState> &frames,
                     std::string &error);

} // namespace th95::sim
