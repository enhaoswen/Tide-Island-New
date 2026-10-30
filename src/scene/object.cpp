#include "object.hpp"

#include "graphics/renderer.hpp"
#include "utils/log.hpp"

#include <memory>
#include <vector>
#include <ranges>

using namespace std;

namespace {

vector<unique_ptr<Animation> > animations;
vector<Object::Handle>         objects;

}

// BASE

template <typename T>
Object::Detail::Base<T>::Base(const T& arg_desc) {
    info = arg_desc;
}

template <typename T>
pair<float, float> Object::Detail::Base<T>::actual_size()
    requires requires(T t) { { t.info.fix } -> same_as<bool&>; } {
    return {this->info.frame.x, this->info.frame.y};
}

template <typename T>
float* Object::Detail::Base<T>::animation_target(AnimationTarget target) {
    switch (target) {
    case AnimationTarget::Width:
        return &info.frame.width;
    case AnimationTarget::Height:
        return &info.frame.height;
    case AnimationTarget::X:
        return &info.frame.x;
    case AnimationTarget::Y:
        return &info.frame.y;

    case AnimationTarget::Radius:
        if constexpr (requires { info.radius; })
            return &info.radius;
        return nullptr;

    case AnimationTarget::ColorR:
    case AnimationTarget::ColorG:
    case AnimationTarget::ColorB:
    case AnimationTarget::ColorA:
        if constexpr (requires { info.color; }) {
            const auto index = static_cast<unsigned>(target)
                - static_cast<unsigned>(AnimationTarget::ColorR);
            return &info.color[index];
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
    info.radius = radius;
}

template <typename T>
void Object::Detail::Base<T>::set_radius(float, source_location l) {
    Log::logger(
        Log::Warning,
        "{}:{}: {} does not support function set_radius",
        l.file_name(),
        l.line(),
        info.radius, Log::get_type_name<T>()
        );
}

template <typename T>
void Object::Detail::Base<T>::set_color(Color color, source_location)
    requires Object::Detail::has_color<T> {
    info.color = color;
}

template <typename T>
void Object::Detail::Base<T>::set_color(Color, source_location l) {
    Log::logger(
        Log::Warning,
        "{}:{}: {} does not support function set_color",
        l.file_name(),
        l.line(),
        info.radius, Log::get_type_name<T>()
        );
}

template <typename T, typename... Features>
    requires Object::Detail::is_element<T>
bool Object::Detail::Element<T, Features...>::click(float x, float y, bool left)
    requires (requires(Features f) { f.lcb; } && ...) {

    if (this->info.frame.width <= 0 || this->info.frame.height <= 0 ||
        x < this->info.frame.x || x > this->info.frame.x + this->info.frame.width ||
        y < this->info.frame.y || y > this->info.frame.y + this->info.frame.height) {
        return false;
    }
    const float corner_radius{clamp(this->radius, 0.0f, max(0.0f, min(this->info.frame.width, this->info.frame.height) / 2))};
    const float dx{x - clamp(x, this->info.frame.x + corner_radius, this->info.frame.x + this->info.frame.width - corner_radius)};
    const float dy{y - clamp(y, this->info.frame.y + corner_radius, this->info.frame.y + this->info.frame.height - corner_radius)};
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

void Object::Layout::update() {
    uint16_t len = 0;
    for (Object::Handle id : this->info.children) {
        len += id
    }
}