// Standalone tests with a fake renderer/backend; no display or GPU is required.
// clang++ -std=c++23 -Isrc src/scene/object.cpp src/utils/color.cpp tests/object_test.cpp -o /tmp/tide-object-tests
#include "scene/object.hpp"
#include "scene/layout.hpp"
#include "app/island.hpp"
#include "backend/wayland.hpp"
#include "graphics/renderer.hpp"

#include <cassert>
#include <cmath>
#include <iostream>
#include <type_traits>
#include <vector>

using namespace std::chrono_literals;

namespace {

bool redraw{};
float scale = 1;
int image_preparations{};
int text_preparations{};
int live_textures{};
int clicked{};
const RectDesc* last_rectangle{};
const ImageDesc* last_image{};
const TextDesc* last_text{};
const Texture* image_texture{};
const Texture* text_texture{};
std::vector<char> draw_order;

void near(float actual, float expected) {
    assert(std::abs(actual - expected) < 0.001f);
}

void animate(Object::Rectangle& obj, AnimationTarget target, float to, float priority = 0,
             std::chrono::milliseconds duration = 1h) {
    obj.add_animation({.to = to, .priority = priority, .duration = duration, .target = target});
}

void test_animation_policy() {
    Object::Rectangle rect(RectDesc{});
    rect.draw();
    auto& state = const_cast<RectDesc&>(*last_rectangle);

    animate(rect, AnimationTarget::Width, 100, 10);
    animate(rect, AnimationTarget::Width, 999, 9);
    assert(state.animations.size() == 1);
    assert(state.animations[0].to == 100);

    state.animations[0].start_time = std::chrono::steady_clock::now() - 30min;
    animate(rect, AnimationTarget::Width, 200, 10);
    assert(state.animations.size() == 1);
    assert(state.animations[0].from > 49 && state.animations[0].from < 51);
    assert(state.animations[0].from == rect.frame().width);
    assert(state.animations[0].to == 200);

    animate(rect, AnimationTarget::Width, 300, 11, 0ms);
    rect.update();
    near(rect.frame().width, 300);
    assert(state.animations.empty());
    rect.update();
    near(rect.frame().width, 300); // The interrupted animation never resumes.

    animate(rect, AnimationTarget::Width, 400, 100);
    state.animations[0].start_time = std::chrono::steady_clock::now() - 2h;
    animate(rect, AnimationTarget::Width, 500, 0);
    assert(state.animations.size() == 1);
    near(state.animations[0].from, 400);
    assert(state.animations[0].priority == 0);

    animate(rect, AnimationTarget::Height, 100, -1);
    animate(rect, AnimationTarget::ColorR, 1, -1);
    assert(state.animations.size() == 3); // Priority applies only to the same target.
    for (auto& animation : state.animations) {
        animation.duration = 0ms;
    }
    rect.update();
    assert(state.animations.empty());
    near(rect.frame().width, 500);
    near(rect.frame().height, 100);
    near(state.color[0], 1);

    animate(rect, AnimationTarget::X, 12, 0, -1ms);
    rect.update();
    near(rect.frame().x, 12);

    for (int i = 0; i < 1000; ++i) {
        animate(rect, AnimationTarget::Width, static_cast<float>(i));
        assert(state.animations.size() == 1);
    }
}

void test_animation_ownership_and_targets() {
    float external = 42;
    Animation inherited{
        .to = 999, .target_ptr = &external, .target = AnimationTarget::Width,
        .priority = 100, .duration = 0ms, .start_time = {},
    };
    RectDesc rd{};
    ImageDesc id{};
    TextDesc td{};
    rd.animations.push_back(inherited);
    id.animations.push_back(inherited);
    td.animations.push_back(inherited);
    Object::Rectangle rect(rd);
    Object::Image image(id);
    Object::Text text(td);
    rect.update();
    image.update();
    text.update();
    near(external, 42);
    assert(rd.animations.size() == 1 && id.animations.size() == 1 && td.animations.size() == 1);

    rect.add_animation({.to = 20, .duration = 0ms, .target = AnimationTarget::Radius});
    image.add_animation({.to = 30, .duration = 0ms, .target = AnimationTarget::Radius});
    text.add_animation({.to = 0.5f, .duration = 0ms, .target = AnimationTarget::ColorA});
    rect.update();
    image.update();
    text.update();
    rect.draw();
    image.draw();
    text.draw();
    near(last_rectangle->radius, 20);
    near(last_image->radius, 30);
    near(last_text->color[3], 0.5f);

    redraw = false;
    image.add_animation({.to = 1, .duration = 0ms, .target = AnimationTarget::ColorR});
    text.add_animation({.to = 1, .duration = 0ms, .target = AnimationTarget::Radius});
    rect.add_animation({.to = 1, .duration = 0ms, .target = static_cast<AnimationTarget>(100)});
    assert(!redraw);

    rect.set_desc(rd);
    image.set_desc(id);
    text.set_desc(td);
    rect.update();
    image.update();
    text.update();
    near(external, 42);
}

void test_texture_updates() {
    const int initial_images = image_preparations;
    ImageDesc image_desc{};
    image_desc.path = "first.png";
    Object::Image image(image_desc);
    assert(image_preparations == initial_images + 1);
    image.draw();
    image.set_radius(10);
    image.draw();
    assert(image_preparations == initial_images + 1);
    image.set_path("second.png");
    image.draw();
    assert(image_preparations == initial_images + 2);
    assert(last_image->path == "second.png");
    image.set_path("second.png");
    image.draw();
    assert(image_preparations == initial_images + 2);
    image.reload();
    image.draw();
    assert(image_preparations == initial_images + 3);
    image_desc.path = "third.png";
    image.set_desc(image_desc);
    image.draw();
    assert(image_preparations == initial_images + 4);

    const int initial_texts = text_preparations;
    TextDesc text_desc{};
    text_desc.text = "first";
    text_desc.font = {1};
    Object::Text text(text_desc);
    assert(text_preparations == initial_texts + 1);
    text.draw();
    text.set_color(Color::Red);
    text.draw();
    assert(text_preparations == initial_texts + 1);
    text.set_text("second");
    text.draw();
    assert(text_preparations == initial_texts + 2);
    assert(last_text->text == "second");
    text.set_text("second");
    text.set_font({1});
    text.draw();
    assert(text_preparations == initial_texts + 2);
    text.set_font({2});
    text.draw();
    assert(text_preparations == initial_texts + 3);
    scale = 2;
    text.draw();
    assert(text_preparations == initial_texts + 4);
    text.draw();
    assert(text_preparations == initial_texts + 4);
    scale = 1;
    text_desc.text = "third";
    text_desc.font = {3};
    text.set_desc(text_desc);
    text.draw();
    assert(text_preparations == initial_texts + 5);
}

void test_clicks_and_layout() {
    Object::Rectangle rect(RectDesc{.frame = {0, 0, 100, 100}, .radius = 20});
    ImageDesc image_desc{};
    image_desc.frame = {0, 0, 100, 100};
    image_desc.radius = 20;
    Object::Image image(image_desc);
    TextDesc text_desc{};
    text_desc.frame = {0, 0, 100, 100};
    Object::Text text(text_desc);
    rect.set_click_callback_left(+[] { clicked = 1; });
    image.set_click_callback_left(+[] { clicked = 2; });
    text.set_click_callback_right(+[] { clicked = 3; });
    assert(!rect.click(0, 0, true));
    assert(!image.click(0, 0, true));
    assert(rect.click(50, 50, true) && clicked == 1);
    assert(image.click(50, 50, true) && clicked == 2);
    assert(text.click(0, 0, false) && clicked == 3);
    assert(!text.click(200, 200, false));
    assert(!text.click(50, 50, true));

    rect.set_frame({0, 0, 40, 10});
    image.set_frame({0, 0, 60, 20});
    text.set_frame({0, 0, 80, 30});
    Layout_horizontal_layout((Frame{10, 20, 300, 100}), Align::Center, Align::Center, 5,
                             rect, &image, text);
    near(rect.frame().x, 65);
    near(image.frame().x, 110);
    near(text.frame().x, 175);
    near(rect.frame().y, 65);
    near(image.frame().y, 60);
    near(text.frame().y, 55);
    Layout_vertical_layout((Frame{0, 0, 100, 200}), Align::Center, Align::Center, 5,
                           &rect, image, &text);
    near(rect.frame().y, 65);
    near(image.frame().y, 80);
    near(text.frame().y, 105);

    RectDesc descriptor{.frame = {0, 0, 10, 10}};
    Layout_horizontal_layout((Frame{0, 0, 100, 100}), Align::Left, Align::Left, 5,
                             descriptor, &rect);
    near(descriptor.frame.x, 0);
    near(rect.frame().x, 15);
}

void test_registry() {
    Object::clear();
    auto* rect = Object::add_rectangle(RectDesc{.frame = {0, 0, 100, 100}});
    ImageDesc image_desc{};
    image_desc.frame = {0, 0, 100, 100};
    auto* image = Object::add_image(image_desc);
    TextDesc text_desc{};
    text_desc.frame = {0, 0, 100, 100};
    auto* text = Object::add_text(text_desc);
    rect->set_click_callback_left(+[] { clicked = 1; });
    image->set_click_callback_left(+[] { clicked = 2; });
    text->set_click_callback_left(+[] { clicked = 3; });
    Object::click(50, 50, true);
    assert(clicked == 3);
    text->set_click_callback_left(nullptr);
    Object::click(50, 50, true);
    assert(clicked == 2);
    image->set_click_callback_left(nullptr);
    Object::click(50, 50, true);
    assert(clicked == 1);
    draw_order.clear();
    Object::draw();
    assert((draw_order == std::vector<char>{'r', 'i', 't'}));

    const Frame* original_frame = &rect->frame();
    animate(*rect, AnimationTarget::Width, 200);
    for (int i = 0; i < 1000; ++i) {
        Object::add_rectangle(RectDesc{});
    }
    assert(&rect->frame() == original_frame);
    rect->draw();
    assert(last_rectangle->animations[0].target_ptr == &rect->frame().width);
    animate(*rect, AnimationTarget::Width, 250, 1, 0ms);
    rect->update();
    near(rect->frame().width, 250);
    redraw = false;
    Object::clear();
    assert(redraw && live_textures == 0);
    draw_order.clear();
    Object::draw();
    assert(draw_order.empty());
}

} // namespace

Texture::Texture() { ++live_textures; }
Texture::~Texture() { --live_textures; }
void Island::request_redraw(bool value) { redraw = value; }
float Wayland::get_scale() { return scale; }

void Renderer::prepare_image(const ImageDesc& desc, Texture& texture) {
    ++image_preparations;
    last_image = &desc;
    image_texture = &texture;
}

void Renderer::prepare_text(const TextDesc& desc, Texture& texture) {
    ++text_preparations;
    last_text = &desc;
    text_texture = &texture;
}

void Renderer::draw_rectangle(const RectDesc& desc) {
    last_rectangle = &desc;
    draw_order.push_back('r');
}

void Renderer::draw_image(const ImageDesc& desc, const Texture& texture) {
    assert(&texture == image_texture);
    last_image = &desc;
    draw_order.push_back('i');
}

void Renderer::draw_text(const TextDesc& desc, const Texture& texture) {
    assert(&texture == text_texture);
    last_text = &desc;
    draw_order.push_back('t');
}

int main() {
    static_assert(!std::is_polymorphic_v<Object::Rectangle>);
    static_assert(!std::is_polymorphic_v<Object::Image>);
    static_assert(!std::is_polymorphic_v<Object::Text>);
    static_assert(!std::is_move_constructible_v<Object::Rectangle>);
    static_assert(!std::is_move_constructible_v<Object::Image>);
    static_assert(!std::is_move_constructible_v<Object::Text>);
    test_animation_policy();
    test_animation_ownership_and_targets();
    test_texture_updates();
    test_clicks_and_layout();
    test_registry();
    std::cout << "Object tests passed\n";
}
