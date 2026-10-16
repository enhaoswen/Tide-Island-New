#include "scene/object.hpp"
#include "app/island.hpp"
#include "graphics/renderer.hpp"
#include "utils/log.hpp"
#include "utils/struct.hpp"

#include <algorithm>
#include <variant>
#include <chrono>
#include <memory>
#include <ranges>

using namespace std;
using namespace std::chrono;
using Objects = variant<Object<RectDesc>, Object<ImageDesc>, Object<TextDesc>>;

namespace {

vector<unique_ptr<Objects>> objects;

bool is_color_target(AnimationTarget target) {
    return target >= AnimationTarget::ColorR && target <= AnimationTarget::ColorA;
}

} // namespace

template<typename desc_type>
requires is_desc<desc_type>
Object<desc_type>::Object(desc_type arg_desc) {
    desc = arg_desc;

    if constexpr (is_same_v<desc_type, ImageDesc>) {
        Renderer::prepare_image(arg_desc, texture);
    }

    else if constexpr (is_same_v<desc_type, TextDesc>) {
        Renderer::prepare_text(arg_desc, texture);
    }
}

template<typename desc_type>
requires is_desc<desc_type>
void Object<desc_type>::add_animation(AnimationDesc anim_desc, source_location location) {
    Animation animation;
    animation.target_ptr = nullptr;
    switch (anim_desc.target) {
    case AnimationTarget::Width:
        animation.target_ptr = &desc.frame.width;
        break;
    case AnimationTarget::Height:
        animation.target_ptr = &desc.frame.height;
        break;
    case AnimationTarget::X:
        animation.target_ptr = &desc.frame.x;
        break;
    case AnimationTarget::Y:
        animation.target_ptr = &desc.frame.y;
        break;
    case AnimationTarget::Radius:
        animation.target_ptr = &desc.radius;
        break;
    default:
        break;
    }

    if (!animation.target_ptr && !(is_color_target(anim_desc.target) && requires {{desc.color} -> same_as<Color>;})) {
        Log::logger(
            Log::Error,
            "{}:{}: Animation target {} is not supported for this object",
            location.file_name(), location.line(), static_cast<int>(anim_desc.target));
        return;
    }

    animation.target = anim_desc.target;
    if (animation.target_ptr) {
        animation.from = *animation.target_ptr;
    }
    else {
        auto rgba = color()->to_rgba();
        auto index = static_cast<unsigned>(animation.target) - static_cast<unsigned>(AnimationTarget::ColorR);
        animation.from = rgba[index];
    }
    animation.to = anim_desc.to;
    animation.duration = anim_desc.duration;
    animation.priority = anim_desc.priority;
    animation.start_time = chrono::system_clock::now();
    desc.animations.push_back(animation);
    Island::request_redraw(true);
}

template<typename desc_type>
requires is_desc<desc_type>
void Object<desc_type>::update() {
    if (desc.animations.empty()) {
        return;
    }

    auto now = system_clock::now();
    for (auto it = desc.animations.begin(); it != desc.animations.end();) {
        bool finished = now >= it->start_time + it->duration;
        float progress = finished ? 1.0f : clamp(duration<float>(now - it->start_time).count() / duration<float>(it->duration).count(), 0.0f, 1.0f);
        float value = finished ? it->to : it->from + (it->to - it->from) * progress;

        if (it->target_ptr) {
            *it->target_ptr = value;
        }
        else {
            Color* target = color();
            auto rgba = target->to_rgba();
            rgba[static_cast<unsigned>(it->target) - static_cast<unsigned>(AnimationTarget::ColorR)] = value;
            *target = Color{rgba};
        }

        if (finished) {
            it = desc.animations.erase(it);
        }
        else {
            ++it;
        }
    }
    Island::request_redraw(true);
}


// till here

float* Object::ItemBase::radius() {
    return nullptr;
}

Color* Object::ItemBase::color() {
    return nullptr;
}

void Object::ItemBase::set_click_callback_left(void (*left)()) {
    mutable_click_callback(true) = left;
}

void Object::ItemBase::set_click_callback_right(void (*right)()) {
    mutable_click_callback(false) = right;
}

bool Object::ItemBase::click(float x, float y, bool left) {
    const Frame& bounds = frame();
    if (
        x < bounds.x || x > bounds.x + bounds.width ||
        y < bounds.y || y > bounds.y + bounds.height) {
        return false;
    }

    const float* current_radius = radius();
    const float corner_radius = clamp(
        current_radius ? *current_radius : 0.0f,
        0.0f, max(0.0f, min(bounds.width, bounds.height) / 2));
    const float dx = x - clamp(x, bounds.x + corner_radius, bounds.x + bounds.width - corner_radius);
    const float dy = y - clamp(y, bounds.y + corner_radius, bounds.y + bounds.height - corner_radius);
    if (dx * dx + dy * dy > corner_radius * corner_radius) {
        return false;
    }

    auto callback = mutable_click_callback(left);
    if (!callback) {
        return false;
    }

    callback();
    return true;
}

void Object::ItemBase::set_frame(const Frame& frame) {
    mutable_frame() = frame;
    Island::request_redraw(true);
}

Object::Rectangle::Rectangle(const RectDesc& desc) : desc(desc) {}

Frame& Object::Rectangle::mutable_frame() { return desc.frame; }
Object::ItemBase::ClickCallback& Object::Rectangle::mutable_click_callback(bool left) {
    return left ? desc.click_callback_left : desc.click_callback_right;
}
const Frame& Object::Rectangle::frame() const { return desc.frame; }
float* Object::Rectangle::radius() { return &desc.radius; }
Color* Object::Rectangle::color() { return &desc.color; }

void Object::Rectangle::set_desc(const RectDesc& arg_desc) {
    desc = arg_desc;
    Island::request_redraw(true);
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

Object::Image::Image(const ImageDesc& desc) : desc(desc) {}

Frame& Object::Image::mutable_frame() { return desc.frame; }
Object::ItemBase::ClickCallback& Object::Image::mutable_click_callback(bool left) {
    return left ? desc.click_callback_left : desc.click_callback_right;
}
const Frame& Object::Image::frame() const { return desc.frame; }
float* Object::Image::radius() { return &desc.radius; }
    }
    desc = arg_desc;

void Object::Image::set_desc(const ImageDesc& arg_desc) {
    if (desc.path !=arg_desc.path) {
        texture_dirty = true;
    }
    desc = arg_desc;
    Island::request_redraw(true);
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

Object::Text::Text(const TextDesc& desc) : desc(desc) {}

Frame& Object::Text::mutable_frame() { return desc.frame; }
Object::ItemBase::ClickCallback& Object::Text::mutable_click_callback(bool left) {
    return left ? desc.click_callback_left : desc.click_callback_right;
}
const Frame& Object::Text::frame() const { return desc.frame; }
Color* Object::Text::color() { return &desc.color; }

void Object::Text::set_desc(const TextDesc& arg_desc) {
    if (desc.text != arg_desc.text || desc.font.id != arg_desc.font.id) {
        texture_dirty = true;
    }
    desc = arg_desc;
    Island::request_redraw(true);
}

void Object::Text::set_text(string_view text) {
    if (desc.text == text) {
        return;
    }
    desc.text = text;
    texture_dirty = true;
    Island::request_redraw(true);
}

void Object::Text::set_font(FontHandle font) {
    if (desc.font.id == font.id) {
        return;
    }
    desc.font = font;
    texture_dirty = true;
    Island::request_redraw(true);
}

void Object::Text::set_color(const Color& color) {
    desc.color = color;
    Island::request_redraw(true);
}

void Object::Text::draw() {
    const float scale = Renderer::get_scale();
    if (texture_dirty || texture_scale != scale) {
        Renderer::prepare_text(desc, scale, texture);
        texture_dirty = false;
        texture_scale = scale;
    }
    Renderer::draw_text(desc, texture);
}

Object::Rectangle* Object::add_rectangle(const RectDesc& desc) {
    auto object = make_unique<Rectangle>(desc);
    auto* handle = object.get();
    objects.push_back(std::move(object));
    Island::request_redraw(true);
    return handle;
}

Object::Image* Object::add_image(const ImageDesc& desc) {
    auto object = make_unique<Image>(desc);
    auto* handle = object.get();
    objects.push_back(std::move(object));
    Island::request_redraw(true);
    return handle;
}

Object::Text* Object::add_text(const TextDesc& desc) {
    auto object = make_unique<Text>(desc);
    auto* handle = object.get();
    objects.push_back(std::move(object));
    Island::request_redraw(true);
    return handle;
}

void Object::click(float x, float y, bool left) {
    for (auto& object : views::reverse(objects)) {
        if (object->click(x, y, left)) {
            return;
        }
    }
}

void Object::draw() {
    for (auto& object : objects) {
        object->update();
        object->draw();
    }
}

void Object::clear() {
    objects.clear();
    Island::request_redraw(true);
}
