#pragma once

#include "scene/object.hpp"
#include "utils/log.hpp"

#include <concepts>
#include <source_location>
#include <type_traits>

namespace Layout::DontUse {

// Accept descriptors, objects, and object pointers. Sizes stay unchanged.
// For y_align, Left means top and Right means bottom.
template <typename T>
Frame get_frame(const T& obj)
requires requires { {obj.frame} -> std::same_as<const Frame&>; } {
    return obj.frame;
}

inline Frame get_frame(const Object::Rectangle& obj) { return obj.frame(); }
inline Frame get_frame(const Object::Rectangle* obj) { return obj->frame(); }
inline Frame get_frame(const Object::Image& obj) { return obj.frame(); }
inline Frame get_frame(const Object::Image* obj) { return obj->frame(); }
inline Frame get_frame(const Object::Text& obj) { return obj.frame(); }
inline Frame get_frame(const Object::Text* obj) { return obj->frame(); }

template <typename T>
void set_frame(T& obj, const Frame& frame)
requires requires { {obj.frame} -> std::same_as<Frame&>; } {
    obj.frame = frame;
}

inline void set_frame(Object::Rectangle& obj, const Frame& frame) { obj.set_frame(frame); }
inline void set_frame(Object::Rectangle* obj, const Frame& frame) { obj->set_frame(frame); }
inline void set_frame(Object::Image& obj, const Frame& frame) { obj.set_frame(frame); }
inline void set_frame(Object::Image* obj, const Frame& frame) { obj->set_frame(frame); }
inline void set_frame(Object::Text& obj, const Frame& frame) { obj.set_frame(frame); }
inline void set_frame(Object::Text* obj, const Frame& frame) { obj->set_frame(frame); }

template <typename T>
bool valid_object(const T& obj) {
    if constexpr (std::is_pointer_v<T>) {
        return obj != nullptr;
    }
    else {
        return true;
    }
}

template <typename... Objs>
void calculate_horizontal_layout(
    std::source_location location,
    Frame parent_frame,
    Align x_align,
    Align y_align,
    float spacing,
    Objs&&... objs
) requires (
    requires(Objs& obj, Frame frame) {
        {get_frame(obj)} -> std::same_as<Frame>;
        set_frame(obj, frame);
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
        if (!(valid_object(objs) && ...)) {
            Log::logger(
                Log::Error,
                "{}:{}: Layout object is null",
                location.file_name(), location.line()
            );
            return;
        }

        float total_width = (0.0f + ... + get_frame(objs).width)
            + spacing * static_cast<float>(sizeof...(objs) - 1);

        float x = parent_frame.x;
        if (x_align == Align::Center) {
            x += (parent_frame.width - total_width) / 2;
        }
        else if (x_align == Align::Right) {
            x += parent_frame.width - total_width;
        }

        auto place = [&](auto& obj) {
            Frame frame = get_frame(obj);
            frame.x = x;
            frame.y = parent_frame.y;

            if (y_align == Align::Center) {
                frame.y += (parent_frame.height - frame.height) / 2;
            }
            else if (y_align == Align::Right) {
                frame.y += parent_frame.height - frame.height;
            }

            set_frame(obj, frame);
            x += frame.width + spacing;
        };
        (place(objs), ...);
    }
}

template <typename... Objs>
void calculate_horizontal_layout(
    std::source_location location,
    Frame,
    Align,
    Align,
    float,
    Objs&&...
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
    Objs&&... objs
) requires (
    requires(Objs& obj, Frame frame) {
        {get_frame(obj)} -> std::same_as<Frame>;
        set_frame(obj, frame);
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
        if (!(valid_object(objs) && ...)) {
            Log::logger(
                Log::Error,
                "{}:{}: Layout object is null",
                location.file_name(), location.line()
            );
            return;
        }

        float total_height = (0.0f + ... + get_frame(objs).height)
            + spacing * static_cast<float>(sizeof...(objs) - 1);

        float y = parent_frame.y;
        if (y_align == Align::Center) {
            y += (parent_frame.height - total_height) / 2;
        }
        else if (y_align == Align::Right) {
            y += parent_frame.height - total_height;
        }

        auto place = [&](auto& obj) {
            Frame frame = get_frame(obj);
            frame.y = y;
            frame.x = parent_frame.x;

            if (x_align == Align::Center) {
                frame.x += (parent_frame.width - frame.width) / 2;
            }
            else if (x_align == Align::Right) {
                frame.x += parent_frame.width - frame.width;
            }

            set_frame(obj, frame);
            y += frame.height + spacing;
        };
        (place(objs), ...);
    }
}

template <typename... Objs>
void calculate_vertical_layout(
    std::source_location location,
    Frame,
    Align,
    Align,
    float,
    Objs&&...
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
