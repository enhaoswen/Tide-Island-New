#include "app/views/clock_view.hpp"
#include "app/API.hpp"
#include "backend/timer.hpp"

#include <chrono>
#include <format>

using namespace std;

namespace ClockView {
namespace {

Object::Handle<Object::Layout> root;
Object::Handle<Object::Text> time_text;
Timer::EventHandle next_tick;
FontHandle font;
bool running{};

void update() {
    const auto now = chrono::system_clock::now();
    if (auto* text = time_text.get()) {
        text->set_text(format("{:%H:%M}", chrono::current_zone()->to_local(now)));
    }
}

void schedule_next_tick();

void on_tick() {
    next_tick = {};
    if (!running) return;
    if (!root || !time_text) {
        stop();
        return;
    }
    update();
    schedule_next_tick();
}

void schedule_next_tick() {
    if (Timer::is_pending(next_tick)) return;
    using namespace chrono;
    const auto now = system_clock::now();
    const auto next = floor<minutes>(now) + minutes{1};
    next_tick = API::push_event(duration_cast<microseconds>(next - now), on_tick, 0);
}

} // namespace

void start(source_location) {
    if (running && root && time_text) {
        // Also repairs scheduling after the event queue has been cleared.
        schedule_next_tick();
        return;
    }
    stop();
    if (!font.id) font = API::load_font("Sans Serif", 18);
    root = Object::add_layout({.fit_surface = true});
    time_text = Object::add_text(TextDesc{
        .text = "",
        .font = font,
        .color = Color::White,
        .horizontal_align = Align::Center,
        .vertical_align = Align::Center,
    });
    root->add_element(time_text, Align::Center, Object::SizeMode::Fill);
    running = true;
    update();
    schedule_next_tick();
}

void stop() {
    running = false;
    API::cancel_event(next_tick);
    next_tick = {};
    Object::delete_element(time_text);
    Object::delete_element(root);
    time_text = {};
    root = {};
}

void shutdown() {
    stop();
    if (font.id) API::unload_font(font);
    font = {};
}

} // namespace ClockView
