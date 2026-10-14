#pragma once

namespace EglVendor {

// Call at startup, before creating threads or using EGL in this process.
// Select a single ICD only when it reproduces the default Wayland renderer.
void select_for_wayland();

} // namespace EglVendor
