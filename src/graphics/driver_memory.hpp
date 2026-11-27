#pragma once

namespace DriverMemory {

// Manual opt-in, Linux/NVIDIA only; the renderer never calls this automatically.
// Call after shader warmup and a successful swap, with the rendering context
// current and no concurrent shader compilation. Avoid repeated calls per frame.
/// @brief Reclaims idle NVIDIA shader-compiler file pages; opt-in, not called automatically.
void release_idle_compiler_pages();

}
