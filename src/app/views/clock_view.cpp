#include "app/views/clock_view.hpp"
#include "utils/log.hpp"
#include "app/API.hpp"
#include "backend/timer.hpp"

#include <chrono>
#include <format>

using namespace std;

namespace {

Object::Handle<Object::Text> time_text{};
Timer::EventHandle           next_tick{};
bool                         running{};

void update() {
    const auto now = chrono::system_clock::now();
    if (auto* text = time_text.get()) {
        text->set_text(format("{:%H:%M}", chrono::current_zone()->to_local(now)));
    }
}

void schedule_next_tick();

void on_tick() {
    next_tick = {};
    if (!running)
        return;
    update();
    schedule_next_tick();
}

void schedule_next_tick() {
    if (Timer::is_pending(next_tick))
        return;

    using namespace chrono;
    const auto now  = system_clock::now();
    const auto next = floor<minutes>(now) + minutes{1};
    next_tick       = API::push_event(duration_cast<microseconds>(next - now), on_tick, 0);
}

} // namespace

void ClockView::start(ClockDesc clock_desc, source_location l) {
    if (!clock_desc.font.id) {
        Log::logger(Log::Error, "{}:{}: Invalid font id", l.file_name(), l.line());
        return;
    }

    if (!clock_desc.root) {
        Log::logger(Log::Error, "{}:{}: Invalid root", l.file_name(), l.line());
        return;
    }

    stop();

    time_text = Object::add_text(TextDesc{
        .text             = "",
        .font             = clock_desc.font,
        .color            = Color::White,
        .horizontal_align = Align::Center,
        .vertical_align   = Align::Center,
    });

    clock_desc.root->add_element(time_text, Align::Center, Object::SizeMode::Fill);
    running = true;
    update();
    schedule_next_tick();
}

void ClockView::stop() {
    running = false;
    API::cancel_event(next_tick);
    next_tick = {};
    Object::delete_element(time_text);
    time_text = {};
}

void ClockView::shutdown() {
    ClockView::stop();
}
