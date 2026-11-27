#pragma once

#include <cstdint>
#include <span>
#include <utility>

namespace Renderer {
struct TextureAccess;
}

/// @brief GPU texture with RAII ownership of the underlying sokol image and view.
class Texture {
private:
	struct Impl;
	Impl* impl_{};

public:
	/// @brief Source pixel layout of an uploaded texture.
	enum class Format : uint8_t {
		R8,
		RGBA8,
	};

	/// @brief Creates an empty texture.
	Texture();

	Texture(const Texture&) = delete;

	Texture& operator=(const Texture&) = delete;

	/// @brief Move-assigns, destroying any existing contents.
	Texture& operator=(Texture&& other) noexcept {
		std::swap(impl_, other.impl_);
		return *this;
	}

	/// @brief Move-constructs, leaving `other` empty.
	Texture(Texture&& other) noexcept :
		impl_(other.impl_) {
		other.impl_ = nullptr;
	}

	~Texture();

	/// @brief Releases the GPU image and view, restoring the empty state.
	void reset();

	/// @brief Opaque sokol view id, or 0 if invalid.
	[[nodiscard]] uint32_t get_view_id() const;

	/// @brief Uploads `pixels` as a new immutable texture of `width` x `height`.
	void upload(int width, int height, Format format, std::span<const uint8_t> pixels);

	// Content rasterized for high-DPI is uploaded at scale (physical size), while
	// the scene graph lays out in logical size. Record the ratio so callers can
	// recover the logical extent; defaults to 1.0 (logical == physical).
	/// @brief Sets physical-to-logical ratio; values <= 0 reset to 1.f.
	void set_scale(float scale);

	/// @brief Physical-to-logical ratio.
	[[nodiscard]] float get_scale() const;

	/// @brief Sets the origin offset in logical pixels.
	void set_origin(float x, float y);

	/// @brief Origin x offset in logical pixels.
	[[nodiscard]] float get_origin_x() const;

	/// @brief Origin y offset in logical pixels.
	[[nodiscard]] float get_origin_y() const;

	/// @brief True if the texture has a valid GPU view.
	[[nodiscard]] bool is_valid() const;

	/// @brief Physical width in pixels, or 0 if empty.
	[[nodiscard]] int get_width() const;

	/// @brief Physical height in pixels, or 0 if empty.
	[[nodiscard]] int get_height() const;

	// Physical size divided by scale; used by layout, not by the GPU.
	/// @brief Width in logical pixels (physical width divided by scale).
	[[nodiscard]] float get_logical_width() const;

	/// @brief Height in logical pixels (physical height divided by scale).
	[[nodiscard]] float get_logical_height() const;
};
