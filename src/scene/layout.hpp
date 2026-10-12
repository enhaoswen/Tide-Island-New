#pragma once

#include "utils/struct.hpp"
#include "utils/log.hpp"

#include <concepts>
#include <source_location>

namespace Layout::DontUse {

template <typename... Objs>
void calculate_horizontal_layout(
    std::source_location location,
    Frame parent_frame,
    Align x_align,
    Align y_align,
    float spacing,
    Objs&... objs
) requires (
    requires(Objs& obj) {
        {obj.frame} -> std::same_as<Frame&>;
    } && ...
) {
    if constexpr (sizeof...(objs) == 0) {
        Log::logger(
            Log::Error,
            "{}:{}: No objects provided for layout calculation",
            location.file_name(),
            location.line()
        );
    }
    else {
        float total_width = (0.0f + ... + objs.frame.width)
            + spacing * static_cast<float>(sizeof...(objs) - 1);

        float x = parent_frame.x;
        if (x_align == Align::Center) {
            x += (parent_frame.width - total_width) / 2;
        }
        else if (x_align == Align::Right) {
            x += parent_frame.width - total_width;
        }

        auto place = [&](Frame& frame) {
            frame.x = x;
            frame.y = parent_frame.y;

            if (y_align == Align::Center) {
                frame.y += (parent_frame.height - frame.height) / 2;
            }
            else if (y_align == Align::Right) {
                frame.y += parent_frame.height - frame.height;
            }

            x += frame.width + spacing;
        };
        (place(objs.frame), ...);
    }
}

template <typename... Objs>
void calculate_horizontal_layout(
    std::source_location location,
    Frame,
    Align,
    Align,
    float,
    Objs&...
) {
    Log::logger(
        Log::Error,
        "{}:{}: Not supported object",
        location.file_name(),
        location.line()
    );
}

template <typename... Objs>
void calculate_vertical_layout(
    std::source_location location,
    Frame parent_frame,
    Align x_align,
    Align y_align,
    float spacing,
    Objs&... objs
) requires (
    requires(Objs& obj) {
        {obj.frame} -> std::same_as<Frame&>;
    } && ...
) {
    if constexpr (sizeof...(objs) == 0) {
        Log::logger(
            Log::Error,
            "{}:{}: No objects provided for layout calculation",
            location.file_name(),
            location.line()
        );
    }
    else {
        float total_height = (0.0f + ... + objs.frame.height)
            + spacing * static_cast<float>(sizeof...(objs) - 1);

        float y = parent_frame.y;
        if (y_align == Align::Center) {
            y += (parent_frame.height - total_height) / 2;
        }
        else if (y_align == Align::Right) {
            y += parent_frame.height - total_height;
        }

        auto place = [&](Frame& frame) {
            frame.y = y;
            frame.x = parent_frame.x;

            if (x_align == Align::Center) {
                frame.x += (parent_frame.width - frame.width) / 2;
            }
            else if (x_align == Align::Right) {
                frame.x += parent_frame.width - frame.width;
            }

            y += frame.height + spacing;
        };
        (place(objs.frame), ...);
    }
}

template <typename... Objs>
void calculate_vertical_layout(
    std::source_location location,
    Frame,
    Align,
    Align,
    float,
    Objs&...
) {
    Log::logger(
        Log::Error,
        "{}:{}: Not supported object",
        location.file_name(),
        location.line()
    );
}

} // namespace Layout::DontUse

#define Layout_horizontal_layout(parent_frame, x_align, y_align, spacing, ...) \
    Layout::DontUse::calculate_horizontal_layout( \
        std::source_location::current(), \
        parent_frame, x_align, y_align, spacing __VA_OPT__(,) __VA_ARGS__ \
    )

#define Layout_vertical_layout(parent_frame, x_align, y_align, spacing, ...) \
    Layout::DontUse::calculate_vertical_layout( \
        std::source_location::current(), \
        parent_frame, x_align, y_align, spacing __VA_OPT__(,) __VA_ARGS__ \
    )
