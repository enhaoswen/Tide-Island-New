#include "app/views/music_player.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <format>
#include <functional>
#include <numbers>
#include <optional>
#include <string>

#include "app/API.hpp"
#include "app/island.hpp"
#include "backend/timer.hpp"
#include "graphics/renderer.hpp"
#include "graphics/text.hpp"
#include "graphics/texture.hpp"
#include "music/media.hpp"
#include "music/spotify_bridge.hpp"
#include "utils/log.hpp"

using namespace std;
using namespace std::chrono;
using Object::Handle;

namespace {

const Color k_art_placeholder{"#2c2c2e"};
const Color k_secondary{"#8e8e93"};
const Color k_track{"#333333"};
const Color k_bar_playing{"#b56cff"};
const Color k_bar_paused{"#5f4b72"};

constexpr float k_pad_x{20.f};
constexpr float k_pad_y{18.f};
constexpr float k_expanded_w{410.f};
constexpr float k_expanded_h{165.f};
constexpr float k_content_w{k_expanded_w - 2 * k_pad_x};
constexpr float k_art_size{60.f};
constexpr float k_bar_w{3.f};
constexpr float k_bar_min_h{6.f};
constexpr float k_bar_max_h{22.f};
constexpr float k_button_w{56.f};
// Taller than the glyphs so the row's center sits halfway between the progress
// bar and the island's bottom edge (equal space above and below the icons).
constexpr float k_controls_h{42.f};
constexpr float k_dim_opacity{0.45f};
constexpr float k_pressed_tone{0.7f};  // icon brightness while held
constexpr float k_pressed_size{0.86f}; // icon scale while held

constexpr int k_bar_count{5};

constexpr float k_icon_scale{1.25f}; // all transport glyph dimensions below are x this
constexpr float k_heart_size{24.f * k_icon_scale};

// Transport is centered on the island; the heart's right edge lines up with
// the end of the progress row (like AirPlay in iOS's player).
constexpr float           k_slot_step{0.225f};
constexpr array<float, 3> k_slot_center{0.5f - k_slot_step, 0.5f, 0.5f + k_slot_step};
constexpr float           k_favorite_slot{1.f - 20.f * k_icon_scale * 0.5f / k_content_w};

constexpr auto k_tick = milliseconds{64};

constexpr Timing k_reveal_fade{260.f, Ease::OutCubic};
constexpr float  k_reveal_delay{70.f};

// Press feedback: quick, critically damped squeeze; release springs back with a
// slight overshoot (damping ratio ~0.6), like iOS buttons.
constexpr Spring k_press_spring{.stiffness_ = 520.f, .damping_ = 46.f, .mass_ = 1.f};
constexpr Spring k_release_spring{.stiffness_ = 320.f, .damping_ = 22.f, .mass_ = 1.f};

struct Nodes {
	Handle                     art_{};
	Handle                     title_{}, artist_{};
	array<Handle, k_bar_count> bars_{};
	Handle                     time_l_{}, time_r_{};
	Handle                     time_l_box_{}, time_r_box_{};
	Handle                     track_{}, fill_{}, fill_rest_{};
	Handle                     prev_{}, play_{}, next_{};
	Handle                     prev_icon_{}, play_icon_{}, next_icon_{};
	Handle                     pause_icon_{};
	Handle                     fav_{}, heart_box_{}, heart_outline_{}, heart_fill_{};
};

Nodes              n{};
Handle             bg{};
Handle             content{};
bool               is_running{};
bool               is_playing{};
float              phase{};
Timer::EventHandle ticker{};

string     shown_title{}, shown_artist{}, shown_time_l{}, shown_time_r{};
string     shown_time_shape; // e.g. "0:00" / "00:00" / "0:00:00", sizes the time boxes
FontHandle time_font{};

// Natural (unscaled) size of each glyph node, for press / pop scaling.
struct IconBase {
	Handle h_{};
	float  w_{}, hgt_{};
};

array<IconBase, 6> icon_base{};
size_t             icon_count{};
bool               is_shown_pause{};

// Optimistic play/pause: the UI flips on click and holds that until the player
// confirms (Spotify reports "Paused" ~265 ms after the command) or the timeout
// gives up and shows the real state again.
constexpr auto     k_predict_timeout = milliseconds{1500};
optional<bool>     predicted_playing{};
milliseconds       predicted_position{}; // frozen progress while predicting a pause
Timer::EventHandle predict_timer{};

// Favorite heart, as last displayed.
string shown_fav_uri{};
bool   is_shown_liked{};

Object::Context* context{};

Object::Context& ctx() {
	return *context;
}

void set_text_cached(Handle h, string& cache, const string& s) {
	if(cache == s)
		return;
	cache = s;
	ctx().text(h).set_text(s);
}

[[nodiscard]] string format_time(milliseconds t) {
	const auto total{max<long long>(0, duration_cast<seconds>(t).count())};
	const auto h{total / 3600};
	const auto m{total / 60 % 60};
	const auto s{total % 60};
	return h > 0 ? format("{}:{:02}:{:02}", h, m, s) : format("{}:{:02}", m, s);
}

[[nodiscard]] float playing_level(int i) {
	const float p{phase + static_cast<float>(i) * 0.78f};
	const float primary{(sin(p) + 1.f) * 0.5f};
	const float secondary{(sin(p * 2.f + static_cast<float>(i) * 0.95f) + 1.f) * 0.5f};
	return 0.22f + primary * 0.42f + secondary * 0.24f;
}

[[nodiscard]] float paused_level(int i) {
	constexpr array<float, k_bar_count> k_levels{0.34f, 0.58f, 0.82f, 0.58f, 0.34f};
	return k_levels[static_cast<size_t>(i)];
}

void set_bars(bool should_use_playing) {
	auto& c{ctx()};
	for(int i{0}; i < k_bar_count; ++i) {
		const Handle bar{n.bars_[static_cast<size_t>(i)]};
		const float  level{should_use_playing ? playing_level(i) : paused_level(i)};
		c.set_size(bar, k_bar_w, k_bar_min_h + (k_bar_max_h - k_bar_min_h) * level);
		c.rect(bar).set_color(should_use_playing ? k_bar_playing : k_bar_paused);
	}
}

void fit_time_boxes(const string& longest) {
	string shape{longest};
	for(char& ch : shape)
		if(ch >= '0' && ch <= '9')
			ch = '0';
	if(shape == shown_time_shape)
		return;
	shown_time_shape = shape;

	float w{0.f};
	for(char d{'0'}; d <= '9'; ++d) {
		string sample{shape};
		for(char& ch : sample)
			if(ch == '0')
				ch = d;
		const Text::Bitmap b{Text::draw(sample, time_font, 1.f)};
		w = max({w, static_cast<float>(b.width_), b.advance_x_});
	}
	w = ceil(w) + 1.f;

	auto& c{ctx()};
	c.set_size(n.time_l_box_, w, c.frame(n.time_l_box_).height_);
	c.set_size(n.time_r_box_, w, c.frame(n.time_r_box_).height_);
}

void update_progress() {
	const auto*  s{Media::get_state()};
	milliseconds pos{}, len{};
	if(s && s->has_player_) {
		pos = predicted_playing == false ? predicted_position : Media::get_position();
		len = s->duration_.value_or(milliseconds{});
	}

	const string total{format_time(len)};
	fit_time_boxes(format_time(max(pos, len)));
	set_text_cached(n.time_l_, shown_time_l, format_time(pos));
	set_text_cached(n.time_r_, shown_time_r, total);

	// The fill and a spacer split the track by weight, so the fill follows the
	// track's width on every layout (e.g. while the island is still expanding),
	// not only when this runs.
	auto&       c{ctx()};
	const float fraction{
		len.count() > 0 ? clamp(static_cast<float>(pos.count()) / static_cast<float>(len.count()), 0.f, 1.f) : 0.f};
	c.set_grow(n.fill_, Object::Axis::Horizontal, {.weight_ = fraction});
	c.set_grow(n.fill_rest_, Object::Axis::Horizontal, {.weight_ = 1.f - fraction});
}

void schedule_tick();

void on_tick() {
	ticker = {};
	if(!is_running || !is_playing)
		return;

	phase += 0.18f;
	if(phase > 2.f * numbers::pi_v<float>)
		phase -= 2.f * numbers::pi_v<float>;
	set_bars(true);
	update_progress();

	Island::request_redraw(true);
	schedule_tick();
}

void schedule_tick() {
	if(!is_running || !is_playing || Timer::is_pending(ticker))
		return;
	ticker = Api::push_event(duration_cast<microseconds>(k_tick), on_tick, 0);
}

void set_enabled(Handle button, bool is_enabled) {
	ctx().set_opacity(button, is_enabled ? 1.f : k_dim_opacity);
}

// ---- Favorite (Spotify Liked Songs) ------------------------------------------

[[nodiscard]] string current_spotify_uri() {
	const auto* s{Media::get_state()};
	if(!s || !s->has_player_)
		return {};
	return SpotifyBridge::track_uri_from_mpris(s->service_, s->track_id_, s->media_url_);
}

[[nodiscard]] bool is_spotify_player() {
	const auto* s{Media::get_state()};
	return s && s->has_player_ && SpotifyBridge::is_spotify_service(s->service_);
}

void set_heart_scale(Handle h, float scale) {
	auto&       c{ctx()};
	const float sz{k_heart_size * scale};
	const float off{(k_heart_size - sz) * 0.5f};
	for(const Prop p : {Prop::X, Prop::Y, Prop::W, Prop::H})
		c.cancel_animation(h, p);
	c.set_pos(h, off, off);
	c.set_size(h, sz, sz);
}

void animate_heart_scale(Handle h, float scale, float ms, float delay, AnimCallback done = {}) {
	auto&        c{ctx()};
	const float  sz{k_heart_size * scale};
	const float  off{(k_heart_size - sz) * 0.5f};
	const Timing t{ms, Ease::InOutQuad};
	c.animate(Object::TAnimDesc{.h_ = h, .to_ = off, .t_ = t, .delay_ = delay, .p_ = Prop::X});
	c.animate(Object::TAnimDesc{.h_ = h, .to_ = off, .t_ = t, .delay_ = delay, .p_ = Prop::Y});
	c.animate(Object::TAnimDesc{.h_ = h, .to_ = sz, .t_ = t, .delay_ = delay, .p_ = Prop::W});
	c.animate(
		Object::TAnimDesc{.h_ = h, .to_ = sz, .t_ = t, .delay_ = delay, .p_ = Prop::H, .on_done_ = std::move(done)});
}

void settle_heart(bool is_liked) {
	auto& c{ctx()};
	for(const Handle h : {n.heart_outline_, n.heart_fill_}) {
		c.cancel_animation(h, Prop::Opacity);
		set_heart_scale(h, 1.f);
	}
	c.set_opacity(n.heart_outline_, is_liked ? 0.f : 1.f);
	c.set_opacity(n.heart_fill_, is_liked ? 1.f : 0.f);
}

// Same choreography as the original FavoriteHeart.qml: the outline pulses
// 100% -> 90% -> 100% while the fill grows in from 50% (or shrinks out).
void play_heart(bool is_liked) {
	auto& c{ctx()};
	settle_heart(!is_liked);
	c.set_opacity(n.heart_outline_, 1.f);
	animate_heart_scale(n.heart_outline_, 0.9f, 167.f, 0.f, [] {
		if(is_running)
			animate_heart_scale(n.heart_outline_, 1.f, 166.f, 0.f);
	});

	const auto done = [is_liked] {
		if(is_running)
			settle_heart(is_liked);
	};
	if(is_liked) {
		set_heart_scale(n.heart_fill_, 0.5f);
		c.set_opacity(n.heart_fill_, 0.f);
		animate_heart_scale(n.heart_fill_, 1.f, 267.f, 66.f, done);
		c.animate(Object::TAnimDesc{.h_ = n.heart_fill_,
			.to_                        = 1.f,
			.t_                         = {167.f, Ease::Linear},
			.delay_                     = 66.f,
			.p_                         = Prop::Opacity});
	} else {
		c.set_opacity(n.heart_fill_, 1.f);
		animate_heart_scale(n.heart_fill_, 0.5f, 267.f, 0.f);
		c.animate(Object::TAnimDesc{.h_ = n.heart_fill_,
			.to_                        = 0.f,
			.t_                         = {167.f, Ease::Linear},
			.delay_                     = 100.f,
			.p_                         = Prop::Opacity,
			.on_done_                   = done});
	}
}

void refresh_favorite() {
	auto&      c{ctx()};
	const bool is_visible{is_spotify_player()};
	c.set_visible(n.fav_, is_visible);
	if(!is_visible) {
		shown_fav_uri.clear();
		return;
	}

	const string uri{current_spotify_uri()};
	const auto&  b{SpotifyBridge::get_state()};
	const bool   is_available{b.is_connected_ && b.is_known_ && !uri.empty() && b.track_uri_ == uri};
	const bool   is_liked{is_available && b.is_liked_};
	set_enabled(n.fav_, is_available && !b.is_busy_);

	if(uri != shown_fav_uri) {
		// new track: show its state without animating
		settle_heart(is_liked);
	} else if(is_liked != is_shown_liked) {
		if(is_available)
			play_heart(is_liked);
		else
			settle_heart(is_liked);
	}
	shown_fav_uri  = uri;
	is_shown_liked = is_liked;
}

void on_bridge() {
	if(!is_running)
		return;
	refresh_favorite();
	Island::request_redraw(true);
}

void toggle_favorite() {
	const string uri{current_spotify_uri()};
	const auto&  b{SpotifyBridge::get_state()};
	if(b.is_connected_ && b.is_known_ && !b.is_busy_ && b.track_uri_ == uri)
		SpotifyBridge::set_favorite(uri, !b.is_liked_);
}

void pop_in(Handle icon);

void refresh();

void clear_prediction() {
	predicted_playing.reset();
	Api::cancel_event(predict_timer);
	predict_timer = {};
}

void on_predict_timeout() {
	predict_timer = {};
	if(!is_running || !predicted_playing)
		return;
	predicted_playing.reset(); // never confirmed: show what the player says
	refresh();
	Island::request_redraw(true);
}

void toggle_play() {
	const auto* s{Media::get_state()};
	if(!s || !s->has_player_)
		return;
	const bool  is_from{predicted_playing.value_or(s->playback_ == LyricsMpris::Playback::Playing)};
	const auto& caps{s->capabilities_};
	if(!caps.can_control_ || !(is_from ? caps.can_pause_ : caps.can_play_))
		return; // the player would refuse; don't fake a state change
	Media::play_pause();
	predicted_playing  = !is_from;
	predicted_position = Media::get_position();
	Api::cancel_event(predict_timer);
	predict_timer = Api::push_event(duration_cast<microseconds>(k_predict_timeout), on_predict_timeout, 0);
	refresh();
}

void refresh() {
	auto&       c{ctx()};
	const auto* s{Media::get_state()};
	const bool  has_player{s && s->has_player_};
	const bool  is_actual{has_player && s->playback_ == LyricsMpris::Playback::Playing};
	if(predicted_playing && (*predicted_playing == is_actual || !has_player))
		clear_prediction(); // confirmed (or the player went away)
	const bool is_now_playing{predicted_playing.value_or(is_actual)};

	string artists;
	if(has_player) {
		for(const auto& a : s->artists_) {
			if(!artists.empty())
				artists += ", ";
			artists += a;
		}
	}
	set_text_cached(n.title_, shown_title, has_player && !s->title_.empty() ? s->title_ : string{"Not Playing"});
	set_text_cached(n.artist_, shown_artist, artists);

	const LyricsMpris::Capabilities caps{has_player ? s->capabilities_ : LyricsMpris::Capabilities{}};
	set_enabled(n.prev_, caps.can_control_ && caps.can_previous_);
	set_enabled(n.play_, caps.can_control_ && (caps.can_play_ || caps.can_pause_));
	set_enabled(n.next_, caps.can_control_ && caps.can_next_);

	if(is_now_playing != is_shown_pause) {
		is_shown_pause = is_now_playing;
		const Handle in{is_now_playing ? n.pause_icon_ : n.play_icon_};
		const Handle out{is_now_playing ? n.play_icon_ : n.pause_icon_};
		c.set_visible(out, false);
		c.set_visible(in, true);
		pop_in(in);
	}

	update_progress();
	refresh_favorite();

	if(is_now_playing != is_playing) {
		is_playing = is_now_playing;
		if(!is_playing)
			set_bars(false);
	}
	schedule_tick();
}

void refresh_artwork() {
	Texture tex{};
	if(const Image::Pixels* p{Media::get_artwork()})
		tex.upload(p->width_, p->height_, Texture::Format::RGBA8, p->rgba_);
	const bool is_valid{tex.is_valid()};
	ctx().image(n.art_).set_texture(std::move(tex));
	ctx().set_visible(n.art_, is_valid);
}

void on_media(Media::Update u) {
	if(!is_running)
		return;
	bool is_shown{false}; // lyrics-only updates are not displayed here: no frame for them
	if(Media::has_flag(u, Media::Update::State)) {
		refresh();
		is_shown = true;
	}
	if(Media::has_flag(u, Media::Update::Artwork)) {
		refresh_artwork();
		is_shown = true;
	}
	if(is_shown)
		Island::request_redraw(true);
}

// ---- Buttons ----------------------------------------------------------------

void animate_tone(Handle icon, float tone, const Timing& t) {
	auto& c{ctx()};
	for(const Prop p : {Prop::ColorR, Prop::ColorG, Prop::ColorB})
		c.animate(Object::TAnimDesc{.h_ = icon, .to_ = tone, .t_ = t, .delay_ = 0, .p_ = p});
}

[[nodiscard]] const IconBase* base_of(Handle h) {
	for(size_t i{0}; i < icon_count; ++i)
		if(icon_base[i].h_ == h)
			return &icon_base[i];
	return nullptr;
}

// Scales a glyph about its center. Layout-placed icons only change size (their
// row re-centers them); the hearts sit in a fixed box, so they move too.
void spring_icon_scale(Handle icon, float s, const Spring& sp) {
	auto& c{ctx()};
	if(icon == n.heart_outline_ || icon == n.heart_fill_) {
		const float sz{k_heart_size * s}, off{(k_heart_size - sz) * 0.5f};
		c.animate(Object::SAnimDesc{.h_ = icon, .to_ = off, .s_ = sp, .delay_ = 0, .p_ = Prop::X});
		c.animate(Object::SAnimDesc{.h_ = icon, .to_ = off, .s_ = sp, .delay_ = 0, .p_ = Prop::Y});
		c.animate(Object::SAnimDesc{.h_ = icon, .to_ = sz, .s_ = sp, .delay_ = 0, .p_ = Prop::W});
		c.animate(Object::SAnimDesc{.h_ = icon, .to_ = sz, .s_ = sp, .delay_ = 0, .p_ = Prop::H});
		return;
	}
	if(const IconBase* b{base_of(icon)}) {
		c.animate(Object::SAnimDesc{.h_ = icon, .to_ = b->w_ * s, .s_ = sp, .delay_ = 0, .p_ = Prop::W});
		c.animate(Object::SAnimDesc{.h_ = icon, .to_ = b->hgt_ * s, .s_ = sp, .delay_ = 0, .p_ = Prop::H});
	}
}

// Play <-> pause swap: the incoming glyph pops in from 70% and fades up.
void pop_in(Handle icon) {
	auto& c{ctx()};
	if(const IconBase* b{base_of(icon)}) {
		c.cancel_animation(icon, Prop::W);
		c.cancel_animation(icon, Prop::H);
		c.set_size(icon, b->w_ * 0.7f, b->hgt_ * 0.7f);
	}
	c.set_opacity(icon, 0.f);
	c.animate(
		Object::TAnimDesc{.h_ = icon, .to_ = 1.f, .t_ = {160.f, Ease::OutCubic}, .delay_ = 0, .p_ = Prop::Opacity});
	spring_icon_scale(icon, 1.f, k_release_spring);
}

void set_pressed(Handle button, bool is_pressed) {
	array<Handle, 2> icons{};
	if(button == n.prev_)
		icons = {n.prev_icon_, {}};
	else if(button == n.next_)
		icons = {n.next_icon_, {}};
	else if(button == n.play_)
		icons = {n.play_icon_, n.pause_icon_};
	else if(button == n.fav_)
		icons = {n.heart_outline_, n.heart_fill_};
	else
		return;

	const Timing tone{is_pressed ? 90.f : 300.f, Ease::OutCubic};
	for(const Handle icon : icons) {
		if(!icon)
			continue;
		animate_tone(icon, is_pressed ? k_pressed_tone : 1.f, tone);
		spring_icon_scale(icon, is_pressed ? k_pressed_size : 1.f, is_pressed ? k_press_spring : k_release_spring);
	}
}

void on_button(const Object::Event& e) {
	switch(e.type_) {
		case Object::EventType::Press:
			set_pressed(e.target_, true);
			break;
		case Object::EventType::Release:
			set_pressed(e.target_, false);
			break;
		case Object::EventType::Click:
			if(e.button_ != Object::Button::Left)
				break;
			if(e.target_ == n.prev_)
				Media::previous();
			else if(e.target_ == n.play_)
				toggle_play();
			else if(e.target_ == n.next_)
				Media::next();
			else if(e.target_ == n.fav_)
				toggle_favorite();
			break;
		default:
			break;
	}
}

// ---- Icons ------------------------------------------------------------------

struct V2 {
	float x_{};
	float y_{};
};

[[nodiscard]] V2 operator-(V2 a, V2 b) {
	return {a.x_ - b.x_, a.y_ - b.y_};
}

[[nodiscard]] V2 operator+(V2 a, V2 b) {
	return {a.x_ + b.x_, a.y_ + b.y_};
}

[[nodiscard]] V2 operator*(V2 a, float k) {
	return {a.x_ * k, a.y_ * k};
}

[[nodiscard]] float dot(V2 a, V2 b) {
	return a.x_ * b.x_ + a.y_ * b.y_;
}

[[nodiscard]] float cross(V2 a, V2 b) {
	return a.x_ * b.y_ - a.y_ * b.x_;
}

[[nodiscard]] float len(V2 a) {
	return sqrt(dot(a, a));
}

// Exact signed distance to a triangle (negative inside).
[[nodiscard]] float sd_triangle(V2 p, V2 a, V2 b, V2 c) {
	const V2    e0{b - a}, e1{c - b}, e2{a - c};
	const V2    v0{p - a}, v1{p - b}, v2{p - c};
	const V2    q0{v0 - e0 * clamp(dot(v0, e0) / dot(e0, e0), 0.f, 1.f)};
	const V2    q1{v1 - e1 * clamp(dot(v1, e1) / dot(e1, e1), 0.f, 1.f)};
	const V2    q2{v2 - e2 * clamp(dot(v2, e2) / dot(e2, e2), 0.f, 1.f)};
	const float s{cross(e0, e2) > 0.f ? 1.f : -1.f};
	const float d{min({dot(q0, q0), dot(q1, q1), dot(q2, q2)})};
	const float w{min({s * cross(v0, e0), s * cross(v1, e1), s * cross(v2, e2)})};
	return -sqrt(d) * (w > 0.f ? 1.f : -1.f);
}

// Triangle with corner radius r whose outer silhouette still spans a, b, c.
[[nodiscard]] float sd_round_triangle(V2 p, V2 a, V2 b, V2 c, float r) {
	const float la{len(c - b)}, lb{len(a - c)}, lc{len(b - a)};
	const float per{la + lb + lc};
	const V2    in{(a * la + b * lb + c * lc) * (1.f / per)};
	const float in_r{abs(cross(b - a, c - a)) / per};
	const float k{max(in_r - r, 0.f) / in_r};
	const auto  shrink = [&](V2 v) { return in + (v - in) * k; };
	return sd_triangle(p, shrink(a), shrink(b), shrink(c)) - r;
}

[[nodiscard]] float sd_round_rect(V2 p, V2 origin, V2 size, float r) {
	const V2 half{size * 0.5f};
	const V2 d{p - (origin + half)};
	const V2 q{abs(d.x_) - half.x_ + r, abs(d.y_) - half.y_ + r};
	return len({max(q.x_, 0.f), max(q.y_, 0.f)}) + min(max(q.x_, q.y_), 0.f) - r;
}

// Coverage mask (R8) of `sdf` over a w x h logical box, at the output scale.
[[nodiscard]] Texture
rasterize(float scale, float sub_x, float sub_y, float w, float h, const function<float(V2)>& sdf) {
	scale = max(scale, 1.f);
	const int pw{static_cast<int>(ceil(w * scale + sub_x))};
	const int ph{static_cast<int>(ceil(h * scale + sub_y))};

	vector<uint8_t> px(static_cast<size_t>(pw) * static_cast<size_t>(ph));
	for(int y{0}; y < ph; ++y)
		for(int x{0}; x < pw; ++x) {
			const V2 p{(static_cast<float>(x) + 0.5f - sub_x) / scale, (static_cast<float>(y) + 0.5f - sub_y) / scale};
			const float d{sdf(p) * scale}; // in physical pixels
			const float coverage{clamp(0.5f - d, 0.f, 1.f)};
			px[static_cast<size_t>(y) * static_cast<size_t>(pw) + static_cast<size_t>(x)] =
				static_cast<uint8_t>(lround(coverage * 255.f));
		}

	Texture tex{};
	tex.upload(pw, ph, Texture::Format::R8, px);
	tex.set_scale(scale);
	return tex;
}

constexpr float k_aa{1.f};

enum class Glyph : uint8_t {
	Play,
	Pause,
	Forward,
	Backward,
	Heart,
	HeartFilled
};

// Heart in unit space: tip at the origin, lobes up to y ~= 1.1, |x| <= ~0.6 (Inigo Quilez).
[[nodiscard]] float sd_heart(V2 p) {
	p.x_ = abs(p.x_);
	if(p.y_ + p.x_ > 1.f)
		return len(p - V2{0.25f, 0.75f}) - numbers::sqrt2_v<float> / 4.f;
	const float t{0.5f * max(p.x_ + p.y_, 0.f)};
	const V2    a{p - V2{0.f, 1.f}};
	const V2    b{p - V2{t, t}};
	return sqrt(min(dot(a, a), dot(b, b))) * (p.x_ - p.y_ > 0.f ? 1.f : -1.f);
}

[[nodiscard]] Texture make_glyph(Glyph g, float scale, float sub_x, float sub_y) {
	switch(g) {
		case Glyph::Play: {
			constexpr float k_height{22.f * k_icon_scale};
			constexpr float k_width{k_height * 0.88f};
			constexpr float k_radius{2.6f * k_icon_scale};
			constexpr float k_nudge{2.f * k_icon_scale};
			constexpr float k_x0{k_aa + k_nudge};
			return rasterize(scale, sub_x, sub_y, k_width + k_nudge + 2 * k_aa, k_height + 2 * k_aa, [=](V2 p) {
				return sd_round_triangle(p,
					{k_x0, k_aa},
					{k_x0, k_aa + k_height},
					{k_x0 + k_width, k_aa + k_height * 0.5f},
					k_radius);
			});
		}
		case Glyph::Pause: {
			constexpr float k_height{21.f * k_icon_scale};
			constexpr float k_bar{6.f * k_icon_scale};
			constexpr float k_gap{6.f * k_icon_scale};
			constexpr float k_radius{1.8f * k_icon_scale};
			return rasterize(scale, sub_x, sub_y, 2 * k_bar + k_gap + 2 * k_aa, k_height + 2 * k_aa, [=](V2 p) {
				return min(sd_round_rect(p, {k_aa, k_aa}, {k_bar, k_height}, k_radius),
					sd_round_rect(p, {k_aa + k_bar + k_gap, k_aa}, {k_bar, k_height}, k_radius));
			});
		}
		case Glyph::Forward:
		case Glyph::Backward: {
			constexpr float k_height{16.f * k_icon_scale};
			constexpr float k_width{15.f * k_icon_scale};
			constexpr float k_step{k_width - 2.5f * k_icon_scale};
			constexpr float k_radius{2.f * k_icon_scale};
			constexpr float k_total{k_step + k_width};
			const bool      is_flipped{g == Glyph::Backward};
			return rasterize(scale, sub_x, sub_y, k_total + 2 * k_aa, k_height + 2 * k_aa, [=](V2 p) {
				if(is_flipped)
					p.x_ = k_total + 2 * k_aa - p.x_;
				float d{1e9f};
				for(const float x_pos : {k_aa, k_aa + k_step})
					d = min(d,
						sd_round_triangle(p,
							{x_pos, k_aa},
							{x_pos, k_aa + k_height},
							{x_pos + k_width, k_aa + k_height * 0.5f},
							k_radius));
				return d;
			});
		}
		case Glyph::Heart:
		case Glyph::HeartFilled: {
			// 20 x ~18.3 heart centered in the 24 box, tip slightly rounded; the
			// outline is a 2px stroke inside the filled silhouette so both match.
			constexpr float k_width{20.f * k_icon_scale};
			constexpr float k_radius{1.f * k_icon_scale};
			constexpr float k_stroke{2.f * k_icon_scale};
			constexpr float k_factor{(k_width - 2 * k_radius) / 1.2071f};
			constexpr float k_height{1.1036f * k_factor + 2 * k_radius};
			constexpr float k_bottom{(k_heart_size + k_height) * 0.5f - k_radius};
			const bool      is_filled{g == Glyph::HeartFilled};
			return rasterize(scale, sub_x, sub_y, k_heart_size, k_heart_size, [=](V2 p) {
				const float d{
					sd_heart({(p.x_ - k_heart_size * 0.5f) / k_factor, (k_bottom - p.y_) / k_factor}) * k_factor -
					k_radius};
				return is_filled ? d : abs(d + k_stroke * 0.5f) - k_stroke * 0.5f;
			});
		}
	}
	return {};
}

// ---- Construction -----------------------------------------------------------

Object::LayoutData row(float spacing,
	Object::Alignment        align   = Object::Alignment::Center,
	Object::Justify          justify = Object::Justify::Start) {
	Object::LayoutData l{};
	l.dir_     = Object::Direction::Row;
	l.spacing_ = spacing;
	l.align_   = align;
	l.justify_ = justify;
	return l;
}

Handle make_button(Handle parent, float slot_center) {
	auto&        c{ctx()};
	const Handle b{c.create_group(parent,
		Frame{
			.x_      = k_content_w * slot_center - k_button_w * 0.5f,
			.y_      = 0.f,
			.width_  = k_button_w,
			.height_ = k_controls_h,
		})};
	c.set_layout(b, row(0.f, Object::Alignment::Center, Object::Justify::Center));
	c.on(b, Object::EventType::Press | Object::EventType::Release | Object::EventType::Click, on_button);
	return b;
}

Handle make_icon(Handle parent, Glyph glyph) {
	auto&        c{ctx()};
	const Handle t{c.create_text(parent, {})};
	// Redrawn by the scene when the output scale changes, so edges stay crisp.
	c.text(t)
		.set_color(Color::k_white)
		.set_raster(
			[](float scale, float sub_x, float sub_y, uintptr_t g) {
				return make_glyph(static_cast<Glyph>(g), scale, sub_x, sub_y);
			},
			static_cast<uintptr_t>(glyph));
	// Fixed at the glyph's natural size so press / pop animations can scale it.
	const float w{c.text(t).get_texture_width()}, h{c.text(t).get_texture_height()};
	c.set_size_mode(t, Object::Axis::Horizontal, Object::SizeMode::Fixed);
	c.set_size_mode(t, Object::Axis::Vertical, Object::SizeMode::Fixed);
	c.set_size(t, w, h);
	if(icon_count < icon_base.size())
		icon_base[icon_count++] = {t, w, h};
	return t;
}

void build(const MusicPlayerView::Fonts& f) {
	auto& c{ctx()};
	icon_count     = 0;
	is_shown_pause = false;

	{
		Object::LayoutData l{};
		l.dir_   = Object::Direction::Column;
		l.align_ = Object::Alignment::Center;
		c.set_layout(bg, l);
	}
	c.set_clip(bg, true);
	content = c.create_group(bg, Frame{.x_ = 0.f, .y_ = 0.f, .width_ = k_expanded_w, .height_ = k_expanded_h});

	Object::LayoutData column{};
	column.dir_     = Object::Direction::Column;
	column.padding_ = Object::Insets::xy(k_pad_x, k_pad_y);
	column.spacing_ = 8.f;
	c.set_layout(content, column);

	// Header: art | title + artist | visualizer
	const Handle header{c.create_group(content, Frame{.x_ = 0.f, .y_ = 0.f, .width_ = 0.f, .height_ = k_art_size})};
	c.set_size_mode(header, Object::Axis::Horizontal, Object::SizeMode::Grow);
	c.set_layout(header, row(16.f));

	const Handle art_box{c.create_rect(header,
		Object::RectDesc{
			.frame_  = {0, 0, k_art_size, k_art_size},
			.color_  = k_art_placeholder,
			.radius_ = 10.f,
		})};
	n.art_ = c.create_image(art_box,
		Object::ImageDesc{.frame_ = {0, 0, k_art_size, k_art_size}, .texture_ = {}, .radius_ = 10.f});
	c.set_size_mode(n.art_, Object::Axis::Horizontal, Object::SizeMode::Fixed);
	c.set_size_mode(n.art_, Object::Axis::Vertical, Object::SizeMode::Fixed);
	c.set_visible(n.art_, false);

	const Handle info{c.create_group(header, Frame{.x_ = 0.f, .y_ = 0.f, .width_ = 0.f, .height_ = k_art_size})};
	c.set_size_mode(info, Object::Axis::Horizontal, Object::SizeMode::Grow);
	c.set_clip(info, true); // long titles are cut at the visualizer instead of overflowing
	{
		Object::LayoutData l{};
		l.dir_     = Object::Direction::Column;
		l.spacing_ = 4.f;
		l.justify_ = Object::Justify::Center;
		c.set_layout(info, l);
	}
	n.title_  = c.create_text(info, {});
	n.artist_ = c.create_text(info, {});
	c.text(n.title_).set_font(f.title_).set_color(Color::k_white);
	c.text(n.artist_).set_font(f.artist_).set_color(k_secondary);

	const Handle viz{c.create_group(header, Frame{.x_ = 0.f, .y_ = 0.f, .width_ = 32.f, .height_ = k_bar_max_h})};
	c.set_layout(viz, row(3.f, Object::Alignment::Center, Object::Justify::Center));
	for(int i{0}; i < k_bar_count; ++i) {
		const float h{k_bar_min_h + (k_bar_max_h - k_bar_min_h) * paused_level(i)};
		n.bars_[static_cast<size_t>(i)] = c.create_rect(viz,
			Object::RectDesc{
				.frame_  = {0, 0, k_bar_w, h},
				.color_  = k_bar_paused,
				.radius_ = 2.f,
			});
	}

	const Handle progress{c.create_group(content, Frame{.x_ = 0.f, .y_ = 0.f, .width_ = 0.f, .height_ = 16.f})};
	c.set_size_mode(progress, Object::Axis::Horizontal, Object::SizeMode::Grow);
	c.set_layout(progress, row(12.f));
	n.time_l_box_ = c.create_group(progress, Frame{.x_ = 0.f, .y_ = 0.f, .width_ = 0.f, .height_ = 16.f});
	n.track_ = c.create_rect(progress, Object::RectDesc{.frame_ = {0, 0, 0, 6.f}, .color_ = k_track, .radius_ = 3.f});
	n.time_r_box_ = c.create_group(progress, Frame{.x_ = 0.f, .y_ = 0.f, .width_ = 0.f, .height_ = 16.f});
	c.set_layout(n.time_l_box_, row(0.f, Object::Alignment::Center, Object::Justify::Start));
	c.set_layout(n.time_r_box_, row(0.f, Object::Alignment::Center, Object::Justify::End));
	n.time_l_ = c.create_text(n.time_l_box_, {});
	n.time_r_ = c.create_text(n.time_r_box_, {});
	time_font = f.time_;
	c.set_size_mode(n.track_, Object::Axis::Horizontal, Object::SizeMode::Grow);
	c.text(n.time_l_).set_font(f.time_).set_color(k_secondary);
	c.text(n.time_r_).set_font(f.time_).set_color(k_secondary);
	c.set_layout(n.track_, row(0.f));
	n.fill_ =
		c.create_rect(n.track_, Object::RectDesc{.frame_ = {0, 0, 0, 6.f}, .color_ = Color::k_white, .radius_ = 3.f});
	n.fill_rest_ = c.create_group(n.track_, Frame{.x_ = 0.f, .y_ = 0.f, .width_ = 0.f, .height_ = 6.f});

	// Controls (absolute slots, no layout)
	const Handle controls{c.create_group(content, Frame{.x_ = 0.f, .y_ = 0.f, .width_ = 0.f, .height_ = k_controls_h})};
	c.set_size_mode(controls, Object::Axis::Horizontal, Object::SizeMode::Grow);

	n.prev_       = make_button(controls, k_slot_center[0]);
	n.play_       = make_button(controls, k_slot_center[1]);
	n.next_       = make_button(controls, k_slot_center[2]);
	n.prev_icon_  = make_icon(n.prev_, Glyph::Backward);
	n.play_icon_  = make_icon(n.play_, Glyph::Play);
	n.pause_icon_ = make_icon(n.play_, Glyph::Pause);
	n.next_icon_  = make_icon(n.next_, Glyph::Forward);
	// Initial glyph without the pop animation (that is for changes while open).
	{
		const auto* s{Media::get_state()};
		is_shown_pause = s && s->has_player_ && s->playback_ == LyricsMpris::Playback::Playing;
		c.set_visible(n.play_icon_, !is_shown_pause);
		c.set_visible(n.pause_icon_, is_shown_pause);
	}

	// Favorite: two stacked hearts in a fixed box so they can scale about the center.
	n.fav_       = make_button(controls, k_favorite_slot);
	n.heart_box_ = c.create_group(n.fav_, Frame{.x_ = 0.f, .y_ = 0.f, .width_ = k_heart_size, .height_ = k_heart_size});
	for(Handle* h : {&n.heart_outline_, &n.heart_fill_}) {
		*h = make_icon(n.heart_box_, h == &n.heart_outline_ ? Glyph::Heart : Glyph::HeartFilled);
		c.set_size_mode(*h, Object::Axis::Horizontal, Object::SizeMode::Fixed);
		c.set_size_mode(*h, Object::Axis::Vertical, Object::SizeMode::Fixed);
		c.set_size(*h, k_heart_size, k_heart_size);
	}
	shown_fav_uri.clear();
	is_shown_liked = false;
	settle_heart(false);
}

}

namespace MusicPlayerView {
void start(Object::Context& arg_ctx, Handle arg_bg, const Fonts& fonts) {
	if(!arg_bg) {
		Log::logger(Log::LogLevel::Error, "MusicPlayerView: invalid background");
		return;
	}

	stop();
	context = &arg_ctx;
	bg      = arg_bg;
	SpotifyBridge::reload(); // one stat; picks up a setup run after launch
	build(fonts);

	is_running = true;
	is_playing = false;
	phase      = 0.f;
	shown_title.clear();
	shown_artist.clear();
	shown_time_l.clear();
	shown_time_r.clear();
	shown_time_shape.clear();

	ctx().layout();
	refresh();
	refresh_artwork();

	auto& c{ctx()};
	c.set_opacity(content, 0.f);
	c.animate(Object::TAnimDesc{.h_ = content,
		.to_                        = 1.f,
		.t_                         = k_reveal_fade,
		.delay_                     = k_reveal_delay,
		.p_                         = Prop::Opacity});
	Media::set_listener(on_media);
	SpotifyBridge::set_listener(on_bridge);
	Media::resync_position(); // the local estimate may have drifted while hidden
}

void stop() {
	if(!is_running)
		return;
	is_running = false;
	is_playing = false;
	Media::set_listener(nullptr);
	SpotifyBridge::set_listener(nullptr);
	clear_prediction();
	Api::cancel_event(ticker);
	ticker = {};

	auto&          c{ctx()};
	vector<Handle> kids{};
	for(const Handle h : c.children(bg))
		kids.push_back(h);
	for(const Handle h : kids)
		c.destroy(h);
	c.clear_layout(bg);
	c.set_clip(bg, false);

	n       = {};
	content = {};
	bg      = {};
}

}
