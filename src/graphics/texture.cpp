#include "graphics/texture.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>

#include "sokol_gfx.h"
#include "utils/log.hpp"

using namespace std;

struct Texture::Impl {
	sg_image image_{};
	sg_view  view_{};
	int      width_{};
	int      height_{};
	float    scale_{1.f};
	float    origin_x_{};
	float    origin_y_{};

	void reset() {
		if(view_.id)
			sg_destroy_view(view_);
		if(image_.id)
			sg_destroy_image(image_);

		view_  = {};
		image_ = {};
		width_ = height_ = 0;
		scale_           = 1.f;
		origin_x_ = origin_y_ = 0.f;
	}

	~Impl() { reset(); }
};

Texture::Texture() :
	impl_{new Impl{}} {
}

Texture::~Texture() {
	delete impl_;
}

void Texture::reset() {
	if(impl_) {
		impl_->reset();
	}
}

void Texture::upload(int width, int height, Format format, span<const uint8_t> pixels) {
	if(width <= 0 || height <= 0) {
		Log::fatal("Invalid texture dimensions: {}*{}", width, height);
	}

	size_t          channels{};
	sg_pixel_format pixel_format{};
	switch(format) {
		case Format::R8:
			channels     = 1;
			pixel_format = SG_PIXELFORMAT_R8;
			break;
		case Format::RGBA8:
			channels     = 4;
			pixel_format = SG_PIXELFORMAT_RGBA8;
			break;
		default:
			Log::fatal("Invalid texture format");
			return;
	}

	const auto pixel_width{static_cast<size_t>(width)};
	const auto pixel_height{static_cast<size_t>(height)};
	if(pixel_width > numeric_limits<size_t>::max() / pixel_height / channels) {
		Log::fatal("Texture dimensions are too large");
	}
	const auto required{pixel_width * pixel_height * channels};
	if(pixels.size() != required) {
		Log::fatal("Texture pixel data size mismatch: expected={}, actual={}", required, pixels.size());
	}
	if(!sg_isvalid()) {
		Log::fatal("Texture upload requires an initialized renderer");
	}

	if(!impl_) {
		impl_ = new Impl{};
	}
	reset();

	sg_image_desc desc{};
	desc.width              = width;
	desc.height             = height;
	desc.usage.immutable    = true;
	desc.pixel_format       = pixel_format;
	desc.data.mip_levels[0] = {pixels.data(), pixels.size()};
	impl_->image_           = sg_make_image(&desc);
	if(sg_query_image_state(impl_->image_) != SG_RESOURCESTATE_VALID) {
		reset();
		Log::fatal("Failed to create texture");
	}

	sg_view_desc view_desc{};
	view_desc.texture.image = impl_->image_;
	impl_->view_            = sg_make_view(&view_desc);
	if(sg_query_view_state(impl_->view_) != SG_RESOURCESTATE_VALID) {
		reset();
		Log::fatal("Failed to create texture view");
	}

	impl_->width_  = width;
	impl_->height_ = height;
}

bool Texture::is_valid() const {
	return impl_ && impl_->view_.id != 0;
}

int Texture::get_width() const {
	return impl_ ? impl_->width_ : 0;
}

int Texture::get_height() const {
	return impl_ ? impl_->height_ : 0;
}

uint32_t Texture::get_view_id() const {
	return impl_ ? impl_->view_.id : 0;
}

void Texture::set_scale(float scale) {
	if(impl_)
		impl_->scale_ = scale > 0.f ? scale : 1.f;
}

float Texture::get_scale() const {
	return impl_ ? impl_->scale_ : 1.f;
}

void Texture::set_origin(float x, float y) {
	if(impl_) {
		impl_->origin_x_ = x;
		impl_->origin_y_ = y;
	}
}

float Texture::get_origin_x() const {
	return impl_ ? impl_->origin_x_ : 0.f;
}

float Texture::get_origin_y() const {
	return impl_ ? impl_->origin_y_ : 0.f;
}

float Texture::get_logical_width() const {
	return impl_ ? static_cast<float>(impl_->width_) / impl_->scale_ : 0.f;
}

float Texture::get_logical_height() const {
	return impl_ ? static_cast<float>(impl_->height_) / impl_->scale_ : 0.f;
}
