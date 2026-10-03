#include "scene/object.hpp"
#include "utils/log.hpp"
#include "graphics/renderer.hpp"
#include "utils/struct.hpp"

#include <source_location>
#include <type_traits>
#include <vector>
#include <algorithm>
#include <chrono>
#include <variant>

using namespace std;
using namespace std::chrono;

namespace {

template <typename T>
class Item{
private:
    T desc;
    vector<Animation> animations;

    float* get_target_ptr(AnimationTarget target, source_location l = source_location::current()) {
        switch (target) {
            case AnimationTarget::Width:
                if constexpr (requires { desc.frame; }) {
                    return &desc.frame.width;
                }
                break;

            case AnimationTarget::Height:
                if constexpr (requires { desc.frame; }) {
                    return &desc.frame.height;
                }
                break;

            case AnimationTarget::X:
                if constexpr (requires { desc.frame; }) {
                    return &desc.frame.x;
                }
                break;

            case AnimationTarget::Y:
                if constexpr (requires { desc.frame; }) {
                    return &desc.frame.y;
                }
                break;

            case AnimationTarget::Radius:
                if constexpr (requires { desc.radius; }) {
                    return &desc.radius;
                }
                break;

            case AnimationTarget::ColorR:
                if constexpr (requires { desc.color; }) {
                    return &desc.color.to_rgba()[0];
                }
                break;

            case AnimationTarget::ColorG:
                if constexpr (requires { desc.color; }) {
                    return &desc.color.to_rgba()[1];
                }
                break;

            case AnimationTarget::ColorB:
                if constexpr (requires { desc.color; }) {
                    return &desc.color.to_rgba()[2];
                }
                break;

            case AnimationTarget::ColorA:
                if constexpr (requires { desc.color; }) {
                    return &desc.color.to_rgba()[3];
                }
                break;

            default:
                Log::logger(
                    Log::Error,
                    "{}:{}: Unknown animation target {}",
                    l.file_name(),
                    l.column(),
                    static_cast<int>(target)
                );
                return nullptr;
        }

        Log::logger(
            Log::Error,
            "{}:{}: Animation target {} is not supported for type {}",
            l.file_name(),
            l.column(),
            static_cast<int>(target),
            Log::get_type_name<T>()
        );

        return nullptr;
}

public:

    Item(const T& arg_desc) : desc(arg_desc) {}

    void set_desc(const T& arg_desc) {
        desc = arg_desc;
    }

    void set_frame(const Frame& arg_frame) {
        desc.frame = arg_frame;
    }

    void set_radius(float arg_radius) 
    requires requires { desc.radius = arg_radius;} {
        desc.radius = arg_radius;
    }

    void set_color(const Color& arg_color)
    requires requires { desc.color = arg_color;} {
        desc.color = arg_color;
    }

    void set_text(string_view arg_text)
    requires requires { desc.text = arg_text;} {
        desc.text = arg_text;
    }

    void set_path(const string& arg_path)
    requires requires { desc.path = arg_path;} {
        desc.path = arg_path;
    }


    void draw(source_location l = source_location::current()) {
        if constexpr (is_same_v<T, RectDesc>) {
            Renderer::draw_rectangle(desc.frame, desc.radius, desc.color.to_rgba());
        }
        else if constexpr (is_same_v<T, ImageDesc>) {
            Renderer::draw_image(desc.frame, desc.horizontal_align, desc.vertical_align, desc.radius, desc.path);
        }
        else if constexpr (is_same_v<T, TextDesc>) {
            Renderer::draw_text(desc.frame, desc.horizontal_align, desc.vertical_align, desc.text);
        }

        else {
            Log::logger(
                Log::Error, 
                "{}:{}: Unsupported type. type={}",
                l.file_name(),
                l.column(),
                Log::get_type_name<T>()
            );
        }
    }

    void update() {
        auto now = steady_clock::now();

        for (auto it = animations.begin(); it != animations.end();) {

            auto& animation = *it;

            if (now >= animation.start_time + animation.duration) {
                *animation.target_ptr = animation.to;
                it = animations.erase(it);
                continue;
            }

            float progress = duration<float>(now - animation.start_time).count()
                / duration<float>(animation.duration).count();

            *animation.target_ptr = animation.from + (animation.to - animation.from) * progress;
            ++it;
        }
    }

    bool click(int x, int y, bool left)
    requires requires {desc.radius = 1;} {
        if (
            x < desc.frame.x ||
            x > desc.frame.x + desc.frame.width ||
            y < desc.frame.y ||
            y > desc.frame.y + desc.frame.height
        ) {
            return false;
        }

        float nearest_x = clamp(
            static_cast<float>(x),
            desc.frame.x + desc.radius,
            desc.frame.x + desc.frame.width - desc.radius
        );

        float nearest_y = clamp(
            static_cast<float>(y),
            desc.frame.y + desc.radius,
            desc.frame.y + desc.frame.height - desc.radius
        );

        float dx = x - nearest_x;
        float dy = y - nearest_y;

        if (dx * dx + dy * dy <= desc.radius * desc.radius) {

            if (left && desc.click_callback_left) {
                desc.click_callback_left();
            } else if (!left && desc.click_callback_right) {
                desc.click_callback_right();
            }
            return true;
        }

        return false;
    }

    bool click(int x, int y, bool left) {
        if (
            x < desc.frame.x ||
            x > desc.frame.x + desc.frame.width ||
            y < desc.frame.y ||
            y > desc.frame.y + desc.frame.height
        ) {
            return false;
        }

        if (left && desc.click_callback_left) {
            desc.click_callback_left();
        } else if (!left && desc.click_callback_right) {
            desc.click_callback_right();
        }
        return true;
    }

    void add_animation(Animation animation, source_location l = source_location::current()) {
        animation.target_ptr = get_target_ptr(animation.target, l);
        if (animation.target_ptr) {
            animations.push_back(animation);
        }
    }

};

vector<variant<Item<RectDesc>, Item<ImageDesc>, Item<TextDesc>>> objects;

} // namespace

void Object::add_rectangle(RectDesc& desc) {
    objects.emplace_back(Item<RectDesc>(desc));
}

void Object::add_image(ImageDesc &desc) {
    objects.emplace_back(Item<ImageDesc>(desc));
}

void Object::add_text(TextDesc &desc) {
    objects.emplace_back(Item<TextDesc>(desc));
}

void Object::click(float x, float y, bool left) {
    for (auto& object : objects) {
        bool handled = std::visit(
            [&](auto& obj) {
                return obj.click(x, y, left);
            },
            object
        );

        if (handled) {
            return;
        }
    }
}

void Object::draw() {
    for (auto& object : objects) {
        std::visit(
            [](auto& obj) {
                obj.update();
                obj.draw();
            },
            object
        );
    }
}

void Object::clear() {
    objects.clear();
}
