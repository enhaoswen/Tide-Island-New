#pragma once

#include <array>
#include <cstdint>

// The Wayland backend owns the layer-shell surface and EGL context used by the renderer.

namespace Wayland {

/// @brief Connects Wayland, creates the layer-shell surface and EGL context, and configures blur.
void init(uint16_t width, uint16_t height, int32_t exclusive_zone, int32_t margin_top);

/// @brief Requests a new logical surface size.
void request_resize(uint16_t width, uint16_t height);

// Call once per frame right before drawing. Applies a staged resize once the EGL
// driver hands out buffers of the new size.
/// @brief Applies a staged resize for this frame; returns true if the surface size changed.
bool sync_surface();

/// @brief Returns true while a resize is staged but not yet applied.
bool is_resize_pending();

/// @brief Returns the current logical surface size in pixels.
std::array<uint32_t, 2> get_surface_size();

/// @brief Returns the current EGL buffer size in pixels.
std::array<uint32_t, 2> get_buffer_size();

/// @brief Returns the effective output scale factor.
float get_scale();

/// @brief Swaps the EGL buffers, submitting the next frame.
void swap_buffer();

/// @brief Returns true if a frame may be drawn, or a frame callback timed out.
bool is_frame_ready();

/// @brief Applies size, exclusive zone and top margin to the layer surface.
void apply_config(uint16_t width, uint16_t height, int32_t exclusive_zone, int32_t margin_top);

/// @brief Enables or disables background blur, warning once if unsupported.
void set_blur(bool is_enabled);

/// @brief Sets the rectangular blur region in surface coordinates.
void set_blur_shape(float x, float y, float width, float height, float radius);

/// @brief Button reported to pointer callbacks.
enum class PointerButton : uint8_t {
	Left,
	Right,
	Middle,
	Other,
};

// Pointer input in surface coordinates. Motion is coalesced per wl_pointer.frame and always
// flushed before a button, so `button` sees the position it happened at.
/// @brief Callbacks for pointer input; null members are ignored.
struct PointerCallbacks {
	void (*motion_)(float x, float y){};
	void (*button_)(PointerButton button, bool is_pressed){};
	void (*leave_)(){};
};

/// @brief Registers pointer input callbacks.
void set_pointer_callbacks(const PointerCallbacks& callbacks);

/// @brief Returns and clears the pending redraw flag.
bool get_wayland_redraw();

/// @brief Returns the Wayland connection fd to poll.
int get_wayland_fd();

/// @brief Tears down EGL and releases Wayland resources.
void shutdown();

/// @brief Prepares event dispatch; returns poll events to wait for.
short prepare_events();

/// @brief Handles poll results for the Wayland fd.
void handle_events(short revents);

/// @brief Cancels a pending read after poll error.
void cancel_events();

}
