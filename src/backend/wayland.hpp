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
enum class PointerButton : std::uint8_t { Left, Right, Middle, Other };

// Pointer input in surface coordinates. Motion is coalesced per wl_pointer.frame and always
// flushed before a button, so `button` sees the position it happened at. Null members are ignored.
struct PointerCallbacks {
    void (*motion)(float x, float y){};
    void (*button)(PointerButton button, bool pressed){};
    void (*leave)(){};
};

void set_pointer_callbacks(const PointerCallbacks& callbacks);
bool get_wayland_redraw();
int get_wayland_fd();
void shutdown();
short prepare_events();
void handle_events(short revents);
void cancel_events();

} // namespace Wayland
