#include "app/views/sysinfo.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include <optional>
#include <string>
#include <utility>

#include "app/API.hpp"
#include "app/island.hpp"
#include "backend/sysinfo/sysinfo.hpp"
#include "graphics/image.hpp"
#include "graphics/renderer.hpp"
#include "scene/animation.hpp"

using namespace std;
using namespace std::chrono;

namespace {

enum class Kind : uint8_t {
	Volume,
	Brightness,
	Power
};

enum class Level : uint8_t {
	L25,
	L50,
	L75,
	L100,
	Muted
};

constexpr uint32_t k_progress_w{248};
constexpr uint32_t k_icon_w{140}; // icon-only split, as in the QML island
constexpr float    k_height{38.f};
constexpr float    k_icon{36.f};
constexpr float    k_ring{30.f};

// Where the drawing sits in its 64px cell. `right` includes the +/- sign when there is one.
struct Art {
	float left_{};
	float right_{};
	float center_y_{};
};

struct Sprites {
	string_view dir_{};
	string_view name_{};
	int         step_end_{}; // transitions are settled from this frame on
	Art         art_{};
};

constexpr Sprites k_volume{"volume-icons", "volume", 19, {7.f / 64.f, 61.f / 64.f, 35.5f / 64.f}};
constexpr Sprites k_brightness{"brightness-icons", "brightness", 26, {3.f / 64.f, 62.f / 64.f, 38.f / 64.f}};
constexpr Art     k_battery_art{6.f / 64.f, 59.f / 64.f, 32.f / 64.f};

constexpr int   k_frames{30};
constexpr int   k_mute_end{22};
constexpr int   k_plug_end{18};
constexpr int   k_unplug_end{15};
constexpr float k_sign_shift{5.f}; // pulls the sign toward the icon

// The sign strip is a seamless loop with no in/out frames, so those are opacity fades.

constexpr auto k_frame_time{16667us};
constexpr auto k_hide_after{1250ms};

const Color k_track{1.f, 1.f, 1.f, .16f};

struct Clip {
	Object::Handle node_{};
	string         strip_{}; // loaded sprite, so replaying it (loops) skips the decode
	int            frame_{};
	int            end_{};
	int            step_{1};
	bool           is_active_{};
};

// One opening of the OSD; stop() resets it as a whole. Timer callbacks carry no context,
// so this has to live at file scope.
struct Osd {
	Object::Handle     content_{}, value_{}, ring_{};
	Clip               icon_{}, sign_{};
	Level              shown_{};
	bool               is_shown_plugged_{};
	int                sign_dir_{};
	bool               is_sign_again_{}; // another change arrived during this cycle: loop once more
	Timer::EventHandle hide_timer_{}, frame_timer_{};
};

Osd                osd{};
SysInfoView::Fonts fonts{};
Kind               kind{};
StateType          previous{StateType::Clock};

optional<SysInfo::Volume> last_volume{};
optional<float>           last_brightness{};
optional<SysInfo::Power>  last_power{};

Object::Context* context{};

void (*switch_state)(StateType){};

Object::Context& ctx() {
	return *context;
}

bool is_open() {
	return static_cast<bool>(osd.content_);
}

const Sprites& sprites() {
	return kind == Kind::Brightness ? k_brightness : k_volume;
}

int percent(float v) {
	return static_cast<int>(lround(v * 100.f));
}

Level quarter(float v) {
	return static_cast<Level>(clamp(static_cast<int>(ceil(v * 4.f - 1e-3f)), 1, 4) - 1);
}

Level level_of(const SysInfo::Volume& v) {
	return v.is_muted_ ? Level::Muted : quarter(v.level_);
}

Level target() {
	if(kind == Kind::Brightness) {
		const auto b{SysInfo::get_brightness()};
		return b ? quarter(*b) : Level::L100;
	}
	const auto v{SysInfo::get_volume()};
	return v ? level_of(*v) : Level::L100;
}

int percent_of(Level l) {
	return (to_underlying(l) + 1) * 25;
}

bool is_plugged() {
	const auto p{SysInfo::get_power()};
	return p && p->is_plugged_;
}

bool is_charging() {
	const auto p{SysInfo::get_power()};
	return p && p->is_plugged_ && p->status_ == SysInfo::Charge::Charging;
}

// ---- Sprites ----------------------------------------------------------------

bool load(Clip& c, const string& sprite, int frames) {
	if(c.strip_ == sprite)
		return true;
	const float scale{Renderer::text_scale()};
	const int   side{max(1, static_cast<int>(lround(k_icon * scale)))};
	const auto  path{format("{}/{}", TIDE_RESOURCE_DIR, sprite)};
	auto        px{Image::load_strip(path.c_str(), frames, side)};
	if(!px) {
		c.strip_.clear();
		return false;
	}
	Texture t{};
	t.upload(px->width_, px->height_, Texture::Format::RGBA8, px->rgba_);
	t.set_scale(scale);
	ctx().image(c.node_).set_texture(std::move(t));
	c.strip_ = sprite;
	return true;
}

void show_frame(const Clip& c) {
	constexpr float k_frame_width{1.f / k_frames};
	ctx().image(c.node_).set_uv({.u0_ = static_cast<float>(c.frame_) * k_frame_width,
		.v0_                          = 0.f,
		.u1_                          = static_cast<float>(c.frame_ + 1) * k_frame_width,
		.v1_                          = 1.f});
}

void on_frame();

void schedule_frame() {
	if(!Timer::is_pending(osd.frame_timer_))
		osd.frame_timer_ = Api::push_event(k_frame_time, on_frame, 0.f);
}

void play(Clip& c, const string& sprite, int from, int to) {
	c.is_active_ = load(c, sprite, k_frames);
	if(!c.is_active_)
		return;
	c.frame_ = from;
	c.end_   = to;
	c.step_  = to >= from ? 1 : -1;
	show_frame(c);
	schedule_frame();
}

void rest(const string& sprite) {
	osd.icon_.is_active_ = false;
	if(load(osd.icon_, sprite, 1))
		ctx().image(osd.icon_.node_).set_uv({});
}

void rest_muted() {
	osd.icon_.is_active_ = false;
	if(load(osd.icon_, format("{}/mute/volume-mute.png", k_volume.dir_), k_frames)) {
		osd.icon_.frame_ = k_frames - 1;
		show_frame(osd.icon_);
	}
}

// One clip per call; adjacent levels only, so 25 -> 100 plays three steps.
void step_level() {
	const Sprites& s{sprites()};
	const Level    want{target()};
	const string   mute{format("{}/mute/volume-mute.png", s.dir_)};
	if(osd.shown_ == want) {
		if(want == Level::Muted)
			rest_muted();
		else
			rest(format("{}/static/{}-{}.png", s.dir_, s.name_, percent_of(want)));
		return;
	}
	if(want == Level::Muted) {
		osd.shown_ = Level::Muted;
		play(osd.icon_, mute, 0, k_mute_end);
		return;
	}
	if(osd.shown_ == Level::Muted) {
		// The mute strip is drawn at another size, so unmuting ends on a cut to the static icon.
		osd.shown_ = want;
		play(osd.icon_, mute, k_mute_end, 0);
		return;
	}
	const bool is_up{to_underlying(want) > to_underlying(osd.shown_)};
	const auto next{static_cast<Level>(to_underlying(osd.shown_) + (is_up ? 1 : -1))};
	play(osd.icon_,
		format("{}/transition/{}-{}-{}-{}.png",
			s.dir_,
			s.name_,
			is_up ? "up" : "down",
			percent_of(osd.shown_),
			percent_of(next)),
		0,
		s.step_end_);
	osd.shown_ = next;
}

// Plug in / unplug transition first, then the charging loop for as long as it charges.
void step_battery() {
	const bool is_plugged_now{is_plugged()};
	if(osd.is_shown_plugged_ != is_plugged_now) {
		osd.is_shown_plugged_ = is_plugged_now;
		if(is_plugged_now)
			play(osd.icon_, "charging-icons/transition/charge-plug-in.png", 1, k_plug_end); // 0 == 1
		else
			play(osd.icon_, "charging-icons/transition/charge-unplug.png", 1, k_unplug_end);
		return;
	}
	if(is_charging())
		play(osd.icon_, "charging-icons/loop/charging-loop.png", 0, k_frames - 1);
	else
		rest(is_plugged_now ? "charging-icons/static/battery-charging.png" : "charging-icons/static/battery.png");
}

void step_icon() {
	if(kind == Kind::Power)
		step_battery();
	else
		step_level();
}

void fade_sign(float to) {
	const Object::Handle h{osd.sign_.node_};
	ctx().animate(Object::TAnimDesc{
		.h_     = h,
		.to_    = to,
		.t_     = Timings::k_quick,
		.delay_ = 0.f,
		.p_     = Prop::Opacity,
		.on_done_ =
			[h, to] {
				if(to == 0.f && !osd.sign_.is_active_ && ctx().is_alive(h))
					ctx().set_visible(h, false);
			},
	});
}

void play_sign(int dir) {
	if(osd.sign_.is_active_ && osd.sign_dir_ == dir) {
		osd.is_sign_again_ = true;
		return;
	}
	const bool is_visible{osd.sign_.is_active_ || ctx().is_animating(osd.sign_.node_, Prop::Opacity)};
	osd.sign_dir_      = dir;
	osd.is_sign_again_ = false;
	play(osd.sign_, format("sign-icons/sign-{}.png", dir > 0 ? "plus" : "minus"), 0, k_frames - 1);
	ctx().set_visible(osd.sign_.node_, true);
	if(!is_visible)
		ctx().set_opacity(osd.sign_.node_, 0.f);
	fade_sign(1.f);
}

void on_frame() {
	osd.frame_timer_ = {};
	if(!is_open())
		return;
	if(osd.icon_.is_active_) {
		if(osd.icon_.frame_ == osd.icon_.end_) {
			step_icon();
		} else {
			osd.icon_.frame_ += osd.icon_.step_;
			show_frame(osd.icon_);
		}
	}
	if(osd.sign_.is_active_) {
		if(++osd.sign_.frame_ < k_frames) {
			show_frame(osd.sign_);
		} else if(osd.is_sign_again_) {
			osd.sign_.frame_   = 0;
			osd.is_sign_again_ = false;
			show_frame(osd.sign_);
		} else {
			osd.sign_.is_active_ = false;
			osd.sign_.frame_     = k_frames - 1;
			fade_sign(0.f);
		}
	}
	if(osd.icon_.is_active_ || osd.sign_.is_active_)
		schedule_frame();
	Island::request_redraw(true);
}

// ---- Content ----------------------------------------------------------------

Object::Handle make_text(Object::Handle parent, FontHandle font, const Color& color) {
	const Object::Handle t{ctx().create_text(parent, {})};
	ctx().text(t).set_font(font).set_color(color);
	return t;
}

Object::Handle make_icon(Object::Handle box, const Art& art, float dx = 0.f) {
	auto&                c{ctx()};
	const Object::Handle h{c.create_image(box,
		Object::ImageDesc{
			.frame_ = {-art.left_ * k_icon + dx, (.5f - art.center_y_) * k_icon, k_icon, k_icon},
		})};
	c.set_size_mode(h, Object::Axis::Horizontal, Object::SizeMode::Fixed);
	c.set_size_mode(h, Object::Axis::Vertical, Object::SizeMode::Fixed);
	return h;
}

// Sized to the drawing rather than the cell, so layout spacing is measured from the art.
Object::Handle make_icon_box(Object::Handle parent, const Art& art, float shrink = 0.f) {
	return ctx().create_group(parent, Frame{0.f, 0.f, (art.right_ - art.left_) * k_icon - shrink, k_icon});
}

void build(Object::Handle bg) {
	auto& c{ctx()};

	Object::LayoutData centered{};
	centered.align_   = Object::Alignment::Center;
	centered.justify_ = Object::Justify::Center;
	c.set_layout(bg, centered);
	c.set_clip(bg, true);

	osd.content_ = c.create_group(bg, Frame{0.f, 0.f, static_cast<float>(SysInfoView::width()), k_height});

	Object::LayoutData row{};
	row.align_ = Object::Alignment::Center;

	if(kind == Kind::Power) {
		row.justify_ = Object::Justify::Center;
		c.set_layout(osd.content_, row);
		osd.icon_ = Clip{.node_ = make_icon(make_icon_box(osd.content_, k_battery_art), k_battery_art)};
		return;
	}

	Object::LayoutData bar{};
	bar.align_   = Object::Alignment::Center;
	bar.justify_ = Object::Justify::SpaceBetween;
	bar.padding_ = {.left_ = 18.f, .top_ = 0.f, .right_ = 16.f, .bottom_ = 0.f};
	c.set_layout(osd.content_, bar);

	const Object::Handle left{c.create_group(osd.content_, Frame{0.f, 0.f, 0.f, 0.f})};
	c.set_size_mode(left, Object::Axis::Horizontal, Object::SizeMode::Fit);
	c.set_size_mode(left, Object::Axis::Vertical, Object::SizeMode::Fit);
	row.spacing_ = 4.f;
	c.set_layout(left, row);

	const Art&           art{sprites().art_};
	const Object::Handle box{make_icon_box(left, art, k_sign_shift)};
	osd.icon_ = Clip{.node_ = make_icon(box, art)};
	osd.sign_ = Clip{.node_ = make_icon(box, art, -k_sign_shift)};
	c.set_visible(osd.sign_.node_, false);
	osd.value_ = make_text(left, fonts.value_, Color::k_white);

	osd.ring_ = c.create_ring(osd.content_,
		Object::RingDesc{
			.frame_     = {0.f, 0.f, k_ring, k_ring},
			.color_     = Color::k_white,
			.track_     = k_track,
			.progress_  = 0.f,
			.thickness_ = 3.5f,
		});
}

void refresh(bool should_animate) {
	auto& c{ctx()};
	float progress{};
	switch(kind) {
		case Kind::Volume: {
			const auto v{SysInfo::get_volume()};
			if(!v)
				return;
			c.text(osd.value_).set_text(format("{}%", percent(v->level_)));
			progress = v->level_;
			break;
		}
		case Kind::Brightness: {
			const auto b{SysInfo::get_brightness()};
			if(!b)
				return;
			c.text(osd.value_).set_text(format("{}%", percent(*b)));
			progress = *b;
			break;
		}
		case Kind::Power: {
			if(!osd.icon_.is_active_)
				step_icon();
			return;
		}
	}
	if(!osd.icon_.is_active_)
		step_icon();

	progress = clamp(progress, 0.f, 1.f);
	if(should_animate)
		c.animate(Object::TAnimDesc{.h_ = osd.ring_,
			.to_                        = progress,
			.t_                         = Timings::k_quick,
			.delay_                     = 0.f,
			.p_                         = Prop::Progress});
	else
		c.ring(osd.ring_).set_progress(progress);
}

void on_hide() {
	osd.hide_timer_ = {};
	if(is_open() && Island::get_state() == StateType::Osd)
		switch_state(previous);
}

void restart_hide(microseconds after = k_hide_after) {
	Api::cancel_event(osd.hide_timer_);
	osd.hide_timer_ = Api::push_event(after, on_hide, 0.f);
}

// Opens the OSD for `k`, or refreshes it if already showing. switch_state is synchronous,
// so on true the nodes exist and the caller can start extra animations right away.
bool show(Kind k) {
	if(Island::get_island().current_priority_ > Island::k_priority_sys_info)
		return false;
	const StateType s{Island::get_state()};
	if(is_open() && kind == k) {
		refresh(true);
		restart_hide();
		Island::request_redraw(true);
		return false;
	}
	if(s != StateType::Osd)
		previous = s;
	kind = k;
	switch_state(StateType::Osd);
	return is_open();
}

// Shared by volume and brightness: animate from the old level when freshly opened, and
// show +/- for the direction of the change.
void level_changed(Kind k, Level from, Level to, int was, int now, bool has_sign) {
	const bool is_opened{show(k)};
	if(!is_open() || kind != k)
		return;
	if(is_opened && from != to) {
		osd.shown_ = from;
		step_icon();
	}
	if(has_sign && now != was)
		play_sign(now > was ? 1 : -1);
}

void on_sysinfo(SysInfo::Update u) {
	// Several sources can change in one wakeup, so each is checked on its own.
	if(SysInfo::has_flag(u, SysInfo::Update::Power)) {
		const auto p{SysInfo::get_power()};
		const auto prev{exchange(last_power, p)};
		if(prev && p && prev->is_plugged_ != p->is_plugged_) {
			const bool is_opened{show(Kind::Power)};
			if(is_open() && kind == Kind::Power) {
				if(is_opened)
					osd.is_shown_plugged_ = prev->is_plugged_;
				step_icon();
				restart_hide();
			}
		} else if(is_open() && kind == Kind::Power) {
			refresh(true);
			Island::request_redraw(true);
		}
	}

	if(SysInfo::has_flag(u, SysInfo::Update::Volume)) {
		const auto v{SysInfo::get_volume()};
		const auto prev{exchange(last_volume, v)};
		// nullopt -> value is (re)connecting, not the user changing anything.
		if(prev && v && *prev != *v)
			level_changed(Kind::Volume,
				level_of(*prev),
				level_of(*v),
				percent(prev->level_),
				percent(v->level_),
				v->is_muted_ == prev->is_muted_);
	}

	if(SysInfo::has_flag(u, SysInfo::Update::Brightness)) {
		const auto b{SysInfo::get_brightness()};
		const auto prev{exchange(last_brightness, b)};
		if(prev && b && *prev != *b)
			level_changed(Kind::Brightness, quarter(*prev), quarter(*b), percent(*prev), percent(*b), true);
	}
}

}

namespace SysInfoView {

void init(Object::Context& arg_ctx, const Fonts& f, void (*arg_switch_state)(StateType)) {
	context         = &arg_ctx;
	switch_state    = arg_switch_state;
	fonts           = f;
	last_volume     = SysInfo::get_volume();
	last_brightness = SysInfo::get_brightness();
	last_power      = SysInfo::get_power();
	SysInfo::set_listener(on_sysinfo);
}

void shutdown() {
	SysInfo::set_listener(nullptr);
	stop();
}

void start(Object::Handle arg_bg) {
	stop();
	if(!arg_bg)
		return;
	build(arg_bg);

	if(kind == Kind::Power)
		osd.is_shown_plugged_ = is_plugged();
	else
		osd.shown_ = target();
	refresh(false);

	auto& c{ctx()};
	c.set_opacity(osd.content_, 0.f);
	// Power keeps the clock's size, so nothing to wait for: show it almost at once.
	const Timing& fade{kind == Kind::Power ? Timings::k_instant : Timings::k_smooth};
	c.animate(Object::TAnimDesc{.h_ = osd.content_, .to_ = 1.f, .t_ = fade, .delay_ = 0.f, .p_ = Prop::Opacity});
	restart_hide();
}

void stop() {
	Api::cancel_event(osd.hide_timer_);
	Api::cancel_event(osd.frame_timer_);
	if(osd.content_) {
		auto& c{ctx()};
		c.set_clip(c.parent(osd.content_), false);
		c.destroy(osd.content_);
	}
	osd = {};
}

uint32_t width() {
	return kind == Kind::Power ? k_icon_w : k_progress_w;
}

}
