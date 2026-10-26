#include "app/API.hpp"
#include "app/island.hpp"
#include "app/state_manager.hpp"
#include "backend/timer.hpp"
#include "backend/wayland.hpp"
#include "graphics/renderer.hpp"
#include "scene/object.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {

struct DrawCall {
    bool text{};
    Frame frame{};
    Frame clip{};
    Color color{};
    float radius{};
};

IslandConf island{};
std::array<std::uint32_t, 2> surface_size{411, 166};
std::array<std::uint16_t, 2> requested_size{};
Frame current_clip{};
std::vector<DrawCall> draws;

void check(bool condition, std::string_view message) {
    if (!condition)
        throw std::runtime_error(std::string(message));
}

void render(void (*before_layout)() = nullptr) {
    draws.clear();
    Object::update(before_layout);
    Object::draw();
}

void single_root_background_and_clock() {
    const auto root = Object::add_layout({
        .frame = {0, 0, 140, 38}, .background = Color::Black, .radius = 19});
    StateManager::Resources resources{.main_font = {1}, .root = root};
    StateManager::init(resources);
    StateManager::switch_state(StateType::Clock);
    render(StateManager::update);

    check(draws.size() == 2 && !draws[0].text && draws[1].text, "single root draws its background before the clock");
    check(root->get_frame() == Frame{135.5f, 0, 140, 38}, "single root centers in the confirmed window");
    check(draws[0].frame == draws[1].frame, "clock fills the root directly");
    check(draws[0].clip == draws[1].clip, "root and clock share the same clip");
    check(requested_size == std::array<std::uint16_t, 2>{140, 38}, "clock requests its window size");

    surface_size = {141, 39};
    render(StateManager::update);
    check(root->get_frame() == Frame{0.5f, 0, 140, 38}, "single root follows the confirmed window resize");
    check(draws[0].frame == draws[1].frame, "clock follows the root when it moves");

    StateManager::switch_state(StateType::MusicPLayer);
    check(requested_size == std::array<std::uint16_t, 2>{410, 165}, "expanded root requests its window size");
    surface_size = {411, 166};
    // Finish the size animations deterministically. Position must use this
    // frame's animated width, and child layout must use the resulting position.
    root->add_animation({.to = 410, .priority = 2, .target = AnimationTarget::Width});
    root->add_animation({.to = 165, .priority = 2, .target = AnimationTarget::Height});
    render(StateManager::update);
    check(root->get_frame() == Frame{0.5f, 0, 410, 165}, "center must use the current animated size");
    check(draws.size() == 1 && !draws[0].text, "switching state removes only the clock content");
    StateManager::stop();
}

void background_and_content() {
    const auto window = Object::add_layout({
        .frame = {0, 0, 411, 166}, .cross_align = Align::Left});
    const auto panel = Object::add_layout({
        .frame = {0, 0, 140, 38}, .background = Color::Black, .radius = 19});
    const auto text = Object::add_text({.text = "12:34", .font = {1}, .color = Color::White});
    check(window->add_element(panel, Align::Center), "attach panel");
    check(panel->add_element(text, Align::Center, Object::SizeMode::Fill), "attach text");
    render();

    const Frame expected{135.5f, 0, 140, 38};
    check(panel->get_frame() == expected, "panel must stay at the top center");
    check(text->get_frame() == expected, "background must not displace text");
    check(draws.size() == 2, "transparent window must not draw a background");
    check(!draws[0].text && draws[1].text, "background must draw before content");
    check(draws[0].frame == expected && draws[1].frame == expected, "shared background/content bounds");
    check(draws[0].clip == expected && draws[1].clip == expected, "content must not be half clipped");
    check(draws[0].radius == 19, "background radius");

    window->set_frame({0, 0, 141, 39});
    render();
    check(panel->get_frame() == Frame{0.5f, 0, 140, 38}, "center after window resize");
    check(text->get_frame() == panel->get_frame(), "content follows window resize");

    window->set_frame({0, 0, 411, 166});
    panel->add_animation({.to = 410, .target = AnimationTarget::Width});
    panel->add_animation({.to = 165, .target = AnimationTarget::Height});
    render();
    check(panel->get_frame() == Frame{0.5f, 0, 410, 165}, "resized panel stays centered");
    check(text->get_frame() == panel->get_frame(), "content follows panel size animation");
}

void ancestor_clipping() {
    const auto parent = Object::add_layout({.frame = {20, 10, 100, 50}});
    const auto panel = Object::add_layout({
        .frame = {0, 0, 200, 80}, .background = Color::Black});
    const auto text = Object::add_text({.text = "clipped", .font = {1}});
    parent->add_element(panel, Align::Center);
    panel->add_element(text, Align::Center, Object::SizeMode::Fill);
    render();
    check(draws.size() == 2, "background and text should draw");
    const Frame expected{20, 10, 100, 50};
    check(draws[0].clip == expected && draws[1].clip == expected, "background must respect ancestor clipping");

    int clicks{};
    panel->set_callback_left([&] { ++clicks; });
    Object::click(0, 20, true);
    check(clicks == 0, "clipped part must not receive clicks");
    Object::click(30, 20, true);
    check(clicks == 1, "visible part should receive clicks");
}

void click_priority_and_corners() {
    int panel_clicks{}, child_clicks{}, right_clicks{};
    const auto panel = Object::add_layout({
        .frame = {20, 20, 100, 60}, .background = Color::Black, .radius = 20});
    const auto child = Object::add_clickable_rect({.frame = {0, 0, 40, 20}});
    panel->set_callback_left([&] { ++panel_clicks; });
    panel->set_callback_right([&] { ++right_clicks; });
    child->set_callback_left([&] { ++child_clicks; });
    panel->add_element(child, Align::Center);
    Object::update();

    Object::click(70, 50, true);
    check(child_clicks == 1 && panel_clicks == 0, "child must receive clicks before its container");
    Object::click(25, 45, true);
    check(panel_clicks == 1, "empty background should receive clicks");
    Object::click(70, 50, false);
    check(right_clicks == 1, "unhandled child click should reach the container");
    Object::click(20, 20, true);
    Object::click(119, 21, true);
    check(panel_clicks == 1, "rounded background corners must not be clickable");
}

void styles_and_animations() {
    const auto panel = Object::add_layout({.frame = {0, 0, 100, 60}});
    render();
    check(draws.empty(), "default background must be transparent");
    island.need_redraw = false;
    panel->set_background(Color::Black);
    panel->set_radius(12);
    check(island.need_redraw, "style setters must request a redraw");

    panel->set_radius(-1);
    panel->set_radius(std::numeric_limits<float>::quiet_NaN());
    panel->set_desc({.frame = {0, 0, 1, 1}, .radius = -1});
    render();
    check(draws.size() == 1 && draws[0].radius == 12, "invalid radius must leave the style unchanged");
    check(panel->get_frame() == Frame{0, 0, 100, 60}, "invalid descriptor must leave the frame unchanged");

    panel->add_animation({.to = 30, .target = AnimationTarget::Radius});
    panel->add_animation({.to = 0.25f, .target = AnimationTarget::ColorA});
    render();
    check(draws.size() == 1 && draws[0].radius == 30, "layout radius should animate");
    check(draws[0].color[3] == 0.25f, "layout color animation should affect its background");
    check(!Object::has_active_animations(), "completed animations must be removed");
}

void deletion_in_callback() {
    const auto panel = Object::add_layout({.frame = {0, 0, 100, 60}, .background = Color::Black});
    panel->set_callback_left([] { Object::clear(); });
    Object::click(50, 30, true);
    check(!panel, "layout callback can safely clear the scene");
    render();
    check(draws.empty(), "deleted layout must not draw");
}

} // namespace

// Headless platform/renderer substitutes: exercise real scene geometry, drawing
// order, clipping and callbacks without requiring a compositor or GPU context.
Texture::Texture() = default;
Texture::~Texture() = default;
int Texture::width() const { return 40; }
int Texture::height() const { return 18; }

namespace Wayland {
std::array<std::uint32_t, 2> get_surface_size() { return surface_size; }
std::array<std::uint32_t, 2> get_buffer_size() { return surface_size; }
float get_scale() { return 1; }
}

namespace Island {
const IslandConf& get_island() { return island; }
void request_redraw(bool redraw) { island.need_redraw = redraw; }
StateType get_state() { return island.state; }
void set_state(StateType state) { island.state = state; }
}

namespace API {
void resize(std::uint16_t width, std::uint16_t height) { requested_size = {width, height}; }
Timer::EventHandle push_event(std::chrono::microseconds, void (*)(), float) { return {}; }
bool cancel_event(Timer::EventHandle) { return false; }
}

namespace Timer {
bool is_pending(EventHandle) { return false; }
}

namespace Renderer {
void set_clip(const Frame& frame) { current_clip = frame; }
void prepare_image(const ImageDesc&, Texture&) {}
void prepare_text(const TextDesc&, Texture&) {}
void draw_rectangle(const RectDesc& desc) {
    draws.push_back({false, desc.frame, current_clip, desc.color, desc.radius});
}
void draw_image(const ImageDesc&, const Texture&) {}
void draw_text(const TextDesc& desc, const Texture&) {
    draws.push_back({true, desc.frame, current_clip, desc.color});
}
}

int main() {
    try {
        for (auto test : {single_root_background_and_clock, background_and_content, ancestor_clipping,
                          click_priority_and_corners, styles_and_animations, deletion_in_callback}) {
            Object::clear();
            draws.clear();
            surface_size = {411, 166};
            test();
        }
        Object::clear();
        std::cout << "Layout background, geometry, clipping, clicks and animation checks passed\n";
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
