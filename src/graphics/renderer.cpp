#define SOKOL_IMPL
#define STB_IMAGE_IMPLEMENTATION

#include "graphics/renderer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include <GLES3/gl3.h>
#include <sokol_gfx.h>

#include "backend/wayland.hpp"
#include "graphics/shaders/basic.glsl.h"
#include "graphics/text.hpp"
#include "graphics/texture.hpp"
#include "sokol_log.h"
#include "stb_image.h"
#include "utils/log.hpp"

#ifdef __GLIBC__
#include <malloc.h>
#endif

using namespace std;

namespace {

struct RectVert {
	float p1_x_{}, p1_y_{};
	float p1_r_{}, p1_g_{}, p1_b_{}, p1_a_{};

	float p2_x_{}, p2_y_{};
	float p2_r_{}, p2_g_{}, p2_b_{}, p2_a_{};

	float p3_x_{}, p3_y_{};
	float p3_r_{}, p3_g_{}, p3_b_{}, p3_a_{};

	float p4_x_{}, p4_y_{};
	float p4_r_{}, p4_g_{}, p4_b_{}, p4_a_{};
};

struct ImgVert {
	float p1_x_{}, p1_y_{};
	float p1_u_{}, p1_v_{};

	float p2_x_{}, p2_y_{};
	float p2_u_{}, p2_v_{};

	float p3_x_{}, p3_y_{};
	float p3_u_{}, p3_v_{};

	float p4_x_{}, p4_y_{};
	float p4_u_{}, p4_v_{};
};

sg_shader   rectangle_shader{};
sg_pipeline rectangle_pipeline{};
sg_buffer   rect_vertex_buffer{};

sg_shader   image_shader{};
sg_pipeline image_pipeline{};
sg_buffer   image_vertex_buffer{};

sg_shader   text_shader{};
sg_pipeline text_pipeline{};
sg_shader   ring_shader{};
sg_pipeline ring_pipeline{};

sg_sampler image_sampler{};
sg_sampler text_sampler{};

// Scissor state. current_clip is the rect currently applied, expressed in
// buffer pixels; clip_stack holds the rect to restore on each pop_clip.
array<int, 4>         current_clip{-1, -1, -1, -1};
vector<array<int, 4>> clip_stack{};

RectVert rectangle_vertices(const Frame frame, const array<float, 4> color) {
	const float left{frame.x_};
	const float top{frame.y_};
	const float right{left + frame.width_};
	const float bottom{top + frame.height_};

	// x, y, r, g, b, a

	return {
		left,
		top,
		color[0],
		color[1],
		color[2],
		color[3],
		right,
		top,
		color[0],
		color[1],
		color[2],
		color[3],
		left,
		bottom,
		color[0],
		color[1],
		color[2],
		color[3],
		right,
		bottom,
		color[0],
		color[1],
		color[2],
		color[3],
	};
}

ImgVert image_vertices(const Frame frame, const UvRect& uv = {}) {
	const float left{frame.x_};
	const float top{frame.y_};
	const float right{left + frame.width_};
	const float bottom{top + frame.height_};

	return {
		// x, y, u, v
		left,
		top,
		uv.u0_,
		uv.v0_,
		right,
		top,
		uv.u1_,
		uv.v0_,
		left,
		bottom,
		uv.u0_,
		uv.v1_,
		right,
		bottom,
		uv.u1_,
		uv.v1_,
	};
}

Frame snap_to_pixels(Frame frame) {
	const auto  surface{Wayland::get_surface_size()};
	const auto  buffer{Wayland::get_buffer_size()};
	const float scale_x{static_cast<float>(buffer[0]) / static_cast<float>(surface[0])};
	const float scale_y{static_cast<float>(buffer[1]) / static_cast<float>(surface[1])};
	frame.x_ = round(frame.x_ * scale_x) / scale_x;
	frame.y_ = round(frame.y_ * scale_y) / scale_y;
	return frame;
}

rect_proj_uniform_t projection() {
	const auto surface_size{Wayland::get_surface_size()};

	rect_proj_uniform_t result{};
	result.proj[0]  = 2.f / static_cast<float>(surface_size[0]);
	result.proj[5]  = -2.f / static_cast<float>(surface_size[1]);
	result.proj[10] = 1.f;
	result.proj[12] = -1.f;
	result.proj[13] = 1.f;
	result.proj[15] = 1.f;
	return result;
}

template<typename T>
T radius_uniform(const Frame frame, float radius) {
	T result{};
	result.center[0]    = frame.x_ + frame.width_ / 2.f;
	result.center[1]    = frame.y_ + frame.height_ / 2.f;
	result.half_size[0] = frame.width_ / 2.f;
	result.half_size[1] = frame.height_ / 2.f;
	result.radius       = radius;
	return result;
}

void enable_blending(sg_pipeline_desc& descriptor) {
	auto& blend{descriptor.colors[0].blend};
	blend.enabled          = true;
	blend.src_factor_rgb   = SG_BLENDFACTOR_SRC_ALPHA;
	blend.dst_factor_rgb   = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
	blend.src_factor_alpha = SG_BLENDFACTOR_ONE;
	blend.dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
}

sg_swapchain swapchain() {
	const auto surface_size{Wayland::get_buffer_size()};

	sg_swapchain result{};
	result.width          = static_cast<int>(surface_size[0]);
	result.height         = static_cast<int>(surface_size[1]);
	result.sample_count   = 1;
	result.color_format   = SG_PIXELFORMAT_RGBA8;
	result.depth_format   = SG_PIXELFORMAT_NONE;
	result.gl.framebuffer = 0;
	return result;
}

[[nodiscard]] float clamp_radius(float radius, const Frame& frame) {
	return clamp(radius, 0.f, min(frame.width_, frame.height_) * 0.5f);
}

// Logical frame -> scissor rect in buffer pixels, clamped to the surface.
array<int, 4> frame_to_scissor(const Frame& frame) {
	const auto  surface{Wayland::get_surface_size()};
	const auto  buffer{Wayland::get_buffer_size()};
	const float scale_x{static_cast<float>(buffer[0]) / static_cast<float>(surface[0])};
	const float scale_y{static_cast<float>(buffer[1]) / static_cast<float>(surface[1])};
	const int   x{static_cast<int>(round(clamp(frame.x_, 0.f, static_cast<float>(surface[0])) * scale_x))};
	const int   y{static_cast<int>(round(clamp(frame.y_, 0.f, static_cast<float>(surface[1])) * scale_y))};
	const int   right{
		static_cast<int>(round(clamp(frame.x_ + frame.width_, 0.f, static_cast<float>(surface[0])) * scale_x))};
	const int bottom{
		static_cast<int>(round(clamp(frame.y_ + frame.height_, 0.f, static_cast<float>(surface[1])) * scale_y))};
	return {x, y, max(0, right - x), max(0, bottom - y)};
}

array<int, 4> intersect_scissor(const array<int, 4>& a, const array<int, 4>& b) {
	const int x{max(a[0], b[0])};
	const int y{max(a[1], b[1])};
	const int right{min(a[0] + a[2], b[0] + b[2])};
	const int bottom{min(a[1] + a[3], b[1] + b[3])};
	return {x, y, max(0, right - x), max(0, bottom - y)};
}

void apply_scissor(const array<int, 4>& clip) {
	if(clip == current_clip)
		return;
	sg_apply_scissor_rect(clip[0], clip[1], clip[2], clip[3], true);
	current_clip = clip;
}

}

namespace Renderer {
void init() {
	// sokol environment init

	sg_desc descriptor{};
	descriptor.logger.func                       = slog_func;
	descriptor.environment.defaults.color_format = SG_PIXELFORMAT_RGBA8;
	descriptor.environment.defaults.depth_format = SG_PIXELFORMAT_NONE;
	descriptor.environment.defaults.sample_count = 1;
	sg_setup(&descriptor);
	if(!sg_isvalid()) {
		Log::fatal("Failed to initialize Sokol");
	}

	// rectangle environment init

	rectangle_shader = sg_make_shader(rectangle_shader_desc(sg_query_backend()));

	sg_pipeline_desc rectangle_pipe_desc{};

	rectangle_pipe_desc.shader                                       = rectangle_shader;
	rectangle_pipe_desc.layout.attrs[ATTR_rectangle_position].format = SG_VERTEXFORMAT_FLOAT2;
	rectangle_pipe_desc.layout.attrs[ATTR_rectangle_color].format    = SG_VERTEXFORMAT_FLOAT4;
	rectangle_pipe_desc.primitive_type                               = SG_PRIMITIVETYPE_TRIANGLE_STRIP;
	enable_blending(rectangle_pipe_desc);
	rectangle_pipeline = sg_make_pipeline(&rectangle_pipe_desc);

	sg_buffer_desc rectangle_buffer_desc{};
	rectangle_buffer_desc.size                 = 16 * 1024;
	rectangle_buffer_desc.usage.dynamic_update = true;
	rectangle_buffer_desc.label                = "rect_vertex_buffer";
	rect_vertex_buffer                         = sg_make_buffer(&rectangle_buffer_desc);

	// image environment init

	image_shader = sg_make_shader(image_shader_desc(sg_query_backend()));

	sg_pipeline_desc image_pipe_desc{};

	image_pipe_desc.shader                                   = image_shader;
	image_pipe_desc.layout.attrs[ATTR_image_position].format = SG_VERTEXFORMAT_FLOAT2;
	image_pipe_desc.layout.attrs[ATTR_image_coord].format    = SG_VERTEXFORMAT_FLOAT2;
	image_pipe_desc.primitive_type                           = SG_PRIMITIVETYPE_TRIANGLE_STRIP;
	enable_blending(image_pipe_desc);
	image_pipeline = sg_make_pipeline(image_pipe_desc);

	sg_buffer_desc image_buffer_desc{};
	image_buffer_desc.size                 = 1024 * 1024;
	image_buffer_desc.usage.dynamic_update = true;
	image_buffer_desc.label                = "image_vertex_buffer";
	image_vertex_buffer                    = sg_make_buffer(&image_buffer_desc);

	// text environment init

	text_shader = sg_make_shader(text_shader_desc(sg_query_backend()));

	sg_pipeline_desc text_pipe_desc{};
	text_pipe_desc.shader                                  = text_shader;
	text_pipe_desc.layout.attrs[ATTR_text_position].format = SG_VERTEXFORMAT_FLOAT2;
	text_pipe_desc.layout.attrs[ATTR_text_coord].format    = SG_VERTEXFORMAT_FLOAT2;
	text_pipe_desc.primitive_type                          = SG_PRIMITIVETYPE_TRIANGLE_STRIP;
	enable_blending(text_pipe_desc);
	text_pipeline = sg_make_pipeline(text_pipe_desc);

	ring_shader = sg_make_shader(ring_shader_desc(sg_query_backend()));

	sg_pipeline_desc ring_pipe_desc{};
	ring_pipe_desc.shader                                  = ring_shader;
	ring_pipe_desc.layout.attrs[ATTR_ring_position].format = SG_VERTEXFORMAT_FLOAT2;
	ring_pipe_desc.layout.buffers[0].stride                = 4 * sizeof(float);
	ring_pipe_desc.primitive_type                          = SG_PRIMITIVETYPE_TRIANGLE_STRIP;
	enable_blending(ring_pipe_desc);
	ring_pipeline = sg_make_pipeline(ring_pipe_desc);

	if(sg_query_pipeline_state(rectangle_pipeline) != SG_RESOURCESTATE_VALID ||
		sg_query_pipeline_state(image_pipeline) != SG_RESOURCESTATE_VALID ||
		sg_query_pipeline_state(text_pipeline) != SG_RESOURCESTATE_VALID ||
		sg_query_pipeline_state(ring_pipeline) != SG_RESOURCESTATE_VALID) {
		Log::fatal("Renderer pipeline is invalid (shader compile error?)");
	}

	sg_sampler_desc sampler_desc{};
	sampler_desc.min_filter = SG_FILTER_LINEAR;
	sampler_desc.mag_filter = SG_FILTER_LINEAR;
	sampler_desc.wrap_u     = SG_WRAP_CLAMP_TO_EDGE;
	sampler_desc.wrap_v     = SG_WRAP_CLAMP_TO_EDGE;
	image_sampler           = sg_make_sampler(&sampler_desc);

	sampler_desc.min_filter = SG_FILTER_NEAREST;
	sampler_desc.mag_filter = SG_FILTER_NEAREST;
	text_sampler            = sg_make_sampler(&sampler_desc);
	if(sg_query_sampler_state(image_sampler) != SG_RESOURCESTATE_VALID ||
		sg_query_sampler_state(text_sampler) != SG_RESOURCESTATE_VALID) {
		Log::fatal("Failed to create shared samplers");
	}

	// release mem

	glReleaseShaderCompiler();
#if defined(__GLIBC__)
	static_cast<void>(malloc_trim(0));
#endif
}

void shutdown() {
	if(sg_isvalid())
		sg_shutdown();
}

void begin_frame() {
	sg_pass pass{};
	pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
	pass.action.colors[0].clear_value = {0.f, 0.f, 0.f, 0.f};
	pass.swapchain                    = swapchain();

	sg_begin_pass(&pass);

	// Reset the scissor to the whole buffer; sokol may retain state from the
	// previous pass, so be explicit rather than relying on defaults.
	const auto buffer{Wayland::get_buffer_size()};
	current_clip = {0, 0, static_cast<int>(buffer[0]), static_cast<int>(buffer[1])};
	sg_apply_scissor_rect(0, 0, static_cast<int>(buffer[0]), static_cast<int>(buffer[1]), true);
	clip_stack.clear();
}

void end_frame() {
	sg_end_pass();
	sg_commit();

	Wayland::swap_buffer();
}

Texture prepare_image(string_view path) {
	// stbi_load needs a null-terminated filename; string_view does not guarantee
	// one, so copy. Image loading is rare (startup), so the copy is negligible.
	int                                                         width{}, height{}, channels{};
	const unique_ptr<unsigned char, decltype(&stbi_image_free)> pixels{
		stbi_load(string{path}.c_str(), &width, &height, &channels, 4),
		&stbi_image_free};
	if(!pixels) {
		Log::fatal(R"_(Failed to load picture, path="{}")_", path);
	}
	// stbi_load guarantees positive dimensions and an allocation that fits size_t.
	// Texture::upload validates the dimensions and byte count at the upload boundary.
	const size_t size{static_cast<size_t>(width) * static_cast<size_t>(height) * 4};

	Texture texture{};
	texture.upload(width, height, Texture::Format::RGBA8, {pixels.get(), size});
	return texture;
}

float text_scale() {
	return Wayland::get_scale();
}

Texture prepare_text(string_view text, FontHandle font) {
	return prepare_text(text, font, text_scale());
}

Texture prepare_text(string_view text, FontHandle font, float scale, float sub_x, float sub_y) {
	const Text::Bitmap bitmap{Text::draw(text, font, scale, sub_x, sub_y)};
	if(bitmap.width_ == 0 || bitmap.height_ == 0 || bitmap.pixels_.empty()) {
		return {};
	}
	if(bitmap.width_ > static_cast<unsigned>(numeric_limits<int>::max()) ||
		bitmap.height_ > static_cast<unsigned>(numeric_limits<int>::max())) {
		Log::fatal("Text bitmap dimensions are too large");
	}

	Texture texture{};
	texture.upload(static_cast<int>(bitmap.width_),
		static_cast<int>(bitmap.height_),
		Texture::Format::R8,
		bitmap.pixels_);
	texture.set_scale(scale);
	texture.set_origin(static_cast<float>(bitmap.offset_x_) / scale, static_cast<float>(bitmap.offset_y_) / scale);
	return texture;
}

void GpuRenderer::draw_rect(const Frame& frame, const Color& color, float radius, float opacity) {
	auto rgba{color.to_rgba()};
	rgba[3] *= opacity;

	const RectVert vertices{rectangle_vertices(frame, rgba)};
	const int      offset{sg_append_buffer(rect_vertex_buffer, SG_RANGE(vertices))};
	if(sg_query_buffer_overflow(rect_vertex_buffer)) {
		Log::fatal("Vertex buffer overflow");
	}

	sg_bindings bindings{};
	bindings.vertex_buffers[0]        = rect_vertex_buffer;
	bindings.vertex_buffer_offsets[0] = offset;
	sg_apply_pipeline(rectangle_pipeline);
	sg_apply_bindings(&bindings);
	const auto project{projection()};
	const auto radius_data{radius_uniform<rect_radius_uniform_t>(frame, clamp_radius(radius, frame))};
	sg_apply_uniforms(UB_rect_proj_uniform, SG_RANGE(project));
	sg_apply_uniforms(UB_rect_radius_uniform, SG_RANGE(radius_data));
	sg_draw(0, 4, 1);
}

void GpuRenderer::draw_image(const Frame& frame,
	const Texture&                        texture,
	const UvRect&                         uv,
	float                                 radius,
	float                                 opacity) {
	if(!texture.is_valid())
		return;

	// Prepared at the output resolution (within rounding): draw texel-per-pixel, as text does.
	const float px_scale{text_scale()};
	const float tex_w{static_cast<float>(texture.get_width()) * (uv.u1_ - uv.u0_)};
	const float tex_h{static_cast<float>(texture.get_height()) * (uv.v1_ - uv.v0_)};
	Frame       target{frame};
	if(abs(frame.width_ * px_scale - tex_w) < 1.f && abs(frame.height_ * px_scale - tex_h) < 1.f) {
		target.x_ += (frame.width_ - tex_w / px_scale) * 0.5f;
		target.y_ += (frame.height_ - tex_h / px_scale) * 0.5f;
		target.width_  = tex_w / px_scale;
		target.height_ = tex_h / px_scale;
		target         = snap_to_pixels(target);
	}
	const ImgVert vertices{image_vertices(target, uv)};
	const int     offset{sg_append_buffer(image_vertex_buffer, SG_RANGE(vertices))};
	if(sg_query_buffer_overflow(image_vertex_buffer)) {
		Log::fatal("Image vertex buffer overflow");
	}

	sg_bindings bindings{};
	bindings.vertex_buffers[0]        = image_vertex_buffer;
	bindings.vertex_buffer_offsets[0] = offset;
	bindings.samplers[SMP_smp]        = image_sampler;
	bindings.views[VIEW_tex]          = sg_view(texture.get_view_id());
	sg_apply_pipeline(image_pipeline);
	sg_apply_bindings(&bindings);

	const auto project{projection()};
	sg_apply_uniforms(UB_img_proj, SG_RANGE(project));
	const auto radius_data{radius_uniform<img_radius_uniform_t>(frame, clamp_radius(radius, frame))};
	sg_apply_uniforms(UB_img_radius_uniform, SG_RANGE(radius_data));

	img_params_t params{};
	params.tint[0] = params.tint[1] = params.tint[2] = 1.f;
	params.tint[3]                                   = opacity;
	sg_apply_uniforms(UB_img_params, SG_RANGE(params));
	sg_draw(0, 4, 1);
}

void GpuRenderer::draw_text(const Frame& frame,
	const Texture&                       texture,
	FontHandle /*font*/,
	const Color& color,
	float        opacity) {
	if(!texture.is_valid())
		return;

	// Texels only map 1:1 to pixels at the natural size; when stretched, nearest sampling
	// duplicates whole columns and snapping makes it step, so filter and place it exactly.
	const float px_scale{text_scale()};
	const bool  is_stretched{abs(frame.width_ * px_scale - static_cast<float>(texture.get_width())) > 0.01f ||
		abs(frame.height_ * px_scale - static_cast<float>(texture.get_height())) > 0.01f};

	// Snap the origin to the physical pixel grid. At fractional scales a
	// half-pixel offset resamples every glyph and makes text look blurry.
	const Frame snapped{is_stretched ? frame : snap_to_pixels(frame)};

	const ImgVert vertices{image_vertices(snapped)};
	const int     offset{sg_append_buffer(image_vertex_buffer, SG_RANGE(vertices))};
	if(sg_query_buffer_overflow(image_vertex_buffer)) {
		Log::fatal("Text vertex buffer overflow");
	}

	sg_bindings bindings{};
	bindings.vertex_buffers[0]        = image_vertex_buffer;
	bindings.vertex_buffer_offsets[0] = offset;
	bindings.samplers[SMP_smp]        = is_stretched ? image_sampler : text_sampler;
	bindings.views[VIEW_tex]          = sg_view(texture.get_view_id());
	sg_apply_pipeline(text_pipeline);
	sg_apply_bindings(&bindings);

	const auto project{projection()};
	sg_apply_uniforms(UB_img_proj, SG_RANGE(project));

	const auto    rgba{color.to_rgba()};
	text_params_t params{};
	copy(rgba.begin(), rgba.end(), params.color);
	params.color[3] *= opacity;
	sg_apply_uniforms(UB_text_params, SG_RANGE(params));
	sg_draw(0, 4, 1);
}

void GpuRenderer::draw_ring(const Frame& frame,
	const Color&                         color,
	const Color&                         track,
	float                                progress,
	float                                thickness,
	float                                opacity) {
	const float diameter{min(frame.width_, frame.height_)};
	thickness = min(thickness, diameter * 0.5f);
	if(!(thickness > 0.f))
		return;

	// Grown by a pixel so the outer edge's antialiasing is not cut off by the quad.
	const Frame   quad{frame.x_ - 1.f, frame.y_ - 1.f, frame.width_ + 2.f, frame.height_ + 2.f};
	const ImgVert vertices{image_vertices(quad)};
	const int     offset{sg_append_buffer(image_vertex_buffer, SG_RANGE(vertices))};
	if(sg_query_buffer_overflow(image_vertex_buffer)) {
		Log::fatal("Ring vertex buffer overflow");
	}

	sg_bindings bindings{};
	bindings.vertex_buffers[0]        = image_vertex_buffer;
	bindings.vertex_buffer_offsets[0] = offset;
	sg_apply_pipeline(ring_pipeline);
	sg_apply_bindings(&bindings);

	const auto project{projection()};
	sg_apply_uniforms(UB_img_proj, SG_RANGE(project));

	ring_params_t params{};
	const auto    fg{color.to_rgba()};
	const auto    bg{track.to_rgba()};
	copy(fg.begin(), fg.end(), params.color);
	copy(bg.begin(), bg.end(), params.track);
	params.color[3] *= opacity;
	params.track[3] *= opacity;
	params.geometry[0] = frame.x_ + frame.width_ * 0.5f;
	params.geometry[1] = frame.y_ + frame.height_ * 0.5f;
	params.geometry[2] = (diameter - thickness) * 0.5f;
	params.geometry[3] = thickness * 0.5f;
	params.arc[0]      = clamp(progress, 0.f, 1.f);
	sg_apply_uniforms(UB_ring_params, SG_RANGE(params));
	sg_draw(0, 4, 1);
}

void GpuRenderer::push_clip(const Frame& frame, float /*radius*/) {
	// Rounded corners are clipped in the fragment shaders; the scissor only
	// bounds the rectangular extent, so radius is not needed here.
	const array<int, 4> next{intersect_scissor(current_clip, frame_to_scissor(frame))};
	clip_stack.push_back(current_clip);
	apply_scissor(next);
}

void GpuRenderer::pop_clip() {
	if(clip_stack.empty())
		return;
	const array<int, 4> prev{clip_stack.back()};
	clip_stack.pop_back();
	apply_scissor(prev);
}

}
