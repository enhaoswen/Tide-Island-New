#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <source_location>
#include <string>
#include <string_view>

// input can be #RRGGBB or #RRGGBBAA
// But not rgb(a,b,c) or rgba(a,b,c,d)

// Ex. #AABBCC or #AABBCCDD
// but not rgb(1,2,3) or rgba(1,2,3,4)

// Values are normalized RGBA in [0, 1], rather than [0, 255].
// Ex. opaque black = {0.0, 0.0, 0.0, 1.0}
// opaque red = {1.0, 0.0, 0.0, 1.0}

/// @brief RGBA color with normalized components in [0, 1].
class Color {
private:
	std::array<float, 4> color_{0.f, 0.f, 0.f, 1.f};

	struct PresetTag {};

	constexpr Color(PresetTag, uint8_t r, uint8_t g, uint8_t b) :
		color_{
			static_cast<float>(r) / 255.f,
			static_cast<float>(g) / 255.f,
			static_cast<float>(b) / 255.f,
			1.f,
		} {}

public:
	/// @brief Opaque black preset.
	static const Color k_black;
	/// @brief Opaque white preset.
	static const Color k_white;
	/// @brief Opaque gray preset.
	static const Color k_gray;
	/// @brief Opaque silver preset.
	static const Color k_silver;
	/// @brief Opaque red preset.
	static const Color k_red;
	/// @brief Opaque maroon preset.
	static const Color k_maroon;
	/// @brief Opaque yellow preset.
	static const Color k_yellow;
	/// @brief Opaque olive preset.
	static const Color k_olive;
	/// @brief Opaque lime preset.
	static const Color k_lime;
	/// @brief Opaque green preset.
	static const Color k_green;
	/// @brief Opaque cyan preset.
	static const Color k_cyan;
	/// @brief Opaque teal preset.
	static const Color k_teal;
	/// @brief Opaque blue preset.
	static const Color k_blue;
	/// @brief Opaque navy preset.
	static const Color k_navy;
	/// @brief Opaque magenta preset.
	static const Color k_magenta;
	/// @brief Opaque purple preset.
	static const Color k_purple;

	/// @brief Default-constructs opaque black.
	Color() = default;
	/// @brief Constructs from a #RRGGBB or #RRGGBBAA string, logging on error.
	Color(std::string_view color, std::source_location location = std::source_location::current());
	/// @brief Constructs from normalized RGB components with opaque alpha.
	Color(std::array<float, 3> arg_color, std::source_location location = std::source_location::current());
	/// @brief Constructs from normalized RGBA components, logging on error.
	Color(std::array<float, 4> arg_color, std::source_location location = std::source_location::current());
	/// @brief Constructs from normalized RGBA components, logging on error.
	Color(float r, float g, float b, float a, std::source_location location = std::source_location::current());
	/// @brief Constructs from normalized RGB components with opaque alpha.
	Color(float r, float g, float b, std::source_location location = std::source_location::current());

	/// @brief Parses a #RRGGBB or #RRGGBBAA string into a Color.
	[[nodiscard]] static std::expected<Color, std::string_view> parse(std::string_view input);

	/// @brief Formats the color as #RRGGBBAA.
	[[nodiscard]] std::string to_string() const;

	/// @brief Returns the normalized RGBA components.
	[[nodiscard]] constexpr std::array<float, 4> to_rgba() const { return color_; }

	/// @brief Returns a reference to the component at index.
	[[nodiscard]] constexpr float& operator[](size_t index) { return color_[index]; }

	/// @brief Returns a const reference to the component at index.
	[[nodiscard]] constexpr const float& operator[](size_t index) const { return color_[index]; }
};

inline constexpr Color Color::k_black{PresetTag{}, 0, 0, 0};
inline constexpr Color Color::k_white{PresetTag{}, 255, 255, 255};
inline constexpr Color Color::k_gray{PresetTag{}, 128, 128, 128};
inline constexpr Color Color::k_silver{PresetTag{}, 192, 192, 192};
inline constexpr Color Color::k_red{PresetTag{}, 255, 0, 0};
inline constexpr Color Color::k_maroon{PresetTag{}, 128, 0, 0};
inline constexpr Color Color::k_yellow{PresetTag{}, 255, 255, 0};
inline constexpr Color Color::k_olive{PresetTag{}, 128, 128, 0};
inline constexpr Color Color::k_lime{PresetTag{}, 0, 255, 0};
inline constexpr Color Color::k_green{PresetTag{}, 0, 128, 0};
inline constexpr Color Color::k_cyan{PresetTag{}, 0, 255, 255};
inline constexpr Color Color::k_teal{PresetTag{}, 0, 128, 128};
inline constexpr Color Color::k_blue{PresetTag{}, 0, 0, 255};
inline constexpr Color Color::k_navy{PresetTag{}, 0, 0, 128};
inline constexpr Color Color::k_magenta{PresetTag{}, 255, 0, 255};
inline constexpr Color Color::k_purple{PresetTag{}, 128, 0, 128};
