#pragma once

#include <string_view>

#include "graphics/texture.hpp"
#include "utils/color.hpp"
#include "utils/struct.hpp"

namespace Renderer {

/// @brief Initializes the GPU renderer and its shader pipelines.
void init();

// Destroy scene objects before calling shutdown, while EGL is still current.
/// @brief Tears down the renderer; scene objects must be destroyed first while EGL is current.
void shutdown();

/// @brief Begins a render pass for the current frame.
void begin_frame();

/// @brief Ends the render pass, commits, and swaps buffers.
void end_frame();

// Content -> texture. String / path handling lives here (not in the scene
// graph), because font rasterization and image decoding are renderer concerns.
// Text is rasterized at the surface scale and tagged with it so the scene graph
// can recover logical dimensions for layout.
/// @brief Loads an image file at `path` into a texture.
[[nodiscard]] Texture prepare_image(std::string_view path);

// Rasterization scale for text (physical pixels per logical pixel).
/// @brief Returns the rasterization scale for text (physical pixels per logical pixel).
[[nodiscard]] float text_scale();

/// @brief Rasterizes `text` in `font` at the current surface scale into a texture.
[[nodiscard]] Texture prepare_text(std::string_view text, FontHandle font);

/// @brief Rasterizes `text` in `font` at `scale` with subpixel offsets `sub_x`/`sub_y` into a texture.
[[nodiscard]] Texture
prepare_text(std::string_view text, FontHandle font, float scale, float sub_x = 0.f, float sub_y = 0.f);

// Satisfies Object::Renderer (see scene/render.hpp). Stateless: all GL state is
// internal to the renderer translation unit, so a single instance is enough.
/// @brief Stateless draw sink satisfying Object::Renderer; all GL state is internal to the renderer.
struct GpuRenderer {
	/// @brief Draws a solid rectangle with corner `radius` and `opacity`.
	void draw_rect(const Frame& frame, const Color& color, float radius, float opacity);

	/// @brief Draws `texture` over `frame` sampling `uv`, with corner `radius` and `opacity`.
	void draw_image(const Frame& frame, const Texture& texture, const UvRect& uv, float radius, float opacity);

	/// @brief Draws `texture` tinted by `color` with `opacity`.
	void draw_text(const Frame& frame, const Texture& texture, FontHandle font, const Color& color, float opacity);

	/// @brief Draws a ring/arc; `progress` in [0,1], `thickness`, `track` background, `opacity`.
	void draw_ring(const Frame& frame,
		const Color&            color,
		const Color&            track,
		float                   progress,
		float                   thickness,
		float                   opacity);

	/// @brief Pushes a scissor clip intersected with the current one.
	void push_clip(const Frame& frame, float radius);

	/// @brief Restores the previously pushed clip.
	void pop_clip();
};

}
