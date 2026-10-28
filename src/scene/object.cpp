#include "object.hpp"

#include "utils/log.hpp"

#include <memory>
#include <vector>

using namespace std;

namespace {

vector<unique_ptr<Animation> > animations;

}

// BASE

template <typename T>
Object::Base<T>::Base(const T& arg_desc) {
    desc = arg_desc;
}

template <typename T>
float* Object::Base<T>::animation_target(AnimationTarget target) {
    switch (target) {
    case AnimationTarget::Width:
        return &desc.frame.width;
    case AnimationTarget::Height:
        return &desc.frame.height;
    case AnimationTarget::X:
        return &desc.frame.x;
    case AnimationTarget::Y:
        return &desc.frame.y;

    case AnimationTarget::Radius:
        if constexpr (requires { desc.radius; })
            return &desc.radius;
        return nullptr;

    case AnimationTarget::ColorR:
    case AnimationTarget::ColorG:
    case AnimationTarget::ColorB:
    case AnimationTarget::ColorA:
        if constexpr (requires { desc.color; }) {
            const auto index = static_cast<unsigned>(target)
                - static_cast<unsigned>(AnimationTarget::ColorR);
            return &desc.color[index];
        }
        return nullptr;
    default:
        return nullptr;
    }
}

template <typename T>
void Object::Base<T>::add_animation(const AnimationDesc animation_desc) {

    float* target_ptr = animation_target(animation_desc.target);

    const auto existing = find_if(
        animations.begin(),
        animations.end(),
        [target_ptr](const auto& anim) {
            return anim->target_ptr == target_ptr;
        });

    if (existing != animations.end()) {
        if (!((*existing)->priority <= animation_desc.priority)) {
            return;
        }
        if ((*existing)->priority == animation_desc.priority) {
            Log::logger(Log::Warning,
                "Animation conflict: target={}, from={}, to={}",
                static_cast<const void*>(target_ptr),
                (*existing)->from,
                (*existing)->to
                );
        }
    }

    auto animation = make_unique<Animation>();

    animation->from       = *target_ptr;
    animation->to         = animation_desc.to;
    animation->target_ptr = target_ptr;
    animation->priority   = animation_desc.priority;
    animation->duration   = animation_desc.duration;
    animation->start_time = chrono::steady_clock::now();

    if (existing != animations.end()) {
        *existing = move(animation);
    }
    else {
        animations.push_back(move(animation));
    }
}

template <typename T>
void Object::Base<T>::set_radius(float radius, source_location)
    requires Object::has_radius<T> {
    desc.radius = radius;
}

template <typename T>
void Object::Base<T>::set_radius(float, source_location l) {
    Log::logger(
        Log::Warning,
        "{}:{}: {} does not support function set_radius",
        l.file_name(),
        l.line(),
        desc.radius, Log::get_type_name<T>()
        );
}

template <typename T>
void Object::Base<T>::set_color(Color color, source_location)
    requires Object::has_color<T> {
    desc.color = color;
}

template <typename T>
void Object::Base<T>::set_color(Color, source_location l) {
    Log::logger(
        Log::Warning,
        "{}:{}: {} does not support function set_color",
        l.file_name(),
        l.line(),
        desc.radius, Log::get_type_name<T>()
        );
}

template <typename T, typename... Features>
    requires Object::is_element<T>
bool Object::Element<T, Features...>::click(float x, float y, bool left)
    requires (requires(Features f) { f.lcb; } && ...) {

    if (desc.frame.width <= 0 || desc.frame.height <= 0 ||
        x < desc.frame.x || x > desc.frame.x + desc.frame.width ||
        y < desc.frame.y || y > desc.frame.y + desc.frame.height) {
        return false;
        }
    const float corner_radius{clamp(radius, 0.0f, max(0.0f, min(desc.frame.width, desc.frame.height) / 2))};
    const float dx{x - clamp(x, desc.frame.x + corner_radius, desc.frame.x + desc.frame.width - corner_radius)};
    const float dy{y - clamp(y, desc.frame.y + corner_radius, desc.frame.y + desc.frame.height - corner_radius)};
    if (dx * dx + dy * dy > corner_radius * corner_radius || !callback) {
        return false;
    }
    callback();
    return true;
}