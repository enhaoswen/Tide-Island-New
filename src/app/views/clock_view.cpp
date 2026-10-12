#include "app/views/clock_view.hpp"
#include "app/API.hpp"
#include "utils/log.hpp"

#include <chrono>
#include <format>
#include <source_location>

namespace ClockView {
namespace {

Object::Text* time_text{};
bool running{};
bool tick_pending{};

void update() {
    const auto now = std::chrono::system_clock::now();
    time_text->set_text(std::format(
        "{:%H:%M}", std::chrono::current_zone()->to_local(now)
    ));
}

void schedule_next_tick();

void on_tick() {
    tick_pending = false;
    if (!running) {
        return;
    }

    update();
    schedule_next_tick();
}

void schedule_next_tick() {
    if (tick_pending) {
        return;
    }

    using namespace std::chrono;
    const auto now = system_clock::now();
    const auto next = floor<minutes>(now) + minutes{1};
    const auto delay = duration_cast<microseconds>(next - now);

    API::push_event(delay, on_tick, 0);
    tick_pending = true;
}

} // namespace

void start(std::source_location location) {
    if (running) {
        Log::logger(Log::Warning, "{}:{}: Clock is running, but still still get called", location.file_name(), location.line());
        return;
    }

    if (!time_text) {
        const auto& conf = API::island_state();
        time_text = API::add_text(TextDesc{
            .frame = {0, 0, conf.island_width, conf.island_height},
            .text = "",
            .font = API::load_font("Sans Serif", 18),
            .color = Color::White,
            .horizontal_align = Align::Center,
            .vertical_align = Align::Center,
        });
    }

    running = true;
    update();
    schedule_next_tick();
}

void stop() {
    running = false;
}

} // namespace ClockView
