#include "animation.hpp"
#include "utils/log.hpp"

#include <chrono>
#include <memory>
#include <vector>

using namespace std;

namespace {

// animations only saves animation will start immediately
vector<unique_ptr<Animation> > animations;

}

// might be nullptr
Animation* AnimationManager::add_animation(const AnimationDesc& anim_desc) {
    for (auto& anim : animations) {

        if (anim->target_ptr == anim_desc.target_ptr) {
            if (anim->priority <= anim_desc.priority) {

                if (anim->priority == anim_desc.priority) {
                    Log::logger(Log::Warning,
                        "Animation conflict: target={}, from={}, to={}",
                        static_cast<const void*>(anim->target_ptr),
                        anim->from,
                        anim->to
                        );
                }

                auto result_anim = make_unique<Animation>();

                result_anim->from       = *anim_desc.target_ptr;
                result_anim->to         = anim_desc.to;
                result_anim->target_ptr = anim_desc.target_ptr;
                result_anim->priority   = anim_desc.priority;
                result_anim->duration   = anim_desc.duration;
                result_anim->start_time = chrono::steady_clock::now();

                anim = move(result_anim);
                return anim.get();
            }

            return nullptr;
        }
    }

    auto animation = make_unique<Animation>();

    animation->from       = *anim_desc.target_ptr;
    animation->to         = anim_desc.to;
    animation->target_ptr = anim_desc.target_ptr;
    animation->priority   = anim_desc.priority;
    animation->duration   = anim_desc.duration;
    animation->start_time = chrono::steady_clock::now();

    animations.push_back(move(animation));
    return animations.back().get();
}

void AnimationManager::remove_animation(Animation* arg_anim) {
    if (arg_anim == nullptr)
        return;

    erase_if(animations, [arg_anim](const auto& anim) {
        return anim.get() == arg_anim;
    });
}

void AnimationManager::update() {
    const auto now = chrono::steady_clock::now();
    erase_if(animations, [now](const unique_ptr<Animation>& animation) {
        const auto elapsed  = chrono::duration<float, milli>(now - animation->start_time).count();
        const bool finished = elapsed >= animation->duration;

        const float progress = finished
            ? 1.0
            : clamp(
                elapsed / animation->duration,
                0.0f, 1.0f);

        *animation->target_ptr = finished
            ? animation->to
            : animation->from + (animation->to - animation->from) * progress;
        return finished;
    });
}

void AnimationManager::clear() {
    animations.clear();
}