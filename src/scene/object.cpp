#include "scene/object.hpp"
#include "app/island.hpp"
#include "backend/wayland.hpp"
#include "graphics/renderer.hpp"
#include "utils/log.hpp"

#include <algorithm>
#include <chrono>
#include <memory>
#include <ranges>
#include <utility>
#include <variant>
#include <vector>

using namespace std;
using namespace std::chrono;

namespace {

using SceneObject = variant<
    unique_ptr<Object::Rectangle>,
    unique_ptr<Object::Image>,
    unique_ptr<Object::Text>,
    unique_ptr<Object::ClickableRect>,
    unique_ptr<Object::ClickableImage>,
    unique_ptr<Object::ClickableText> >;
vector<SceneObject> objects;

float* animation_target(Frame& frame, float* radius, Color* color, AnimationTarget target) {
    switch (target) {
    case AnimationTarget::Width:
        return &frame.width;
    case AnimationTarget::Height:
        return &frame.height;
    case AnimationTarget::X:
        return &frame.x;
    case AnimationTarget::Y:
        return &frame.y;
    case AnimationTarget::Radius:
        return radius;
    case AnimationTarget::ColorR:
    case AnimationTarget::ColorG:
    case AnimationTarget::ColorB:
    case AnimationTarget::ColorA:
        if (color) {
            const auto index = static_cast<unsigned>(target)
                - static_cast<unsigned>(AnimationTarget::ColorR);
            return &(*color)[index];
        }
        return nullptr;
    default:
        return nullptr;
    }
}

void advance_animations(vector<Animation>& animations, steady_clock::time_point now) {
    erase_if(animations, [now](const Animation& animation) {
        const auto elapsed  = now - animation.start_time;
        const bool finished = animation.duration <= milliseconds::zero()
            || elapsed >= animation.duration;
        const float progress = finished
            ? 1.0f
            : clamp(
                duration<float>(elapsed).count() / duration<float>(animation.duration).count(),
                0.0f, 1.0f);
        *animation.target_ptr = finished
            ? animation.to
            : animation.from + (animation.to - animation.from) * progress;
        return finished;
    });
}

void start_animation(
    vector<Animation>& animations, Frame&         frame, float* radius, Color* color,
    AnimationDesc      anim_desc, source_location location
    ) {

    float* target = animation_target(frame, radius, color, anim_desc.target);
    if (!target) {
        Log::logger(
            Log::Error,
            "{}:{}: Animation target {} is not supported for this object",
            location.file_name(), location.line(), static_cast<int>(anim_desc.target));
        return;
    }

    const auto now = steady_clock::now();
    if (!animations.empty()) {
        advance_animations(animations, now);
        Island::request_redraw(true);
    }
    auto existing = ranges::find(animations, anim_desc.target, &Animation::target);
    if (existing != animations.end() && existing->priority > anim_desc.priority) {
        return;
    }

    Animation animation{
        .from       = *target,
        .to         = anim_desc.to,
        .target_ptr = target,
        .target     = anim_desc.target,
        .priority   = anim_desc.priority,
        .duration   = anim_desc.duration,
        .start_time = now,
    };

    if (existing != animations.end()) {
        *existing = animation;
    }
    else {
        animations.push_back(animation);
    }
    Island::request_redraw(true);
}

void update_animations(vector<Animation>& animations) {
    if (animations.empty()) {
        return;
    }
    advance_animations(animations, steady_clock::now());
    Island::request_redraw(true);
}

bool click_object(const Frame& frame, float radius, const ClickCallback& callback, float x, float y) {
    if (x < frame.x || x > frame.x + frame.width ||
        y < frame.y || y > frame.y + frame.height) {
        return false;
    }
    const float corner_radius = clamp(radius, 0.0f, max(0.0f, min(frame.width, frame.height) / 2));
    const float dx            = x - clamp(x, frame.x + corner_radius, frame.x + frame.width - corner_radius);
    const float dy            = y - clamp(y, frame.y + corner_radius, frame.y + frame.height - corner_radius);
    if (dx * dx + dy * dy > corner_radius * corner_radius || !callback) {
        return false;
    }
    callback();
    return true;
}

} // namespace

void Object::DontUse::ClickableItem::set_callback_left(ClickCallback left) {
    callback_left = std::move(left);
}

void Object::DontUse::ClickableItem::set_callback_right(ClickCallback right) {
    callback_right = std::move(right);
}

Object::Rectangle::Rectangle(const RectDesc& arg_desc) : desc(arg_desc) {
    desc.animations.clear();
}

const Frame& Object::Rectangle::frame() const { return desc.frame; }

void Object::Rectangle::set_frame(const Frame& frame) {
    desc.frame = frame;
    Island::request_redraw(true);
}

void Object::Rectangle::set_desc(const RectDesc& arg_desc) {
    desc = arg_desc;
    desc.animations.clear();
    Island::request_redraw(true);
}

float Object::Rectangle::radius() const { return desc.radius; }

void Object::Rectangle::set_radius(float radius) {
    desc.radius = radius;
    Island::request_redraw(true);
}

void Object::Rectangle::set_color(const Color& color) {
    desc.color = color;
    Island::request_redraw(true);
}

void Object::Rectangle::add_animation(AnimationDesc anim_desc, source_location location) {
    start_animation(desc.animations, desc.frame, &desc.radius, &desc.color, anim_desc, location);
}

void Object::Rectangle::update() {
    update_animations(desc.animations);
}

void Object::Rectangle::draw() {
    Renderer::draw_rectangle(desc);
}

Object::Image::Image(const ImageDesc& arg_desc) : desc(arg_desc) {
    // Animation pointers from the descriptor belong to another object.
    desc.animations.clear();
    Renderer::prepare_image(desc, texture);
}

const Frame& Object::Image::frame() const { return desc.frame; }

void Object::Image::set_frame(const Frame& frame) {
    desc.frame = frame;
    Island::request_redraw(true);
}

void Object::Image::set_desc(const ImageDesc& arg_desc) {
    texture_dirty = texture_dirty || desc.path != arg_desc.path;
    desc          = arg_desc;
    desc.animations.clear();
    Island::request_redraw(true);
}

float Object::Image::radius() const { return desc.radius; }

void Object::Image::set_radius(float radius) {
    desc.radius = radius;
    Island::request_redraw(true);
}

void Object::Image::set_path(string_view path) {
    if (desc.path == path) {
        return;
    }
    desc.path = path;
    reload();
}

void Object::Image::reload() {
    texture_dirty = true;
    Island::request_redraw(true);
}

void Object::Image::add_animation(AnimationDesc anim_desc, source_location location) {
    start_animation(desc.animations, desc.frame, &desc.radius, nullptr, anim_desc, location);
}

void Object::Image::update() {
    update_animations(desc.animations);
}

void Object::Image::draw() {
    if (texture_dirty) {
        Renderer::prepare_image(desc, texture);
        texture_dirty = false;
    }
    Renderer::draw_image(desc, texture);
}

Object::Text::Text(const TextDesc& arg_desc) : desc(arg_desc) {
    // Animation pointers from the descriptor belong to another object.
    desc.animations.clear();
    texture_scale = Wayland::get_scale();
    Renderer::prepare_text(desc, texture);
}

const Frame& Object::Text::frame() const { return desc.frame; }

void Object::Text::set_frame(const Frame& frame) {
    desc.frame = frame;
    Island::request_redraw(true);
}

void Object::Text::set_desc(const TextDesc& arg_desc) {
    texture_dirty = texture_dirty || desc.text != arg_desc.text || desc.font.id != arg_desc.font.id;
    desc          = arg_desc;
    desc.animations.clear();
    Island::request_redraw(true);
}

void Object::Text::set_color(const Color& color) {
    desc.color = color;
    Island::request_redraw(true);
}

void Object::Text::set_text(string_view text) {
    if (desc.text == text) {
        return;
    }
    desc.text     = text;
    texture_dirty = true;
    Island::request_redraw(true);
}

void Object::Text::set_font(FontHandle font) {
    if (desc.font.id == font.id) {
        return;
    }
    desc.font     = font;
    texture_dirty = true;
    Island::request_redraw(true);
}

void Object::Text::add_animation(AnimationDesc anim_desc, source_location location) {
    start_animation(desc.animations, desc.frame, nullptr, &desc.color, anim_desc, location);
}

void Object::Text::update() {
    update_animations(desc.animations);
}

void Object::Text::draw() {
    const float scale = Wayland::get_scale();
    if (texture_dirty || texture_scale != scale) {
        Renderer::prepare_text(desc, texture);
        texture_dirty = false;
        texture_scale = scale;
    }
    Renderer::draw_text(desc, texture);
}

bool Object::ClickableRect::click(float x, float y, bool left) {
    return click_object(frame(), radius(), left ? callback_left : callback_right, x, y);
}

bool Object::ClickableImage::click(float x, float y, bool left) {
    return click_object(frame(), radius(), left ? callback_left : callback_right, x, y);
}

bool Object::ClickableText::click(float x, float y, bool left) {
    return click_object(frame(), 0.0f, left ? callback_left : callback_right, x, y);
}

Object::Rectangle* Object::add_rectangle(const RectDesc& desc) {
    auto  object = make_unique<Rectangle>(desc);
    auto* handle = object.get();
    objects.emplace_back(std::move(object));
    Island::request_redraw(true);
    return handle;
}

Object::Image* Object::add_image(const ImageDesc& desc) {
    auto  object = make_unique<Image>(desc);
    auto* handle = object.get();
    objects.emplace_back(std::move(object));
    Island::request_redraw(true);
    return handle;
}

Object::Text* Object::add_text(const TextDesc& desc) {
    auto  object = make_unique<Text>(desc);
    auto* handle = object.get();
    objects.emplace_back(std::move(object));
    Island::request_redraw(true);
    return handle;
}

Object::ClickableRect* Object::add_clickable_rect(const RectDesc& desc) {
    auto  object = make_unique<ClickableRect>(desc);
    auto* handle = object.get();
    objects.emplace_back(std::move(object));
    Island::request_redraw(true);
    return handle;
}

Object::ClickableImage* Object::add_clickable_image(const ImageDesc& desc) {
    auto  object = make_unique<ClickableImage>(desc);
    auto* handle = object.get();
    objects.emplace_back(std::move(object));
    Island::request_redraw(true);
    return handle;
}

Object::ClickableText* Object::add_clickable_text(const TextDesc& desc) {
    auto  object = make_unique<ClickableText>(desc);
    auto* handle = object.get();
    objects.emplace_back(std::move(object));
    Island::request_redraw(true);
    return handle;
}

void Object::click(float x, float y, bool left) {
    for (auto& object : views::reverse(objects)) {
        if (visit([=](const auto& item) {
            if constexpr (requires { item->click(x, y, left); }) {
                return item->click(x, y, left);
            }
            else {
                return false;
            }
        }, object)) {
            return;
        }
    }
}

void Object::draw() {
    for (auto& object : objects) {
        visit([](const auto& item) {
            item->update();
            item->draw();
        }, object);
    }
}

void Object::clear() {
    objects.clear();
    Island::request_redraw(true);
}
