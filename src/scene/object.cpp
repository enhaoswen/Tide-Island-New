#include "scene/object.hpp"

#include <algorithm>
#include <chrono>
#include <memory>
#include <ranges>
#include <utility>
#include <variant>
#include <vector>

#include "app/island.hpp"
#include "backend/wayland.hpp"
#include "graphics/renderer.hpp"
#include "utils/log.hpp"

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

template <typename desc_type>
    requires (same_as<desc_type, RectDesc> ||
        same_as<desc_type, ImageDesc> ||
        same_as<desc_type, TextDesc>
    )
float* animation_target(desc_type& type, AnimationTarget target) {
    switch (target) {
    case AnimationTarget::Width:
        return &type.frame.width;
    case AnimationTarget::Height:
        return &type.frame.height;
    case AnimationTarget::X:
        return &type.frame.x;
    case AnimationTarget::Y:
        return &type.frame.y;

    case AnimationTarget::Radius:
        if constexpr (requires { type.radius; })
            return &type.radius;
        return nullptr;

    case AnimationTarget::ColorR:
    case AnimationTarget::ColorG:
    case AnimationTarget::ColorB:
    case AnimationTarget::ColorA:
        if constexpr (requires { type.color; }) {
            const auto index = static_cast<unsigned>(target)
                - static_cast<unsigned>(AnimationTarget::ColorR);
            return &type.color[index];
        }
        return nullptr;
    default:
        return nullptr;
    }
}

void add_animation(
    vector<unique_ptr<Animation> >& animations, float* target_ptr, const AnimationDesc& anim_desc) {
    auto existing = find_if(animations.begin(), animations.end(), [target_ptr](const auto& anim) {
        return anim->target_ptr == target_ptr;
    });

    if (existing != animations.end()) {
        if (!((*existing)->priority <= anim_desc.priority)) {
            return;
        }
        if ((*existing)->priority == anim_desc.priority) {
            Log::logger(Log::Warning,
                "Animation conflict: target={}, from={}, to={}",
                static_cast<const void*>((*existing)->target_ptr),
                (*existing)->from,
                (*existing)->to
                );
        }
    }

    auto animation = make_unique<Animation>();

    animation->from       = *target_ptr;
    animation->to         = anim_desc.to;
    animation->target_ptr = target_ptr;
    animation->priority   = anim_desc.priority;
    animation->duration   = anim_desc.duration;
    animation->start_time = chrono::steady_clock::now();

    if (existing != animations.end()) {
        *existing = move(animation);
    }
    else {
        animations.push_back(move(animation));
    }
}

void update(vector<unique_ptr<Animation> >& animations) {
    const auto  now   = chrono::steady_clock::now();
    const float scale = Island::state().animation_scale;

    erase_if(animations, [now, scale](const unique_ptr<Animation>& animation) {
        const float elapsed =
            chrono::duration<float, milli>(
                now - animation->start_time
                ).count();

        const float progress = animation->duration == 0
            ? 1.0f
            : clamp(
                elapsed * scale / animation->duration,
                0.0f, 1.0f
                );

        const bool finished = progress >= 1.0f;

        *animation->target_ptr = finished
            ? animation->to
            : animation->from +
            (animation->to - animation->from) * progress;

        return finished;
    });
}

} // namespace

template <typename Desc>
Object::Element<Desc>::Element(const Desc& arg_desc) : desc(arg_desc) {
}

template <typename Desc>
void Object::Element<Desc>::replace_desc(const Desc& arg_desc) {
    desc = arg_desc;
    animations.clear();
    Island::request_redraw(true);
}

template <typename Desc>
void Object::Element<Desc>::set_frame(const Frame& frame) {
    desc.frame = frame;
    Island::request_redraw(true);
}

template <typename Desc>
void Object::Element<Desc>::add_animation(AnimationDesc anim_desc, source_location l) {
    float* target_ptr = animation_target(desc, anim_desc.target);
    if (target_ptr == nullptr) {
        Log::logger(Log::Error,
            "{}:{}: Unsupported animation target, id = {}",
            l.file_name(), l.line(), static_cast<int>(anim_desc.target));
        return;
    }
    ::add_animation(animations, target_ptr, anim_desc);
    Island::request_redraw(true);
}

template <typename Desc>
void Object::Element<Desc>::update() {
    ::update(animations);
}

template <typename Desc>
bool Object::Element<Desc>::has_active_animations() const {
    return !animations.empty();
}

template class Object::Element<RectDesc>;
template class Object::Element<ImageDesc>;
template class Object::Element<TextDesc>;

void Object::DontUse::ClickableItem::set_callback_left(ClickCallback left) {
    callback_left = move(left);
}

void Object::DontUse::ClickableItem::set_callback_right(ClickCallback right) {
    callback_right = move(right);
}

void Object::Rectangle::set_desc(const RectDesc& arg_desc) {
    replace_desc(arg_desc);
}

void Object::Rectangle::set_radius(float radius) {
    desc.radius = radius;
    Island::request_redraw(true);
}

void Object::Rectangle::set_color(const Color& color) {
    desc.color = color;
    Island::request_redraw(true);
}

void Object::Rectangle::draw() {
    Renderer::draw_rectangle(desc);
}

Object::Image::Image(const ImageDesc& arg_desc) : Element(arg_desc) {
    Renderer::prepare_image(desc, texture);
}

void Object::Image::set_desc(const ImageDesc& arg_desc) {
    texture_dirty = texture_dirty || desc.path != arg_desc.path;
    replace_desc(arg_desc);
}

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

void Object::Image::draw() {
    if (texture_dirty) {
        Renderer::prepare_image(desc, texture);
        texture_dirty = false;
    }
    Renderer::draw_image(desc, texture);
}

Object::Text::Text(const TextDesc& arg_desc) : Element(arg_desc) {
    texture_scale = Wayland::get_scale();
    Renderer::prepare_text(desc, texture);
}

void Object::Text::set_desc(const TextDesc& arg_desc) {
    texture_dirty = texture_dirty || desc.text != arg_desc.text || desc.font.id != arg_desc.font.id;
    replace_desc(arg_desc);
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
    return click_object(desc.frame, desc.radius, left ? callback_left : callback_right, x, y);
}

bool Object::ClickableImage::click(float x, float y, bool left) {
    return click_object(desc.frame, desc.radius, left ? callback_left : callback_right, x, y);
}

bool Object::ClickableText::click(float x, float y, bool left) {
    return click_object(desc.frame, 0.0f, left ? callback_left : callback_right, x, y);
}

Object::Rectangle* Object::add_rectangle(const RectDesc& desc) {
    auto  object = make_unique<Rectangle>(desc);
    auto* handle = object.get();
    objects.emplace_back(move(object));
    Island::request_redraw(true);
    return handle;
}

Object::Image* Object::add_image(const ImageDesc& desc) {
    auto  object = make_unique<Image>(desc);
    auto* handle = object.get();
    objects.emplace_back(move(object));
    Island::request_redraw(true);
    return handle;
}

Object::Text* Object::add_text(const TextDesc& desc) {
    auto  object = make_unique<Text>(desc);
    auto* handle = object.get();
    objects.emplace_back(move(object));
    Island::request_redraw(true);
    return handle;
}

Object::ClickableRect* Object::add_clickable_rect(const RectDesc& desc) {
    auto  object = make_unique<ClickableRect>(desc);
    auto* handle = object.get();
    objects.emplace_back(move(object));
    Island::request_redraw(true);
    return handle;
}

Object::ClickableImage* Object::add_clickable_image(const ImageDesc& desc) {
    auto  object = make_unique<ClickableImage>(desc);
    auto* handle = object.get();
    objects.emplace_back(move(object));
    Island::request_redraw(true);
    return handle;
}

Object::ClickableText* Object::add_clickable_text(const TextDesc& desc) {
    auto  object = make_unique<ClickableText>(desc);
    auto* handle = object.get();
    objects.emplace_back(move(object));
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
            item->draw();
        }, object);
    }
}

void Object::update() {
    for (auto& object : objects) {
        visit([](const auto& item) { item->update(); }, object);
    }
}

bool Object::has_active_animations() {
    return ranges::any_of(objects, [](const auto& object) {
        return visit([](const auto& item) { return item->has_active_animations(); }, object);
    });
}

void Object::clear() {
    objects.clear();
    Island::request_redraw(true);
}