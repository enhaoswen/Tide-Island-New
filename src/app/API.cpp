#include "app/API.hpp"

#include <algorithm>

#include "app/island.hpp"
#include "app/main_loop.hpp"
#include "app/state_manager.hpp"
#include "app/views/clock_view.hpp"
#include "app/views/sysinfo.hpp"
#include "backend/egl_vendor.hpp"
#include "backend/sysinfo/sysinfo.hpp"
#include "backend/timer.hpp"
#include "backend/wayland.hpp"
#include "graphics/renderer.hpp"
#include "graphics/text.hpp"
#include "music/media.hpp"
#include "music/spotify_bridge.hpp"
#include "scene/render.hpp"
#include "utils/log.hpp"
#include "utils/struct.hpp"

using namespace std;
using namespace std::chrono;

namespace {

const Island::IslandState* island{};

Renderer::GpuRenderer renderer{};

// Wayland input only feeds the scene's state machine and queues events; callbacks run later
// from Api::run via dispatch_events(), outside Wayland dispatch.
void on_pointer_motion(float x, float y) {
	StateManager::get_context().pointer_move(x, y);
}

void on_pointer_button(Wayland::PointerButton button, bool is_pressed) {
	Object::Button b{};
	switch(button) {
		case Wayland::PointerButton::Left:
			b = Object::Button::Left;
			break;
		case Wayland::PointerButton::Right:
			b = Object::Button::Right;
			break;
		case Wayland::PointerButton::Middle:
			b = Object::Button::Middle;
			break;
		case Wayland::PointerButton::Other:
			return;
	}
	StateManager::get_context().pointer_button(b, is_pressed);
}

void on_pointer_leave() {
	StateManager::get_context().pointer_cancel();
}

}

namespace Api {
void init() {

	Island::init();
	island = &Island::get_island();
	Log::logger(Log::LogLevel::Debug, "Island initialized successfully");

	Wayland::init(island->island_width_, island->island_height_, island->zone_, island->anchor_top_);
	Log::logger(Log::LogLevel::Debug, "Wayland initialized successfully");

	Text::init();
	Log::logger(Log::LogLevel::Debug, "Text initialized successfully");

	Renderer::init();
	Log::logger(Log::LogLevel::Debug, "Renderer initialized successfully");

	Timer::init();
	Log::logger(Log::LogLevel::Debug, "Timer initialized successfully");

	Media::init();
	SpotifyBridge::init();
	Log::logger(Log::LogLevel::Debug, "Media initialized (available: {})", Media::is_available());

	SysInfo::init();

	Wayland::set_pointer_callbacks({
		.motion_ = on_pointer_motion,
		.button_ = on_pointer_button,
		.leave_  = on_pointer_leave,
	});

	Log::logger(Log::LogLevel::Debug, "Initialization completed successfully");
}

FontHandle load_font(string_view family, unsigned pixel_size, Text::Weight weight) {
	return Text::load_font(family, pixel_size, weight);
}

void unload_font(FontHandle font) {
	Text::unload_font(font);
}

const Island::IslandState& get_island() {
	return *island;
}

Timer::EventHandle push_event(microseconds duration, void (*callback)(), float priority) {
	return Timer::push(duration, callback, priority);
}

bool cancel_event(Timer::EventHandle event) {
	return Timer::cancel(event);
}

void clear_status() {
	ClockView::stop();
	StateManager::get_context().clear();
}

void run() {
	auto  last{steady_clock::now()};
	auto& ctx{StateManager::get_context()};

	while(island->is_running_) {

		if(island->should_redraw_ && Wayland::is_frame_ready()) {
			Island::request_redraw(false);

			const auto  now{steady_clock::now()};
			const float dt_ms{
				std::min(static_cast<float>(duration_cast<microseconds>(now - last).count()) / 1000.0f, 33.f)};
			last = now;

			ctx.update(dt_ms * island->animation_scale_);
			// Animation / layout can move nodes under a still pointer (hover changes). Lay out
			// again if callbacks ran so their changes make it into this frame.
			if(ctx.dispatch_events())
				ctx.layout();
			StateManager::sync_surface();

			Renderer::begin_frame();
			Object::draw(ctx, renderer);
			Renderer::end_frame();
		}

		const bool should_redraw{MainLoop::wait(ctx.is_animating() || island->should_redraw_)};
		// Pointer input gathered during the wait; callbacks usually change something visible.
		const bool is_handled{ctx.dispatch_events()};
		Island::request_redraw(should_redraw || is_handled || island->should_redraw_);
	}
}

void shutdown() {
	SysInfoView::shutdown();
	ClockView::shutdown();
	StateManager::stop();
	SpotifyBridge::shutdown();
	Media::shutdown();
	SysInfo::shutdown();
	Timer::shutdown();
	Text::shutdown();
	Renderer::shutdown();
	Wayland::shutdown();
}

void select_for_wayland() {
	EglVendor::select_for_wayland();
}

}
