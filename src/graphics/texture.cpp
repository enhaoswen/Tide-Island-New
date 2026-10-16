#include "texture.hpp"
#include "sokol_gfx.h"
#include "utils/log.hpp"

#include <limits>

using namespace std;

struct Texture::Impl {
    sg_image image{};
    sg_view view{};
    int width{};
    int height{};

    void reset() {
        if (view.id)
            sg_destroy_view(view);
        if (image.id)
            sg_destroy_image(image);

        view = {};
        image = {};
        width = height = 0;
    }

    ~Impl() {
        reset();
    }
};

Texture::Texture() {
    impl = new Impl{};
}

Texture::~Texture() {
    delete impl;
}

void Texture::reset() {
    if (impl) {
        impl->reset();
    }
}

void Texture::upload(int width, int height, Format format,
    span<const uint8_t> pixels) {
    if (width <= 0 || height <= 0) {
        Log::fatal("Invalid texture dimensions: {}*{}", width, height);
    }

    size_t channels{};
    sg_pixel_format pixel_format{};
    switch (format) {
    case Format::R8:
        channels = 1;
        pixel_format = SG_PIXELFORMAT_R8;
        break;
    case Format::RGBA8:
        channels = 4;
        pixel_format = SG_PIXELFORMAT_RGBA8;
        break;
    default:
        Log::fatal("Invalid texture format");
        return;
    }

    if (static_cast<size_t>(width) >
        numeric_limits<size_t>::max() / height / channels) {
        Log::fatal("Texture dimensions are too large");
    }
    const auto required = static_cast<size_t>(width) * height * channels;
    if (pixels.size() != required) {
        Log::fatal("Texture pixel data size mismatch: expected={}, actual={}",
            required, pixels.size());
    }
    if (!sg_isvalid()) {
        Log::fatal("Texture upload requires an initialized renderer");
    }

    if (!impl) {
        impl = new Impl{};
    }
    reset();

    sg_image_desc desc{};
    desc.width = width;
    desc.height = height;
    desc.usage.immutable = true;
    desc.pixel_format = pixel_format;
    desc.data.mip_levels[0] = {pixels.data(), pixels.size()};
    impl->image = sg_make_image(&desc);
    if (sg_query_image_state(impl->image) != SG_RESOURCESTATE_VALID) {
        reset();
        Log::fatal("Failed to create texture");
    }

    sg_view_desc view_desc{};
    view_desc.texture.image = impl->image;
    impl->view = sg_make_view(&view_desc);
    if (sg_query_view_state(impl->view) != SG_RESOURCESTATE_VALID) {
        reset();
        Log::fatal("Failed to create texture view");
    }

    impl->width = width;
    impl->height = height;
}

bool Texture::valid() const {
    return impl && impl->view.id != 0;
}

int Texture::width() const {
    return impl ? impl->width : 0;
}

int Texture::height() const {
    return impl ? impl->height : 0;
}

uint32_t Texture::view_id() const {
    return impl ? impl->view.id : 0;
}
