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


void Object::Detail::add_animation(const AnimationDesc animation_desc, float* const target_ptr) {

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