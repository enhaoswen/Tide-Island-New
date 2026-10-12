#include "scene/object.hpp"
#include "app/island.hpp"
#include "graphics/renderer.hpp"
#include "utils/log.hpp"

#include <algorithm>
#include <chrono>
#include <memory>

using namespace std;
using namespace std::chrono;

namespace {

vector<unique_ptr<Object::ItemBase>> objects;

bool is_color_target(AnimationTarget target) {
    return target >= AnimationTarget::ColorR && target <= AnimationTarget::ColorA;
}

bool handle_click(
    const Frame& frame,
    float radius,
    void (*left_callback)(),
    void (*right_callback)(),
    float x,
    float y,
    bool left
) {
    if (
        x < frame.x || x > frame.x + frame.width ||
        y < frame.y || y > frame.y + frame.height
    ) {
        return false;
    }

    radius = clamp(radius, 0.0f, max(0.0f, min(frame.width, frame.height) / 2));
    float dx = x - clamp(x, frame.x + radius, frame.x + frame.width - radius);
    float dy = y - clamp(y, frame.y + radius, frame.y + frame.height - radius);
    if (dx * dx + dy * dy > radius * radius) {
        return false;
    }

    if (left && left_callback) {
        left_callback();
    }
    else if (!left && right_callback) {
        right_callback();
    }
    return true;
}

} // namespace

float* Object::ItemBase::radius() {
    return nullptr;
}

Color* Object::ItemBase::color() {
    return nullptr;
}

void Object::ItemBase::set_frame(const Frame& frame) {
    mutable_frame() = frame;
    Island::request_redraw(true);
}

void Object::ItemBase::add_animation(Animation animation, source_location location) {
    Frame& frame = mutable_frame();
    animation.target_ptr = nullptr;
    switch (animation.target) {
        case AnimationTarget::Width: animation.target_ptr = &frame.width; break;
        case AnimationTarget::Height: animation.target_ptr = &frame.height; break;
        case AnimationTarget::X: animation.target_ptr = &frame.x; break;
        case AnimationTarget::Y: animation.target_ptr = &frame.y; break;
        case AnimationTarget::Radius: animation.target_ptr = radius(); break;
        default: break;
    }

    if (!animation.target_ptr && !(is_color_target(animation.target) && color())) {
        Log::logger(
            Log::Error,
            "{}:{}: Animation target {} is not supported for this object",
            location.file_name(), location.line(), static_cast<int>(animation.target)
        );
        return;
    }

    animations.push_back(animation);
    Island::request_redraw(true);
}

void Object::ItemBase::update() {
    if (animations.empty()) {
        return;
    }

    auto now = steady_clock::now();
    for (auto it = animations.begin(); it != animations.end();) {
        bool finished = now >= it->start_time + it->duration;
        float progress = finished ? 1.0f : clamp(
            duration<float>(now - it->start_time).count() / duration<float>(it->duration).count(),
            0.0f, 1.0f
        );
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
            it = animations.erase(it);
        }
        else {
            ++it;
        }
    }
    Island::request_redraw(true);
}

Object::Rectangle::Rectangle(const RectDesc& desc) : desc(desc) {}

Frame& Object::Rectangle::mutable_frame() { return desc.frame; }
const Frame& Object::Rectangle::frame() const { return desc.frame; }
float* Object::Rectangle::radius() { return &desc.radius; }
Color* Object::Rectangle::color() { return &desc.color; }

void Object::Rectangle::set_desc(const RectDesc& desc) {
    this->desc = desc;
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

bool Object::Rectangle::click(float x, float y, bool left) {
    return handle_click(desc.frame, desc.radius, desc.click_callback_left, desc.click_callback_right, x, y, left);
}

Object::Image::Image(const ImageDesc& desc) : desc(desc) {}

Frame& Object::Image::mutable_frame() { return desc.frame; }
const Frame& Object::Image::frame() const { return desc.frame; }
float* Object::Image::radius() { return &desc.radius; }

void Object::Image::set_desc(const ImageDesc& desc) {
    this->desc = desc;
    Island::request_redraw(true);
}

void Object::Image::set_radius(float radius) {
    desc.radius = radius;
    Island::request_redraw(true);
}

void Object::Image::set_path(string_view path) {
    desc.path = path;
    Island::request_redraw(true);
}

void Object::Image::draw() {
    Renderer::draw_image(desc);
}

bool Object::Image::click(float x, float y, bool left) {
    return handle_click(desc.frame, desc.radius, desc.click_callback_left, desc.click_callback_right, x, y, left);
}

Object::Text::Text(const TextDesc& desc) : desc(desc) {}

Frame& Object::Text::mutable_frame() { return desc.frame; }
const Frame& Object::Text::frame() const { return desc.frame; }
Color* Object::Text::color() { return &desc.color; }

void Object::Text::set_desc(const TextDesc& desc) {
    this->desc = desc;
    Island::request_redraw(true);
}

void Object::Text::set_text(string_view text) {
    desc.text = text;
    Island::request_redraw(true);
}

void Object::Text::set_font(FontHandle font) {
    desc.font = font;
    Island::request_redraw(true);
}

void Object::Text::set_color(const Color& color) {
    desc.color = color;
    Island::request_redraw(true);
}

void Object::Text::draw() {
    Renderer::draw_text(desc);
}

bool Object::Text::click(float x, float y, bool left) {
    return handle_click(desc.frame, 0, desc.click_callback_left, desc.click_callback_right, x, y, left);
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
    for (auto& object : objects) {
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
