#pragma once

#include <cstddef>

namespace dm::ui
{
// Memory currently used by the whole process (working set / resident size). In a plug-in this is the HOST's process,
// which is why the UI shows it next to the memory held by Daft Mug's own samples. Returns 0 when unavailable.
std::size_t getProcessMemoryBytes();
} // namespace dm::ui
