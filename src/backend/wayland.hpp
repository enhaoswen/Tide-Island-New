#pragma once

#include <array>
#include <cstdint>

// ============================================================================
// Tide Island Wayland API
// ============================================================================
//
// The Wayland backend owns the layer-shell surface and EGL context used by the
// renderer.

namespace Wayland {

void init(std::uint16_t width, std::uint16_t height, int32_t exclusive_zone, int32_t margin_top);
void request_resize(std::uint16_t width, std::uint16_t height);
std::array<std::uint32_t, 2> get_surface_size();
std::array<std::uint32_t, 2> get_buffer_size();
float get_scale();
void swap_buffer();
void apply_config(std::uint16_t width, std::uint16_t height, int32_t exclusive_zone, int32_t margin_top);
void set_report_click(void (*callback)(float x, float y, bool left));
bool get_wayland_redraw();
int get_wayland_fd();
void shutdown();
short prepare_events();
void handle_events(short revents);
void cancel_events();

} // namespace Wayland
