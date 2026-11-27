// ============================================================================
// Tide Island Wayland backend
// ============================================================================
//
// This translation unit owns the native Wayland, layer-shell, EGL window, and
// swapchain-facing platform state used by the renderer.

#include "backend/wayland.hpp"
#include "utils/log.hpp"

#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include "fractional-scale-v1-client-protocol.h"
#include "viewporter-client-protocol.h"
#include "ext-background-effect-v1-client-protocol.h"
#include <GLES3/gl3.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <wayland-client-core.h>
#include <wayland-client-protocol.h>
#include <linux/input-event-codes.h>
#include <wayland-client.h>
#include <wayland-egl-core.h>
#include <wayland-egl.h>
#include <sys/timerfd.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <cstring>
#include <array>
#include <chrono>
#include <poll.h>
#include <memory>
#include <string_view>
#include <vector>

using namespace std;

// ============================================================================
// [Internal Details]
// ============================================================================

namespace {

template <auto delete_func>
struct DeleteWayland {
    void operator()(auto* ptr) const noexcept {
        if (ptr) {
            delete_func(ptr);
        }
    }
};

unique_ptr<wl_display, DeleteWayland<wl_display_disconnect> >                                      display{nullptr};
unique_ptr<wl_registry, DeleteWayland<wl_registry_destroy> >                                       registry{nullptr};
unique_ptr<wl_compositor, DeleteWayland<wl_compositor_destroy> >                                   compositor{nullptr};
unique_ptr<zwlr_layer_shell_v1, DeleteWayland<zwlr_layer_shell_v1_destroy> >                       layer_shell{nullptr};
unique_ptr<wp_fractional_scale_manager_v1, DeleteWayland<wp_fractional_scale_manager_v1_destroy> > fractional_scale_manager{nullptr};
unique_ptr<wp_viewporter, DeleteWayland<wp_viewporter_destroy> >                                   viewporter{nullptr};
unique_ptr<wl_surface, DeleteWayland<wl_surface_destroy> >                                         surface{nullptr};
unique_ptr<wp_fractional_scale_v1, DeleteWayland<wp_fractional_scale_v1_destroy> >                 fractional_scale{nullptr};
unique_ptr<wp_viewport, DeleteWayland<wp_viewport_destroy> >                                       viewport{nullptr};
unique_ptr<zwlr_layer_surface_v1, DeleteWayland<zwlr_layer_surface_v1_destroy> >                   layer_surface{nullptr};
unique_ptr<wl_egl_window, DeleteWayland<wl_egl_window_destroy> >                                   egl_window{nullptr};
unique_ptr<wl_seat, DeleteWayland<wl_seat_release> >                                               seat{nullptr};
unique_ptr<wl_pointer, DeleteWayland<wl_pointer_release> >                                         pointer{nullptr};
unique_ptr<ext_background_effect_manager_v1, DeleteWayland<ext_background_effect_manager_v1_destroy> > effect_manager{nullptr};
unique_ptr<ext_background_effect_surface_v1, DeleteWayland<ext_background_effect_surface_v1_destroy> > background_effect{nullptr};

EGLDisplay egl_display{EGL_NO_DISPLAY};
EGLConfig  egl_config{};
EGLContext egl_context{EGL_NO_CONTEXT};
EGLSurface egl_surface{EGL_NO_SURFACE};

uint32_t current_width{};
uint32_t current_height{};
uint32_t requested_width{};
uint32_t requested_height{};
uint32_t asked_width{};
uint32_t asked_height{};
uint32_t buffer_width{};
uint32_t buffer_height{};
bool     surface_configured{false};
float    surface_scale{1.0f};

float pointer_x{};
float pointer_y{};
bool  pointer_inside{};
bool  pointer_motion_pending{};
bool  pointer_has_frames{}; // wl_pointer >= v5 groups events with wl_pointer.frame
bool  wayland_needs_redraw{false};

wl_callback*                     frame_callback{};
std::chrono::steady_clock::time_point frame_requested{};

void frame_done(void*, wl_callback* callback, uint32_t) {
    wl_callback_destroy(callback);
    if (callback == frame_callback)
        frame_callback = nullptr;
}

constexpr wl_callback_listener frame_listener{.done = frame_done};

int wayland_fd{-1};

Wayland::PointerCallbacks pointer_callbacks{};

struct BlurShape {
    float x{}, y{}, width{}, height{}, radius{};
};

bool      blur_capable{false};
bool      blur_enabled{false};
bool      blur_warned{false};
BlurShape blur_shape{};
vector<array<int32_t, 4> > blur_sent{};
vector<array<int32_t, 4> > blur_scratch{};

void effect_capabilities(void*, ext_background_effect_manager_v1*, uint32_t flags) {
    const bool capable{(flags & EXT_BACKGROUND_EFFECT_MANAGER_V1_CAPABILITY_BLUR) != 0};
    if (capable != blur_capable) {
        blur_capable = capable;
        blur_sent.clear();
        wayland_needs_redraw = true;
    }
    Log::logger(Log::Debug, "Background effect capabilities: 0x{:x}", flags);
}

constexpr ext_background_effect_manager_v1_listener effect_manager_listener{
    .capabilities = effect_capabilities,
};

void build_blur_rects(const BlurShape& shape, vector<array<int32_t, 4> >& out) {
    out.clear();
    if (!(shape.width > 0.f) || !(shape.height > 0.f))
        return;
    const float r{clamp(shape.radius, 0.f, min(shape.width, shape.height) * 0.5f)};
    const float top_center{shape.y + r};
    const float bottom_center{shape.y + shape.height - r};
    const int   first_row{static_cast<int>(ceil(shape.y))};
    const int   last_row{static_cast<int>(floor(shape.y + shape.height))};

    for (int row = first_row; row < last_row; ++row) {
        // Distance from the corner circle center to the row edge farther from it, so each strip stays inside the curve.
        float d{0.f};
        if (static_cast<float>(row) < top_center)
            d = top_center - static_cast<float>(row);
        else if (static_cast<float>(row + 1) > bottom_center)
            d = static_cast<float>(row + 1) - bottom_center;
        const float inset{d > 0.f ? r - sqrt(max(0.f, r * r - d * d)) : 0.f};
        const int   left{static_cast<int>(ceil(shape.x + inset))};
        const int   right{static_cast<int>(floor(shape.x + shape.width - inset))};
        if (right <= left)
            continue;

        if (!out.empty()) {
            auto& last = out.back();
            if (last[0] == left && last[2] == right - left && last[1] + last[3] == row) {
                ++last[3];
                continue;
            }
        }
        out.push_back({left, row, right - left, 1});
    }
}

void flush_blur() {
    if (!background_effect || !blur_capable)
        return;

    if (blur_enabled)
        build_blur_rects(blur_shape, blur_scratch);
    else
        blur_scratch.clear();

    if (blur_scratch == blur_sent)
        return;

    if (blur_scratch.empty()) {
        ext_background_effect_surface_v1_set_blur_region(background_effect.get(), nullptr);
    }
    else {
        wl_region* region{wl_compositor_create_region(compositor.get())};
        if (!region) {
            Log::logger(Log::Error, "Failed to create blur region");
            return;
        }
        for (const auto& [x, y, w, h] : blur_scratch)
            wl_region_add(region, x, y, w, h);
        ext_background_effect_surface_v1_set_blur_region(background_effect.get(), region);
        wl_region_destroy(region);
    }
    blur_sent.swap(blur_scratch);
}

// --- Pointer Listeners ---

void flush_pointer_motion() {
    if (exchange(pointer_motion_pending, false) && pointer_callbacks.motion) {
        pointer_callbacks.motion(pointer_x, pointer_y);
    }
}

void queue_pointer_motion(wl_fixed_t surface_x, wl_fixed_t surface_y) {
    pointer_x              = static_cast<float>(wl_fixed_to_double(surface_x));
    pointer_y              = static_cast<float>(wl_fixed_to_double(surface_y));
    pointer_motion_pending = true;
    if (!pointer_has_frames) {
        flush_pointer_motion();
    }
}

void release_pointer_focus() {
    if (!exchange(pointer_inside, false)) {
        return;
    }
    pointer_motion_pending = false;
    if (pointer_callbacks.leave) {
        pointer_callbacks.leave();
    }
}

Wayland::PointerButton to_pointer_button(uint32_t button) {
    switch (button) {
    case BTN_LEFT:
        return Wayland::PointerButton::Left;
    case BTN_RIGHT:
        return Wayland::PointerButton::Right;
    case BTN_MIDDLE:
        return Wayland::PointerButton::Middle;
    default:
        return Wayland::PointerButton::Other;
    }
}

void pointer_enter(
    void*,
    wl_pointer*,
    uint32_t,
    wl_surface* entered_surface,
    wl_fixed_t  surface_x,
    wl_fixed_t  surface_y
    ) {
    if (entered_surface != surface.get()) {
        return;
    }

    pointer_inside = true;
    queue_pointer_motion(surface_x, surface_y);
}

void pointer_leave(
    void*,
    wl_pointer*,
    uint32_t,
    wl_surface* left_surface
    ) {
    if (left_surface == surface.get()) {
        release_pointer_focus();
    }
}

void pointer_motion(
    void*,
    wl_pointer*,
    uint32_t,
    wl_fixed_t surface_x,
    wl_fixed_t surface_y
    ) {
    if (pointer_inside) {
        queue_pointer_motion(surface_x, surface_y);
    }
}

void pointer_button(
    void*,
    wl_pointer*,
    uint32_t,
    uint32_t,
    uint32_t button,
    uint32_t state
    ) {
    if (!pointer_inside) {
        return;
    }

    // The press must be hit-tested at the position it happened at.
    flush_pointer_motion();

    if (pointer_callbacks.button) {
        pointer_callbacks.button(to_pointer_button(button), state == WL_POINTER_BUTTON_STATE_PRESSED);
    }
}

void pointer_axis(
    void*,
    wl_pointer*,
    uint32_t,
    uint32_t,
    wl_fixed_t) {
}

void pointer_frame(void*, wl_pointer*) {
    flush_pointer_motion();
}

void pointer_axis_source(void*, wl_pointer*, uint32_t) {
}

void pointer_axis_stop(void*, wl_pointer*, uint32_t, uint32_t) {
}

void pointer_axis_discrete(void*, wl_pointer*, uint32_t, int32_t) {
}

constexpr wl_pointer_listener pointer_listener{
    .enter                   = pointer_enter,
    .leave                   = pointer_leave,
    .motion                  = pointer_motion,
    .button                  = pointer_button,
    .axis                    = pointer_axis,
    .frame                   = pointer_frame,
    .axis_source             = pointer_axis_source,
    .axis_stop               = pointer_axis_stop,
    .axis_discrete           = pointer_axis_discrete,
    .axis_value120           = nullptr,
    .axis_relative_direction = nullptr,
#ifdef WL_POINTER_WARP_SINCE_VERSION
    .warp = nullptr,
#endif
};


void seat_capabilities(
    void*,
    wl_seat* current_seat,
    uint32_t capabilities
    ) {
    const bool has_pointer{(capabilities & WL_SEAT_CAPABILITY_POINTER) != 0};

    if (has_pointer && !pointer) {
        pointer.reset(wl_seat_get_pointer(current_seat));

        if (!pointer) {
            Log::fatal("Failed to create Wayland pointer");
        }

        if (wl_pointer_add_listener(
            pointer.get(),
            &pointer_listener,
            nullptr) == -1) {
            Log::fatal("Failed to add Wayland pointer listener");
        }
        pointer_has_frames = wl_pointer_get_version(pointer.get()) >= WL_POINTER_FRAME_SINCE_VERSION;
    }
    else if (!has_pointer && pointer) {
        release_pointer_focus();
        pointer.reset();
    }
}

void seat_name(
    void*,
    wl_seat*,
    const char*
    ) {
}

constexpr wl_seat_listener seat_listener{
    .capabilities = seat_capabilities,
    .name         = seat_name,
};

void registry_global(
    void*,
    wl_registry* registry,
    uint32_t     name,
    const char*  interface,
    uint32_t     version
    ) {
    const string_view interface_name{interface};

    if (interface_name == wl_compositor_interface.name) {
        compositor.reset(static_cast<wl_compositor*>(
            wl_registry_bind(
                registry,
                name,
                &wl_compositor_interface,
                min(version, 4u)
                )
        ));
    }
    else if (interface_name == zwlr_layer_shell_v1_interface.name) {
        layer_shell.reset(static_cast<zwlr_layer_shell_v1*>(
            wl_registry_bind(
                registry,
                name,
                &zwlr_layer_shell_v1_interface,
                min(version, 4u)
                )
        ));
    }

    else if (interface_name == wp_fractional_scale_manager_v1_interface.name) {
        fractional_scale_manager.reset(static_cast<wp_fractional_scale_manager_v1*>(
            wl_registry_bind(
                registry,
                name,
                &wp_fractional_scale_manager_v1_interface,
                min(version, 1u)
                )
        ));

        if (!fractional_scale_manager) {
            Log::fatal("Failed to bind fractional scale manager");
        }
        Log::logger(Log::Debug, "Fractional scale manager bound successfully");
    }
    else if (interface_name == wp_viewporter_interface.name) {
        viewporter.reset(static_cast<wp_viewporter*>(
            wl_registry_bind(
                registry,
                name,
                &wp_viewporter_interface,
                min(version, 1u)
                )
        ));

        if (!viewporter) {
            Log::fatal("Failed to bind viewporter");
        }
        Log::logger(Log::Debug, "Viewporter bound successfully");
    }
    else if (interface_name == ext_background_effect_manager_v1_interface.name) {
        effect_manager.reset(static_cast<ext_background_effect_manager_v1*>(
            wl_registry_bind(
                registry,
                name,
                &ext_background_effect_manager_v1_interface,
                min(version, 1u)
                )
        ));

        if (!effect_manager) {
            Log::logger(Log::Error, "Failed to bind background effect manager, blur disabled");
            return;
        }
        if (ext_background_effect_manager_v1_add_listener(
            effect_manager.get(),
            &effect_manager_listener,
            nullptr) == -1) {
            Log::logger(Log::Error, "Failed to add background effect manager listener, blur disabled");
            effect_manager.reset();
            return;
        }
        Log::logger(Log::Debug, "Background effect manager bound successfully");
    }
    else if (interface_name == wl_seat_interface.name) {
        seat.reset(static_cast<wl_seat*>(
            wl_registry_bind(
                registry,
                name,
                &wl_seat_interface,
                min(version, 7u)
                )
        ));

        if (!seat) {
            Log::fatal("Failed to bind Wayland seat");
        }

        if (wl_seat_add_listener(
            seat.get(),
            &seat_listener,
            nullptr) == -1) {
            Log::fatal("Failed to add Wayland seat listener");
        }
    }
}

void registry_remove(void*, wl_registry*, uint32_t id) {
    logger(Log::Error, "Wayland global resource removed (id: {})", id);
}

constexpr wl_registry_listener registry_listener{
    .global        = registry_global,
    .global_remove = registry_remove,
};

// --- Fractional Scale Listener ---

// Surface geometry changes are staged, not applied directly.
//
// wl_egl_window_resize() only tells the EGL driver which size the *next* buffer
// should have; the driver decides when it actually reallocates (Mesa and
// NVIDIA egl-wayland2 both do it on the first draw after the resize). The
// viewport destination and layer size, however, are plain double-buffered
// surface state committed by that very swap. If they get ahead of the driver,
// the compositor scales a stale-size buffer into the new destination and the
// whole island visibly shrinks/stretches for a frame.
//
// So the EGL window is resized right away, and the logical size, viewport and
// layer size are switched together (in Wayland::sync_surface) right before the
// next frame is drawn, so all of them land in the same commit as that buffer.
uint32_t pending_width{};
uint32_t pending_height{};
uint32_t pending_buffer_width{};
uint32_t pending_buffer_height{};
bool     has_pending{false};

void apply_pending() {
    if (!has_pending) {
        return;
    }
    if (layer_surface && (pending_width != current_width || pending_height != current_height)) {
        zwlr_layer_surface_v1_set_size(layer_surface.get(), pending_width, pending_height);
    }
    if (viewport) {
        wl_surface_set_buffer_scale(surface.get(), 1);
        wp_viewport_set_destination(viewport.get(), static_cast<int>(pending_width), static_cast<int>(pending_height));
    }
    current_width        = pending_width;
    current_height       = pending_height;
    buffer_width         = pending_buffer_width;
    buffer_height        = pending_buffer_height;
    has_pending          = false;
    wayland_needs_redraw = true;
    Log::logger(Log::Debug, "Surface scale: {:.3f}", surface_scale);
    Log::logger(Log::Debug, "EGL buffer: {}*{}", buffer_width, buffer_height);
}

void stage_size(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0 ||
        width > static_cast<uint32_t>(numeric_limits<int32_t>::max()) ||
        height > static_cast<uint32_t>(numeric_limits<int32_t>::max())) {
        Log::fatal("Invalid logical surface size: {}*{}", width, height);
    }
    // A viewport maps the pixel buffer back to the logical surface size.
    const float scale{viewport && fractional_scale ? surface_scale : 1.0f};
    const float buf_w{round(static_cast<float>(width) * scale)};
    const float buf_h{round(static_cast<float>(height) * scale)};
    if (buf_w < 1 || buf_h < 1 ||
        buf_w >= static_cast<float>(numeric_limits<int>::max()) ||
        buf_h >= static_cast<float>(numeric_limits<int>::max())) {
        Log::fatal("Invalid EGL buffer size: {}*{}", buf_w, buf_h);
    }
    pending_width         = width;
    pending_height        = height;
    pending_buffer_width  = static_cast<uint32_t>(buf_w);
    pending_buffer_height = static_cast<uint32_t>(buf_h);
    has_pending           = true;

    if (egl_window) {
        wl_egl_window_resize(egl_window.get(), static_cast<int>(pending_buffer_width), static_cast<int>(pending_buffer_height), 0, 0);
    }
    // Nothing renders yet: there is no driver state to wait for.
    if (egl_surface == EGL_NO_SURFACE) {
        apply_pending();
    }
    wayland_needs_redraw = true;
}

// The size the surface is heading to: staged if a change is in flight.
array<uint32_t, 2> target_size() {
    return has_pending ? array{pending_width, pending_height} : array{current_width, current_height};
}

uint32_t fit_extent(uint32_t v, bool even) {
    const float scale{viewport && fractional_scale ? surface_scale : 1.0f};
    for (uint32_t c = v; c < v + 240; ++c) {
        const float physical{static_cast<float>(c) * scale};
        const float rounded{round(physical)};
        if (abs(physical - rounded) < 1e-3f && (!even || static_cast<uint32_t>(rounded) % 2 == 0)) {
            return c;
        }
    }
    return v;
}

void apply_size() {
    const uint32_t width{fit_extent(asked_width, true)};
    const uint32_t height{fit_extent(asked_height, false)};
    if (width == requested_width && height == requested_height) {
        return;
    }
    requested_width  = width;
    requested_height = height;
    stage_size(width, height);
}

void preferred_scale(void*, wp_fractional_scale_v1*, uint32_t scale) {
    surface_scale        = static_cast<float>(scale) / 120.0f;
    wayland_needs_redraw = true;
    if (surface_configured) {
        if (asked_width != 0 && asked_height != 0) {
            // Re-fit to the new scale; force restaging even if the fit is unchanged.
            requested_width = requested_height = 0;
            apply_size();
        }
        else {
            const auto [width, height] = target_size();
            stage_size(width, height);
        }
    }
}

constexpr wp_fractional_scale_v1_listener fractional_scale_listener{
    .preferred_scale = preferred_scale,
};

// --- Layer Surface Listeners ---

void layer_surface_configure(
    void*,
    zwlr_layer_surface_v1* surface,
    uint32_t               serial,
    uint32_t               width,
    uint32_t               height
    ) {
    zwlr_layer_surface_v1_ack_configure(surface, serial);

    if (surface_configured) {
        return;
    }

    current_width      = width != 0 ? width : requested_width;
    current_height     = height != 0 ? height : requested_height;
    surface_configured = true;
    stage_size(current_width, current_height);
}

void layer_surface_closed(void*, zwlr_layer_surface_v1*) {
    Log::fatal("Layer surface was closed by compositor. Exiting...");
}

constexpr zwlr_layer_surface_v1_listener layer_surface_listener{
    .configure = layer_surface_configure,
    .closed    = layer_surface_closed,
};

} // namespace

// ============================================================================
// [Public API Implementation]
// ============================================================================

namespace Wayland {
    void init(std::uint16_t island_width, std::uint16_t island_height, int32_t exclusive_zone, int32_t margin_top) {
        if (island_width == 0 || island_height == 0) {
            Log::fatal("Invalid island size: {}*{}", island_width, island_height);
        }

        // Hyprland Error
        // see more info in Hyprland Discussion #12878
        // we add 1 pixel to avoid the issue
        // Widen before adding so a configured size of 65535 does not wrap to zero.
        const uint32_t width{static_cast<uint32_t>(island_width) + 1};
        const uint32_t height{static_cast<uint32_t>(island_height) + 1};

        requested_width    = width;
        requested_height   = height;
        asked_width        = width;
        asked_height       = height;
        surface_configured = false;

        // 1. Establish Wayland Connection & Registry
        display.reset(wl_display_connect(nullptr));
        if (!display) {
            Log::fatal("Failed to connect Wayland");
        }

        registry.reset(wl_display_get_registry(display.get()));

        if (!registry) {
            Log::fatal("Failed to get registry");
        }

        if (wl_registry_add_listener(
            registry.get(),
            &registry_listener,
            nullptr) == -1) {
            Log::fatal("Failed to add Wayland registry listener");
        }
        if (wl_display_roundtrip(display.get()) == -1) {
            Log::fatal("Wayland roundtrip failed");
        }

        if (wl_display_roundtrip(display.get()) == -1) {
            Log::fatal("Wayland roundtrip failed");
        }

        if (!compositor) {
            Log::fatal("No compositor found");
        }

        if (!layer_shell) {
            Log::fatal("No layer shell found");
        }

        // 2. Setup Wayland Surface & Layer Shell
        surface.reset(wl_compositor_create_surface(compositor.get()));

        if (!surface) {
            Log::fatal("Failed to create surface");
        }

        if (fractional_scale_manager) {
            fractional_scale.reset(wp_fractional_scale_manager_v1_get_fractional_scale(
                fractional_scale_manager.get(), surface.get()
                ));
            if (!fractional_scale) {
                Log::fatal("Failed to create fractional scale object");
            }
            if (wp_fractional_scale_v1_add_listener(
                fractional_scale.get(), &fractional_scale_listener, nullptr) == -1) {
                Log::fatal("Failed to add fractional scale listener");
            }
        }

        if (viewporter) {
            viewport.reset(wp_viewporter_get_viewport(viewporter.get(), surface.get()));
            if (!viewport) {
                Log::fatal("Failed to create viewport");
            }
        }

        if (effect_manager) {
            background_effect.reset(ext_background_effect_manager_v1_get_background_effect(
                effect_manager.get(), surface.get()
                ));
            if (!background_effect) {
                Log::logger(Log::Error, "Failed to create background effect object, blur disabled");
            }
        }

        layer_surface.reset(zwlr_layer_shell_v1_get_layer_surface(
            layer_shell.get(),
            surface.get(),
            nullptr,
            ZWLR_LAYER_SHELL_V1_LAYER_TOP,
            "tide-island"
            ));

        if (!layer_surface) {
            Log::fatal("Failed to create layer surface");
        }

        zwlr_layer_surface_v1_set_anchor(
            layer_surface.get(),
            ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP
            );
        zwlr_layer_surface_v1_set_size(
            layer_surface.get(),
            width,
            height
            );
        zwlr_layer_surface_v1_set_exclusive_zone(layer_surface.get(), exclusive_zone);
        zwlr_layer_surface_v1_set_margin(layer_surface.get(), margin_top, 0, 0, 0);

        if (zwlr_layer_surface_v1_add_listener(
            layer_surface.get(),
            &layer_surface_listener,
            nullptr) == -1) {
            Log::fatal("Failed to add layer surface listener");
        }
        wl_surface_commit(surface.get());

        if (wl_display_roundtrip(display.get()) == -1) {
            Log::fatal("Wayland roundtrip failed");
        }

        // clangd says infinity loop, but actually not.
        while (!surface_configured) {
            if (wl_display_dispatch(display.get()) == -1) {
                Log::fatal("Failed to wait for initial layer surface configure");
            }
        }

        // 3. Setup EGL Window & Display
        egl_window.reset(wl_egl_window_create(
            surface.get(),
            static_cast<int>(buffer_width),
            static_cast<int>(buffer_height)
            ));
        if (!egl_window) {
            Log::fatal("Failed to create wl_egl_window");
        }

        // Name the platform explicitly: legacy eglGetDisplay makes libglvnd guess what
        // the native pointer is, which some vendors (NVIDIA) refuse.
        egl_display = eglGetPlatformDisplay(EGL_PLATFORM_WAYLAND_KHR, display.get(), nullptr);
        if (egl_display == EGL_NO_DISPLAY) {
            Log::logger(Log::Warning, "eglGetPlatformDisplay failed (0x{:x}), falling back to eglGetDisplay", eglGetError());
            egl_display = eglGetDisplay(reinterpret_cast<EGLNativeDisplayType>(display.get()));
        }
        if (egl_display == EGL_NO_DISPLAY) {
            Log::fatal("eglGetDisplay failed (0x{:x})", eglGetError());
        }

        EGLint major{};
        EGLint minor{};
        if (!eglInitialize(egl_display, &major, &minor)) {
            Log::fatal("eglInitialize failed");
        }
        logger(Log::Debug, "Using EGL {}.{}", major, minor);

        if (!eglBindAPI(EGL_OPENGL_ES_API)) {
            Log::fatal("eglBindAPI failed");
        }

        // 4. Configure EGL Surface & Context
        constexpr array<EGLint, 13> attribs = {
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
            EGL_RED_SIZE, 8,
            EGL_GREEN_SIZE, 8,
            EGL_BLUE_SIZE, 8,
            EGL_ALPHA_SIZE, 8,
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
            EGL_NONE,
        };

        EGLint count{};
        if (!eglChooseConfig(egl_display, attribs.data(), &egl_config, 1, &count)
            || count == 0) {
            Log::fatal("eglChooseConfig failed");
        }

        constexpr array<EGLint, 3> ctx_attribs = {
            EGL_CONTEXT_CLIENT_VERSION, 3,
            EGL_NONE
        };

        egl_context = eglCreateContext(
            egl_display,
            egl_config,
            EGL_NO_CONTEXT,
            ctx_attribs.data()
            );
        if (egl_context == EGL_NO_CONTEXT) {
            Log::fatal("eglCreateContext failed");
        }

        egl_surface = eglCreateWindowSurface(
            egl_display,
            egl_config,
            reinterpret_cast<EGLNativeWindowType>(egl_window.get()),
            nullptr
            );

        if (egl_surface == EGL_NO_SURFACE) {
            Log::fatal("eglCreateWindowSurface failed");
        }

        if (!eglMakeCurrent(
            egl_display,
            egl_surface,
            egl_surface,
            egl_context
            )) {
            Log::fatal("eglMakeCurrent failed");
        }

        // Swap interval 0: the main loop paces frames itself (MainLoop::wait), so the
        // driver must not. With interval >= 1 NVIDIA's egl-wayland2 attaches
        // wp_fifo_v1 / wp_commit_timing_v1 to every commit; on Hyprland that lets the
        // layer size / viewport of a resize take effect a frame before the matching
        // buffer, so the island flashes at the wrong scale on every expand/collapse.
        // TIDE_SWAP_INTERVAL overrides it for testing.
        int swap_interval{0};
        if (const char* value = getenv("TIDE_SWAP_INTERVAL"); value && *value != '\0') {
            swap_interval = atoi(value);
        }
        if (!eglSwapInterval(egl_display, swap_interval)) {
            Log::logger(Log::Warning, "eglSwapInterval({}) failed (0x{:x})", swap_interval, eglGetError());
        }

        const auto* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
        Log::logger(Log::Debug, "EGL vendor=\"{}\", GL renderer=\"{}\"",
            eglQueryString(egl_display, EGL_VENDOR), renderer ? renderer : "unknown");
        if (renderer && (string_view(renderer).contains("llvmpipe") ||
            string_view(renderer).contains("softpipe") ||
            string_view(renderer).contains("Software Rasterizer"))) {
            Log::logger(Log::Warning, "Using software rendering; check the GPU driver if CPU or RAM usage is high");
        }

        wayland_fd = wl_display_get_fd(display.get());
    }

    void swap_buffer() {
        flush_blur();
        // Must precede the swap to be part of its commit.
        if (frame_callback)
            wl_callback_destroy(frame_callback);
        frame_callback = wl_surface_frame(surface.get());
        wl_callback_add_listener(frame_callback, &frame_listener, nullptr);
        frame_requested = chrono::steady_clock::now();
        if (!eglSwapBuffers(egl_display, egl_surface)) {
            Log::fatal("eglSwapBuffers failed");
        }
    }

    void request_resize(
        std::uint16_t width,
        std::uint16_t height
        ) {
        if (!layer_surface) {
            Log::fatal("request_resize called before layer_surface creation");
        }

        if (width == 0 || height == 0) {
            Log::fatal("Island width and height must be positive");
        }

        if (!egl_window) {
            Log::fatal("EGL_window is not initialized");
        }

        // Hyprland Error
        // see more info in Hyprland Discussion #12878
        // we add 1 pixel to avoid the issue
        // Widen before adding so a configured size of 65535 does not wrap to zero.

        // plus another 1 for the sake of safety
        asked_width  = static_cast<uint32_t>(width) + 1;
        asked_height = static_cast<uint32_t>(height) + 1;
        apply_size();

    }

    array<uint32_t, 2> get_surface_size() {
        return {current_width, current_height};
    }

    array<uint32_t, 2> get_buffer_size() {
        return {buffer_width, buffer_height};
    }

    float get_scale() {
        return viewport && fractional_scale ? surface_scale : 1.0f;
    }

    void apply_config(
        std::uint16_t width,
        std::uint16_t height,
        int32_t       exclusive_zone,
        int32_t       margin_top
        ) {
        if (width == 0 || height == 0) {
            Log::fatal("Island width and height must be positive");
        }
        requested_width  = width;
        requested_height = height;

        zwlr_layer_surface_v1_set_exclusive_zone(
            layer_surface.get(),
            exclusive_zone
            );

        zwlr_layer_surface_v1_set_margin(
            layer_surface.get(),
            margin_top,
            0, 0, 0
            );

        // The layer size is set together with the buffer once the driver catches up.
        stage_size(width, height);

        logger(Log::Debug, "Applyed config: {}*{}", width, height);
    }

    void set_blur(bool enabled) {
        if (enabled && !blur_warned && (!background_effect || !blur_capable)) {
            blur_warned = true;
            Log::logger(Log::Warning,
                "Compositor does not support blur via ext-background-effect-v1; background blur is disabled");
        }
        blur_enabled = enabled;
    }

    void set_blur_shape(float x, float y, float width, float height, float radius) {
        blur_shape = {x, y, width, height, radius};
    }

    void set_pointer_callbacks(const PointerCallbacks& callbacks) {
        pointer_callbacks = callbacks;
    }

    void handle_events(short revents) {
        if (revents & (POLLERR | POLLHUP | POLLNVAL)) {
            wl_display_cancel_read(display.get());
            Log::fatal(
                "Wayland fd poll error: {}",
                revents
                );
        }

        if (revents & POLLIN) {
            if (wl_display_read_events(display.get()) == -1) {
                Log::fatal(
                    "wl_display_read_events failed: {}",
                    strerror(errno)
                    );
            }
        }
        else {
            wl_display_cancel_read(display.get());
        }

        if (revents & POLLOUT) {
            if (wl_display_flush(display.get()) == -1 &&
                errno != EAGAIN) {
                Log::fatal(
                    "wl_display_flush failed: {}",
                    strerror(errno)
                    );
            }
        }

        if (wl_display_dispatch_pending(display.get()) == -1) {
            Log::fatal("wl_display_dispatch_pending failed");
        }
    }

    short prepare_events() {
        while (wl_display_prepare_read(display.get()) == -1) {
            if (wl_display_dispatch_pending(display.get()) == -1) {
                Log::fatal("wl_display_dispatch_pending failed");
            }
        }
        short events = POLLIN;

        if (wl_display_flush(display.get()) == -1) {
            if (errno == EAGAIN) {
                events |= POLLOUT;
            }
            else {
                wl_display_cancel_read(display.get());
                Log::fatal(
                    "wl_display_flush failed: {}",
                    strerror(errno)
                    );
            }
        }

        return events;
    }

    int get_wayland_fd() {
        return wayland_fd;
    }

    void cancel_events() {
        wl_display_cancel_read(display.get());
    }

    void shutdown() {
        if (!display) {
            return;
        }

        if (frame_callback) {
            wl_callback_destroy(frame_callback);
            frame_callback = nullptr;
        }

        // 1. Terminate EGL Environment
        if (egl_display != EGL_NO_DISPLAY) {
            eglMakeCurrent(
                egl_display,
                EGL_NO_SURFACE,
                EGL_NO_SURFACE,
                EGL_NO_CONTEXT
                );

            if (egl_surface != EGL_NO_SURFACE) {
                eglDestroySurface(egl_display, egl_surface);
                egl_surface = EGL_NO_SURFACE;
            }

            if (egl_context != EGL_NO_CONTEXT) {
                eglDestroyContext(egl_display, egl_context);
                egl_context = EGL_NO_CONTEXT;
            }

            eglTerminate(egl_display);
            egl_display = EGL_NO_DISPLAY;
            egl_config  = nullptr;
        }
    }

    bool sync_surface() {
        if (!has_pending) {
            return false;
        }
        // Apply in the frame that is about to be drawn: every EGL driver seen so far
        // (Mesa, NVIDIA egl-wayland2) reallocates the color buffer on the first draw
        // after wl_egl_window_resize, so this frame already renders into the new size.
        // Note: eglQuerySurface cannot be used to wait for that, NVIDIA still reports
        // the old size until the first draw.
        apply_pending();
        return true;
    }

    bool resize_pending() {
        return has_pending;
    }

    bool frame_ready() {
        // Hidden surfaces get no frame callbacks; don't freeze animations.
        constexpr auto k_frame_timeout = chrono::milliseconds{250};
        return !frame_callback || chrono::steady_clock::now() - frame_requested > k_frame_timeout;
    }

    bool get_wayland_redraw() {
        return exchange(wayland_needs_redraw, false);
    }

} // namespace Wayland
