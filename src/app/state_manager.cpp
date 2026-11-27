#include "app/state_manager.hpp"

#include <cmath>

#include "app/island.hpp"
#include "app/views/clock_view.hpp"
#include "app/views/music_player.hpp"
#include "app/views/sysinfo.hpp"
#include "backend/wayland.hpp"
#include "scene/animation.hpp"

using namespace std;

// Tide Island QS size

// Size of Clock: 140*38                                      radius 19
// Size of Lyrics: dynamic*38 (minimum width: 220)            radius 19
// Size of Custom: dynamic*38 (minimum width: 220)            radius 19
// Size of Split: 140*38 / 220*38 / 248*38 (icon/text/progress)radius 19
// Size of Workspace (long_capsule): 220*38                   radius 19
// Size of MusicPlayer (expanded): 410*165                    radius 32
// Size of BluetoothExpanded: 410*165                         radius 32
// Size of ControlCenter: 420*352                             radius 28
// Size of ControlCenterPowerView: 420*150                    radius 28
// Size of NotificationCenter: 410*dynamic                    radius 28
// Size of WallpaperPicker: 1100*260                          radius 28
// Size of ApplicationLauncher: 1100*260                      radius 28
// Size of FileShelf: 1100*260                                radius 28
// Size of Clipboard: 520*350                                 radius 28
// Size of ClipboardImagePreview: 520*400                     radius 28
// Size of Weather: 460*340                                   radius 28
// Size of Calendar: 420*340                                  radius 28
// Size of Notification: (272–400)*(56 or 68)                 radius 28 (56/2) 或 34 (68/2)
// Size of NotificationExpanded: 520*(84–240)                 radius 28

namespace {

struct StateSize {
	uint32_t width_{};
	uint32_t height_{};
	float    radius_{};
};

StateSize size_of(StateType t) {
	switch(t) {
		case StateType::Clock:
			return {.width_ = 140, .height_ = 38, .radius_ = 19};
		case StateType::Lyrics:
			return {.width_ = 220, .height_ = 38, .radius_ = 19};
		case StateType::MusicPlayer:
			return {.width_ = 410, .height_ = 165, .radius_ = 32};
		case StateType::ControlCenter:
			return {.width_ = 420, .height_ = 352, .radius_ = 28};
		case StateType::Osd:
			return {.width_ = SysInfoView::width(), .height_ = 38, .radius_ = 19};
	}
	return {.width_ = 140, .height_ = 38, .radius_ = 19};
}

Island::IslandState     island_state{};
StateManager::Resources resources{};
Object::Context         ctx{};
Object::Handle          root{};
Object::Handle          bg{};

constexpr float k_hover_scale = 1.03f;

bool is_expended(StateType type) {
	switch(type) {
		case StateType::Clock:
		case StateType::Lyrics:
		case StateType::Osd:
			return false;

		case StateType::MusicPlayer:
		case StateType::ControlCenter:
			return true;
	}

	return false;
}

void on_bg_click(const Object::Event& e) {
	if(e.button_ == Object::Button::Left) {
		StateManager::switch_state(is_expended(Island::get_state()) ? StateType::Clock : StateType::MusicPlayer);
	}
}

void set_surface(const uint32_t width, const uint32_t height) {
	Wayland::request_resize(width, height);
}

void hover(bool is_on);

void resize(const uint32_t width, const uint32_t height, bool is_expended, float radius = -1.f) {

	Island::set_priority(is_expended ? Island::k_priority_expended : Island::k_priority_transition);

	AnimCallback on_done{};
	if(!is_expended) {
		on_done = [width, height] {
			if(!ctx.is_animating(bg, Prop::W) && !ctx.is_animating(bg, Prop::H)) {
				Island::set_priority(Island::k_priority_normal);
				// Hover events were dropped during the transition; catch up with the pointer.
				if(ctx.get_hovered() == bg)
					hover(true);
				else
					set_surface(width, height);
				Island::request_redraw(true);
			}
		};
	}

	ctx.animate(Object::SAnimDesc{
		.h_       = bg,
		.to_      = static_cast<float>(width),
		.s_       = Springs::k_smooth,
		.delay_   = 0,
		.p_       = Prop::W,
		.on_done_ = on_done,
	});

	ctx.animate(Object::SAnimDesc{
		.h_       = bg,
		.to_      = static_cast<float>(height),
		.s_       = Springs::k_smooth,
		.delay_   = 0,
		.p_       = Prop::H,
		.on_done_ = on_done,
	});

	if(radius != -1.f) {
		ctx.animate(Object::TAnimDesc{
			.h_       = bg,
			.to_      = radius,
			.t_       = Timings::k_smooth,
			.delay_   = 0,
			.p_       = Prop::Radius,
			.on_done_ = nullptr,
		});
	}

	if(is_expended)
		set_surface(width, height);
	else if(const auto [sw, sh] = Wayland::get_surface_size(); width > sw || height > sh)
		set_surface(max(width, sw), max(height, sh)); // growing: room now, exact size once settled
}

void scale_bg(const float to, AnimCallback on_done = {}) {
	ctx.animate(Object::SAnimDesc{
		.h_       = bg,
		.to_      = to,
		.s_       = Springs::k_gentle,
		.delay_   = 0,
		.p_       = Prop::Scale,
		.on_done_ = std::move(on_done),
	});
}

void hover(const bool is_on) {
	const StateSize size{size_of(Island::get_state())};

	if(is_on) {
		const auto grown = [](const uint32_t v) {
			return v + static_cast<uint32_t>(ceil(static_cast<float>(v) * (k_hover_scale - 1.f)));
		};
		set_surface(grown(size.width_), grown(size.height_));
		scale_bg(k_hover_scale);
	} else {
		scale_bg(1.f, [size] {
			set_surface(size.width_, size.height_);
			Island::request_redraw(true);
		});
	}
}

void on_hovered([[maybe_unused]] const Object::Event& event) {
	if(is_expended(Island::get_state()) || Island::get_island().current_priority_ > Island::k_priority_hover)
		return;
	hover(ctx.get_hovered() == bg);
}

}

namespace StateManager {
void init(const Resources& arg_resources) {
	island_state = Island::get_island();
	resources    = arg_resources;

	root = ctx.create_root(island_state.island_width_, island_state.island_height_);

	bg = ctx.create_rect(root,
		Object::RectDesc{
			.frame_ =
				Frame{
					.x_      = 0.f,
					.y_      = 0.f,
					.width_  = static_cast<float>(island_state.island_width_),
					.height_ = static_cast<float>(island_state.island_height_),
				},

			.color_  = island_state.color_,
			.radius_ = island_state.radius_,
		});

	ctx.set_layout(root,
		Object::LayoutData{
			.dir_     = Object::Direction::Row,
			.justify_ = Object::Justify::Center,
		});

	ctx.set_size_mode(bg, Object::Axis::Horizontal, Object::SizeMode::Fixed);
	ctx.set_size_mode(bg, Object::Axis::Vertical, Object::SizeMode::Fixed);
	ctx.on_click(bg, on_bg_click);
	ctx.set_pivot(bg, .5f, 0.f);
	ctx.on(bg, Object::EventType::HoverEnter | Object::EventType::HoverLeave, on_hovered);

	SysInfoView::init(ctx, {.value_ = resources.main_font_, .label_ = resources.title_}, StateManager::switch_state);
}

Object::Context& get_context() {
	return ctx;
}

void sync_surface() {
	// Root follows the surface: it may only change in the frame the new buffer is used.
	Wayland::sync_surface();
	if(!root)
		return;
	// Actual size: the backend pads and rounds the requested one.
	if(!Wayland::is_resize_pending()) {
		const auto [w, h] = Wayland::get_surface_size();
		ctx.set_size(root, static_cast<float>(w), static_cast<float>(h));
		ctx.layout();
	}

	Wayland::set_blur(island_state.is_blur_);
	if(island_state.is_blur_) {
		const Frame f{ctx.absolute_frame(bg)};
		Wayland::set_blur_shape(f.x_, f.y_, f.width_, f.height_, ctx.rect(bg).get_radius());
	}
}

void switch_state(const StateType state) {
	stop();

	Island::set_state(state);
	if(ctx.scale(bg) != 1.f || ctx.is_animating(bg, Prop::Scale))
		scale_bg(1.f);

	if(state == StateType::Clock) {
		resize(140, 38, false, 19);
		ClockView::start(ctx, bg, resources.main_font_);
	} else if(state == StateType::MusicPlayer) {
		resize(410, 165, true, 32);
		MusicPlayerView::start(ctx,
			bg,
			{
				.title_  = resources.title_,
				.artist_ = resources.artist_,
				.time_   = resources.time_,
				.icon_   = resources.icon_,
			});
	} else if(state == StateType::Osd) {
		resize(SysInfoView::width(), 38, false, 19);
		SysInfoView::start(bg);
	}

	Island::request_redraw(true);
}

void stop() {
	switch(Island::get_state()) {
		case StateType::Clock:
			ClockView::stop();
			break;
		case StateType::MusicPlayer:
			MusicPlayerView::stop();
			break;
		case StateType::Osd:
			SysInfoView::stop();
			break;
		default:
			break;
	}
}

}
