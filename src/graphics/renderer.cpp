#define SOKOL_IMPL
#define STB_IMAGE_IMPLEMENTATION

#include "stb_image.h"
#include "graphics/renderer.hpp"
#include "backend/wayland.hpp"
#include "sokol_gfx.h"
#include "graphics/shaders/basic.glsl.h"
#include "sokol_log.h"
#include "utils/log.hpp"
#include "graphics/text.hpp"

#include <GLES3/gl3.h>
#include <algorithm>
#include <limits>

#if defined(__GLIBC__)
#include <malloc.h>
#endif

using namespace std;

namespace {

struct RectVert {
    float p1_x, p1_y;
    float p1_r, p1_g, p1_b, p1_a;

    float p2_x, p2_y;
    float p2_r, p2_g, p2_b, p2_a;

    float p3_x, p3_y;
    float p3_r, p3_g, p3_b, p3_a;

    float p4_x, p4_y;
    float p4_r, p4_g, p4_b, p4_a;
};

struct ImgVert {
    float p1_x, p1_y;
    float p1_u, p1_v;

    float p2_x, p2_y;
    float p2_u, p2_v;

    float p3_x, p3_y;
    float p3_u, p3_v;

    float p4_x, p4_y;
    float p4_u, p4_v;
};

// tmp
sg_shader rectangle_shader{};
sg_pipeline rectangle_pipeline{};
sg_buffer rect_vertex_buffer{};

sg_shader image_shader{};
sg_pipeline image_pipeline{};
sg_buffer image_vertex_buffer{};

sg_shader text_shader{};
sg_pipeline text_pipeline{};

RectVert rectangle_vertices(
    Frame frame,
    array<float, 4> color
) {
    float left   = frame.x;
    float top    = frame.y;
    float right  = left + frame.width;
    float bottom = top + frame.height;

    // x, y, r, g, b, a

    return {
        left,  top,    color[0], color[1], color[2], color[3],
        right, top,    color[0], color[1], color[2], color[3],
        left,  bottom, color[0], color[1], color[2], color[3],
        right, bottom, color[0], color[1], color[2], color[3],
    };
}

ImgVert image_vertices(Frame frame) {
    float left   = frame.x;
    float top    = frame.y;
    float right  = left + frame.width;
    float bottom = top + frame.height;

    return {
        // x, y, u, v
        left,  top,    0.0f, 0.0f,
        right, top,    1.0f, 0.0f,
        left,  bottom, 0.0f, 1.0f,
        right, bottom, 1.0f, 1.0f,
    };
}

rect_proj_uniform_t projection() {
    auto surface_size = Wayland::get_surface_size();

    rect_proj_uniform_t result{};
    result.proj[0] = 2.0F / surface_size[0];
    result.proj[5] = -2.0F / surface_size[1];
    result.proj[10] = 1.0F;
    result.proj[12] = -1.0F;
    result.proj[13] = 1.0F;
    result.proj[15] = 1.0F;
    return result;
}

template <typename T>
T radius_uniform(Frame frame, float radius) {
    T result{};
    result.center[0] = frame.x + frame.width / 2.0F;
    result.center[1] = frame.y + frame.height / 2.0F;
    result.half_size[0] = frame.width / 2.0F;
    result.half_size[1] = frame.height / 2.0F;
    result.radius = radius;
    return result;
}

void enable_blending(sg_pipeline_desc& descriptor) {
    auto& blend = descriptor.colors[0].blend;
    blend.enabled = true;
    blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
    blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    blend.src_factor_alpha = SG_BLENDFACTOR_ONE;
    blend.dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
}

sg_swapchain swapchain() {
    auto surface_size= Wayland::get_buffer_size();

    sg_swapchain result{};
    result.width = surface_size[0];
    result.height = surface_size[1];
    result.sample_count = 1;
    result.color_format = SG_PIXELFORMAT_RGBA8;
    result.depth_format = SG_PIXELFORMAT_NONE;
    result.gl.framebuffer = 0;
    return result;
}

Frame calculate_frame(
    Frame frame,
    Align horizontal_align,
    Align vertical_align,
    float image_width,
    float image_height,
    bool resize
) {

    Frame result{};

    result.width = image_width;
    result.height = image_height;

    if (resize) {

        float scale_x = frame.width / image_width;
        float scale_y = frame.height / image_height;
        float scale = min(scale_x, scale_y);

        result.width = image_width * scale;
        result.height = image_height * scale;
    }

    if (horizontal_align == Align::Left) {
        result.x = frame.x;
    } else if (horizontal_align == Align::Center) {
        result.x = frame.x + (frame.width - result.width) / 2;
    } else if (horizontal_align == Align::Right) {
        result.x = frame.x + frame.width - result.width;
    }

    if (vertical_align == Align::Left) {
        result.y = frame.y;
    } else if (vertical_align == Align::Center) {
        result.y = frame.y + (frame.height - result.height) / 2;
    } else if (vertical_align == Align::Right) {
        result.y = frame.y + frame.height - result.height;
    }

    return result;
}

} // namespace

void Renderer::init() {
    // sokol environment init

    sg_desc descriptor{};
    descriptor.logger.func = slog_func;
    descriptor.environment.defaults.color_format = SG_PIXELFORMAT_RGBA8;
    descriptor.environment.defaults.depth_format = SG_PIXELFORMAT_NONE;
    descriptor.environment.defaults.sample_count = 1;
    sg_setup(&descriptor);
    if (!sg_isvalid()) {
        Log::fatal("Failed to initialize Sokol");
    }

    // rectangle environment init

    rectangle_shader = sg_make_shader(rectangle_shader_desc(sg_query_backend()));

    sg_pipeline_desc rectangle_pipe_desc{};

    rectangle_pipe_desc.shader = rectangle_shader;
    rectangle_pipe_desc.layout.attrs[ATTR_rectangle_position].format = SG_VERTEXFORMAT_FLOAT2;
    rectangle_pipe_desc.layout.attrs[ATTR_rectangle_color].format = SG_VERTEXFORMAT_FLOAT4;
    rectangle_pipe_desc.primitive_type = SG_PRIMITIVETYPE_TRIANGLE_STRIP;
    enable_blending(rectangle_pipe_desc);
    rectangle_pipeline = sg_make_pipeline(&rectangle_pipe_desc);

    sg_buffer_desc rectangle_buffer_desc{};
    rectangle_buffer_desc.size = 16 * 1024;
    rectangle_buffer_desc.usage.dynamic_update = true;
    rectangle_buffer_desc.label = "rect_vertex_buffer";
    rect_vertex_buffer = sg_make_buffer(&rectangle_buffer_desc);

    // image environment init

    image_shader = sg_make_shader(image_shader_desc(sg_query_backend()));

    sg_pipeline_desc image_pipe_desc{};

    image_pipe_desc.shader = image_shader;
    image_pipe_desc.layout.attrs[ATTR_image_position].format = SG_VERTEXFORMAT_FLOAT2;
    image_pipe_desc.layout.attrs[ATTR_image_coord].format = SG_VERTEXFORMAT_FLOAT2;
    image_pipe_desc.primitive_type = SG_PRIMITIVETYPE_TRIANGLE_STRIP;
    enable_blending(image_pipe_desc);
    image_pipeline = sg_make_pipeline(image_pipe_desc);

    sg_buffer_desc image_buffer_desc{};
    image_buffer_desc.size = 1024 * 1024;
    image_buffer_desc.usage.dynamic_update = true;
    image_buffer_desc.label = "image_vertex_buffer";
    image_vertex_buffer = sg_make_buffer(&image_buffer_desc);

    // text environment init

    text_shader = sg_make_shader(text_shader_desc(sg_query_backend()));

    sg_pipeline_desc text_pipe_desc{};
    text_pipe_desc.shader = text_shader;
    text_pipe_desc.layout.attrs[ATTR_text_position].format = SG_VERTEXFORMAT_FLOAT2;
    text_pipe_desc.layout.attrs[ATTR_text_coord].format = SG_VERTEXFORMAT_FLOAT2;
    text_pipe_desc.primitive_type = SG_PRIMITIVETYPE_TRIANGLE_STRIP;
    enable_blending(text_pipe_desc);
    text_pipeline = sg_make_pipeline(text_pipe_desc);

    if (sg_query_pipeline_state(text_pipeline) != SG_RESOURCESTATE_VALID) {
        Log::fatal("Text pipeline is invalid (shader compile error?)");
    }

    // release mem

    glReleaseShaderCompiler();
    #if defined(__GLIBC__)
        static_cast<void>(malloc_trim(0));
    #endif
}

void Renderer::begin_frame() {
    sg_pass pass{};
    pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
    pass.action.colors[0].clear_value = {0.0F, 0.0F, 0.0F, 0.0F};
    pass.swapchain = swapchain();

    sg_begin_pass(&pass);
}

void Renderer::end_frame() {
    sg_end_pass();
    sg_commit();

    Wayland::swap_buffer();
}

void Renderer::draw_rectangle(const RectDesc& desc) {

    float max_r = min(desc.frame.width, desc.frame.height) * 0.5f;
    float radius = clamp(desc.radius, 0.0f, max_r);

    RectVert vertices = rectangle_vertices(desc.frame, desc.color.to_rgba());
    int offset = sg_append_buffer(rect_vertex_buffer, SG_RANGE(vertices));
    if (sg_query_buffer_overflow(rect_vertex_buffer)) {
        Log::fatal("Vertex bufer overflow");
    }

    sg_bindings bindings{};
    bindings.vertex_buffers[0] = rect_vertex_buffer;
    bindings.vertex_buffer_offsets[0] = offset;
    sg_apply_pipeline(rectangle_pipeline);
    sg_apply_bindings(&bindings);
    auto project = projection();
    auto radius_data = radius_uniform<rect_radius_uniform_t>(desc.frame, radius);
    sg_apply_uniforms(UB_rect_proj_uniform, SG_RANGE(project));
    sg_apply_uniforms(UB_rect_radius_uniform, SG_RANGE(radius_data));
    sg_draw(0, 4, 1);
}

void Renderer::draw_image(const ImageDesc& desc) {

    int width, height, channels;

    unsigned char* pixels = stbi_load(desc.path.c_str(), &width, &height, &channels, 4);

    if (!pixels) {
        Log::fatal(R"@(Failed to load picture, path="{}")@", desc.path);
    }

    sg_image_desc image_desc {
        .type = SG_IMAGETYPE_2D,
        .usage = {
            .immutable = true
        },
        .width = width,
        .height = height,
        .num_slices = 1,
        .num_mipmaps = 1,
        .pixel_format = SG_PIXELFORMAT_RGBA8,
        .sample_count = 1,
        .data = {
            .mip_levels = {
                {
                    .ptr = pixels,
                    .size = static_cast<size_t>(width * height * 4)
                }
            }
        }
    };
    sg_image image = sg_make_image(image_desc);

    if (sg_query_image_state(image) != SG_RESOURCESTATE_VALID) {
        Log::fatal("Failed to create image");
    }

    sg_sampler_desc sampler_desc {
        .min_filter = SG_FILTER_LINEAR,
        .mag_filter = SG_FILTER_LINEAR,
        .wrap_u = SG_WRAP_CLAMP_TO_EDGE,
        .wrap_v = SG_WRAP_CLAMP_TO_EDGE,
    };
    sg_sampler image_sampler = sg_make_sampler(sampler_desc);

    if (sg_query_sampler_state(image_sampler) != SG_RESOURCESTATE_VALID) {
        Log::fatal("Failed to create sampler");
    }

    ImgVert vertices = image_vertices(
        calculate_frame(
            desc.frame,
            desc.horizontal_align,
            desc.vertical_align,
            width, 
            height,
            true
        ));

    int offset = sg_append_buffer(image_vertex_buffer, SG_RANGE(vertices));
    if (sg_query_buffer_overflow(image_vertex_buffer)) {
        Log::fatal("Image vertex buffer overflow");
    }

    sg_view_desc tex_view_desc {
        .texture = {
            .image = image
        }
    };

    sg_view tex_view = sg_make_view(tex_view_desc);

    if (sg_query_view_state(tex_view) != SG_RESOURCESTATE_VALID) {
        Log::fatal("Failed to create texture view");
    }

    sg_bindings bindings{};
    bindings.vertex_buffers[0] = image_vertex_buffer;
    bindings.vertex_buffer_offsets[0] = offset;
    bindings.samplers[SMP_smp] = image_sampler;
    bindings.views[VIEW_tex] = tex_view;

    sg_apply_pipeline(image_pipeline);

    if (sg_query_pipeline_state(image_pipeline) != SG_RESOURCESTATE_VALID) {
        Log::fatal("Image pipeline is invalid (shader compile error?)");
    }

    sg_apply_bindings(&bindings);

    auto project = projection();
    sg_apply_uniforms(UB_img_proj, SG_RANGE(project));

    auto radius_data = radius_uniform<img_radius_uniform_t>(desc.frame, desc.radius);
    sg_apply_uniforms(UB_img_radius_uniform, SG_RANGE(radius_data));

    sg_draw(0, 4, 1);

    stbi_image_free(pixels);
    sg_destroy_image(image);
    sg_destroy_sampler(image_sampler);
    sg_destroy_view(tex_view);
}

void Renderer::draw_text(const TextDesc& desc) {
    const double scale = Wayland::get_scale();
    Text::Bitmap bitmap = Text::draw(desc.text, desc.font, scale);
    if (bitmap.width == 0 || bitmap.height == 0 || bitmap.pixels.empty()) {
        return;
    }
    if (bitmap.width > static_cast<unsigned>(numeric_limits<int>::max()) ||
        bitmap.height > static_cast<unsigned>(numeric_limits<int>::max())) {
        Log::fatal("Text bitmap dimensions are too large");
    }

    Frame new_frame = calculate_frame(
        desc.frame,
        desc.horizontal_align,
        desc.vertical_align,
        static_cast<float>(bitmap.width / scale),
        static_cast<float>(bitmap.height / scale),
        false
    );

    sg_image_desc image_desc {
        .type = SG_IMAGETYPE_2D,
        .usage = {
            .immutable = true
        },
        .width = static_cast<int>(bitmap.width),
        .height = static_cast<int>(bitmap.height),
        .num_slices = 1,
        .num_mipmaps = 1,
        .pixel_format = SG_PIXELFORMAT_R8,
        .sample_count = 1,
        .data = {
            .mip_levels = {
                {
                    .ptr = bitmap.pixels.data(),
                    .size = bitmap.pixels.size()
                }
            }
        }
    };

    sg_image image = sg_make_image(image_desc);
    if (sg_query_image_state(image) != SG_RESOURCESTATE_VALID) {
        Log::fatal("Failed to create text image");
    }

    sg_sampler_desc sampler_desc {
        .min_filter = SG_FILTER_LINEAR,
        .mag_filter = SG_FILTER_LINEAR,
        .wrap_u = SG_WRAP_CLAMP_TO_EDGE,
        .wrap_v = SG_WRAP_CLAMP_TO_EDGE,
    };
    sg_sampler sampler = sg_make_sampler(sampler_desc);
    if (sg_query_sampler_state(sampler) != SG_RESOURCESTATE_VALID) {
        Log::fatal("Failed to create text sampler");
    }

    ImgVert vertices = image_vertices(new_frame);
    int offset = sg_append_buffer(image_vertex_buffer, SG_RANGE(vertices));
    if (sg_query_buffer_overflow(image_vertex_buffer)) {
        Log::fatal("Text vertex buffer overflow");
    }

    sg_view_desc view_desc {
        .texture = {
            .image = image
        }
    };
    sg_view view = sg_make_view(view_desc);
    if (sg_query_view_state(view) != SG_RESOURCESTATE_VALID) {
        Log::fatal("Failed to create text texture view");
    }

    sg_bindings bindings{};
    bindings.vertex_buffers[0] = image_vertex_buffer;
    bindings.vertex_buffer_offsets[0] = offset;
    bindings.samplers[SMP_smp] = sampler;
    bindings.views[VIEW_tex] = view;

    sg_apply_pipeline(text_pipeline);
    sg_apply_bindings(&bindings);

    auto project = projection();
    sg_apply_uniforms(UB_img_proj, SG_RANGE(project));
    text_params_t params{};
    auto color = desc.color.to_rgba();
    copy(color.begin(), color.end(), params.color);
    sg_apply_uniforms(UB_text_params, SG_RANGE(params));
    sg_draw(0, 4, 1);

    sg_destroy_view(view);
    sg_destroy_sampler(sampler);
    sg_destroy_image(image);
}
