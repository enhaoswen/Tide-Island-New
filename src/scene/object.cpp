#include "object.hpp"

#include "graphics/renderer.hpp"
#include "utils/log.hpp"

#include <memory>
#include <vector>

using namespace std;

namespace {

vector<unique_ptr<Animation> > animations;
vector<Object::Handle>         objects;

}

// BASE

template <typename T>
Object::Detail::Base<T>::Base(const T& arg_desc) {
    desc = arg_desc;
}

template <typename T>
pair<float, float> Object::Detail::Base<T>::actual_size()
    requires requires(T t) { { t.desc.fix } -> same_as<bool&>; } {

    if constexpr (same_as<T, Object::Image>) {

    }

    else if constexpr (same_as<T, Object::Text>) {

    }

    else if constexpr (same_as<T, Object::Layout>) {

    }

}

template <typename T>
float* Object::Detail::Base<T>::animation_target(AnimationTarget target) {
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
void Object::Detail::Base<T>::add_animation(const AnimationDesc animation_desc) {

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
void Object::Detail::Base<T>::set_radius(float radius, source_location)
    requires Object::Detail::has_radius<T> {
    desc.radius = radius;
}

template <typename T>
void Object::Detail::Base<T>::set_radius(float, source_location l) {
    Log::logger(
        Log::Warning,
        "{}:{}: {} does not support function set_radius",
        l.file_name(),
        l.line(),
        desc.radius, Log::get_type_name<T>()
        );
}

template <typename T>
void Object::Detail::Base<T>::set_color(Color color, source_location)
    requires Object::Detail::has_color<T> {
    desc.color = color;
}

template <typename T>
void Object::Detail::Base<T>::set_color(Color, source_location l) {
    Log::logger(
        Log::Warning,
        "{}:{}: {} does not support function set_color",
        l.file_name(),
        l.line(),
        desc.radius, Log::get_type_name<T>()
        );
}

template <typename T, typename... Features>
    requires Object::Detail::is_element<T>
bool Object::Detail::Element<T, Features...>::click(float x, float y, bool left)
    requires (requires(Features f) { f.lcb; } && ...) {

    if (this->desc.frame.width <= 0 || this->desc.frame.height <= 0 ||
        x < this->desc.frame.x || x > this->desc.frame.x + this->desc.frame.width ||
        y < this->desc.frame.y || y > this->desc.frame.y + this->desc.frame.height) {
        return false;
    }
    const float corner_radius{clamp(this->radius, 0.0f, max(0.0f, min(this->desc.frame.width, this->desc.frame.height) / 2))};
    const float dx{x - clamp(x, this->desc.frame.x + corner_radius, this->desc.frame.x + this->desc.frame.width - corner_radius)};
    const float dy{y - clamp(y, this->desc.frame.y + corner_radius, this->desc.frame.y + this->desc.frame.height - corner_radius)};
    if (dx * dx + dy * dy > corner_radius * corner_radius) {
        return false;
    }

    if (left && this->lcb) {
        this->lcb();
        return true;
    }
    if (!left && this->rcb) {
        this->rcb();
        return true;
    }

    return false;
}

Object::Handle Object::add_rectangle(RectDesc desc) {

}