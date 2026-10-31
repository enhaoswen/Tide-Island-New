#include "object.hpp"

#include "graphics/renderer.hpp"
#include "utils/log.hpp"

#include <memory>
#include <vector>
#include <ranges>

using namespace std;

namespace {

vector<unique_ptr<Animation> > animations;

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
