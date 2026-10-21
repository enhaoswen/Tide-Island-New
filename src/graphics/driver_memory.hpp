#pragma once

namespace DriverMemory {

// Best effort, Linux/NVIDIA only. Call once after the first successful swap,
// with the rendering context current and no concurrent shader compilation.
void release_idle_compiler_pages();

} // namespace DriverMemory
