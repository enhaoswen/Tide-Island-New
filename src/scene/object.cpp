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

} // namespace

float* Object::ItemBase::radius() {
    return nullptr;
}

Color* Object::ItemBase::color() {
    return nullptr;
}

void Object::ItemBase::set_click_callbacks(void (*left)(), void (*right)()) {
    left_callback = left;
    right_callback = right;
}

bool Object::ItemBase::click(float x, float y, bool left) {
    const Frame& bounds = frame();
    if (
        x < bounds.x || x > bounds.x + bounds.width ||
        y < bounds.y || y > bounds.y + bounds.height
    ) {
        return false;
    }

    const float* current_radius = radius();
    const float corner_radius = clamp(
        current_radius ? *current_radius : 0.0f,
        0.0f, max(0.0f, min(bounds.width, bounds.height) / 2)
    );
    const float dx = x - clamp(x, bounds.x + corner_radius,
                              bounds.x + bounds.width - corner_radius);
    const float dy = y - clamp(y, bounds.y + corner_radius,
                              bounds.y + bounds.height - corner_radius);
    if (dx * dx + dy * dy > corner_radius * corner_radius) {
        return false;
    }

    auto callback = left ? left_callback : right_callback;
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

Object::Rectangle::Rectangle(const RectDesc& desc) : desc(desc) {
    set_click_callbacks(desc.click_callback_left, desc.click_callback_right);
}

Frame& Object::Rectangle::mutable_frame() { return desc.frame; }
const Frame& Object::Rectangle::frame() const { return desc.frame; }
float* Object::Rectangle::radius() { return &desc.radius; }
Color* Object::Rectangle::color() { return &desc.color; }

void Object::Rectangle::set_desc(const RectDesc& desc) {
    this->desc = desc;
    set_click_callbacks(desc.click_callback_left, desc.click_callback_right);
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

Object::Image::Image(const ImageDesc& desc) : desc(desc) {
    set_click_callbacks(desc.click_callback_left, desc.click_callback_right);
}

Frame& Object::Image::mutable_frame() { return desc.frame; }
const Frame& Object::Image::frame() const { return desc.frame; }
float* Object::Image::radius() { return &desc.radius; }

void Object::Image::set_desc(const ImageDesc& desc) {
    if (this->desc.path != desc.path) {
        texture_dirty = true;
    }
    this->desc = desc;
    set_click_callbacks(desc.click_callback_left, desc.click_callback_right);
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

Object::Text::Text(const TextDesc& desc) : desc(desc) {
    set_click_callbacks(desc.click_callback_left, desc.click_callback_right);
}

Frame& Object::Text::mutable_frame() { return desc.frame; }
const Frame& Object::Text::frame() const { return desc.frame; }
Color* Object::Text::color() { return &desc.color; }

void Object::Text::set_desc(const TextDesc& desc) {
    if (this->desc.text != desc.text || this->desc.font.id != desc.font.id) {
        texture_dirty = true;
    }
    this->desc = desc;
    set_click_callbacks(desc.click_callback_left, desc.click_callback_right);
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
    const double scale = Renderer::get_scale();
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
    for (auto it = objects.rbegin(); it != objects.rend(); ++it) {
    if ((*it)->click(x, y, left)) {
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
