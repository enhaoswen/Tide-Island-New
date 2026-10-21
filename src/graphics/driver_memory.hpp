#pragma once

namespace DriverMemory {

// Manual opt-in, Linux/NVIDIA only; the renderer never calls this automatically.
// Call after shader warmup and a successful swap, with the rendering context
// current and no concurrent shader compilation. Avoid repeated calls per frame.
void release_idle_compiler_pages();

} // namespace DriverMemory
