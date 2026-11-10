#include "app/views/clock_view.hpp"
#include "utils/log.hpp"
#include "app/API.hpp"
#include "app/state_manager.hpp"
#include "backend/timer.hpp"

#include <chrono>
#include <format>
#include <string>

using namespace std;

namespace {

Object::Handle     time_text{};
Timer::EventHandle next_tick{};
bool               running{};
string             last{};

void update() {
    const auto now  = chrono::system_clock::now();
    auto&      ctx  = StateManager::context();
    auto       view = ctx.text(time_text);

    const string s{format("{:%H:%M}", chrono::current_zone()->to_local(now))};
    if (s != last) {
        last = s;
        view.set_text(s);
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

void ClockView::start(Object::Handle bg, FontHandle font, std::source_location l) {
    if (!font.id) {
        Log::logger(Log::Error, "{}:{}: Invalid font id", l.file_name(), l.line());
        return;
    }

    if (!bg) {
        Log::logger(Log::Error, "{}:{}: Invalid root", l.file_name(), l.line());
        return;
    }

    stop();

    auto& ctx = StateManager::context();

    time_text = ctx.create_text(bg, {});
    ctx.text(time_text).set_font(font).set_color(Color::White);

    Object::LayoutData layout{};
    layout.align   = Object::Alignment::Center;
    layout.justify = Object::Justify::Center;
    ctx.set_layout(bg, layout);

    last    = {};
    running = true;
    update();
    schedule_next_tick();
}

void ClockView::stop() {
    running = false;
    API::cancel_event(next_tick);
    next_tick = {};
    if (time_text) {
        StateManager::context().destroy(time_text);
        time_text = {};
    }
}

void ClockView::shutdown() {
    ClockView::stop();
}