#include "app/views/clock_view.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <format>
#include <string>

#include "app/API.hpp"
#include "app/settings.hpp"
#include "backend/timer.hpp"
#include "utils/log.hpp"

using namespace std;

namespace {

Object::Context*   context{};
Object::Handle     time_text{};
Timer::EventHandle next_tick{};
bool               is_running{};
string             last{};

void update() {
	const auto now{chrono::system_clock::now()};
	auto       view{context->text(time_text)};

	const auto local{chrono::current_zone()->to_local(now)};
	string     s{Settings::get_settings().is_24h_ ? format("{:%H:%M}", local) : format("{:%I:%M %p}", local)};
	if(s.front() == '0' && !Settings::get_settings().is_24h_)
		s.erase(0, 1);
	if(!Settings::get_settings().is_24h_)
		ranges::transform(s, s.begin(), [](unsigned char c) { return static_cast<char>(tolower(c)); });
	if(s != last) {
		last = s;
		view.set_text(s);
	}
}

void schedule_next_tick();

void on_tick() {
	next_tick = {};
	if(!is_running)
		return;
	update();
	schedule_next_tick();
}

void schedule_next_tick() {
	if(Timer::is_pending(next_tick))
		return;

	using namespace chrono;
	const auto now{system_clock::now()};
	const auto next{floor<minutes>(now) + minutes{1}};
	next_tick = Api::push_event(duration_cast<microseconds>(next - now), on_tick, 0);
}

}

namespace ClockView {
void start(Object::Context& ctx, Object::Handle bg, FontHandle font, std::source_location location) {
	if(!font.id_) {
		Log::logger(Log::LogLevel::Error, "{}:{}: Invalid font id", location.file_name(), location.line());
		return;
	}

	if(!bg) {
		Log::logger(Log::LogLevel::Error, "{}:{}: Invalid root", location.file_name(), location.line());
		return;
	}

	stop();

	context = &ctx;

	time_text = ctx.create_text(bg, {});
	ctx.text(time_text).set_font(font).set_color(Color::k_white);

	Object::LayoutData layout{};
	layout.align_   = Object::Alignment::Center;
	layout.justify_ = Object::Justify::Center;
	ctx.set_layout(bg, layout);

	last       = {};
	is_running = true;
	update();
	schedule_next_tick();
}

void stop() {
	is_running = false;
	Api::cancel_event(next_tick);
	next_tick = {};
	if(time_text) {
		context->destroy(time_text);
		time_text = {};
	}
}

void shutdown() {
	stop();
}

}
