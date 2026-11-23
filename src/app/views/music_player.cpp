#include "app/views/music_player.hpp"

#include "app/API.hpp"
#include "app/island.hpp"
#include "app/state_manager.hpp"
#include "music/media.hpp"
#include "music/spotify_bridge.hpp"
#include "backend/timer.hpp"
#include "graphics/renderer.hpp"
#include "graphics/text.hpp"
#include "graphics/texture.hpp"
#include "utils/log.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <format>
#include <functional>
#include <numbers>
#include <optional>
#include <string>

using namespace std;
using namespace std::chrono;
using Object::Handle;

namespace {

const Color kArtPlaceholder{"#2c2c2e"};
const Color kSecondary{"#8e8e93"};
const Color kTrack{"#333333"};
const Color kBarPlaying{"#b56cff"};
const Color kBarPaused{"#5f4b72"};

constexpr float kPadX        = 20.f;
constexpr float kPadY        = 18.f;
constexpr float kExpandedW   = 410.f;
constexpr float kExpandedH   = 165.f;
constexpr float kContentW    = kExpandedW - 2 * kPadX;
constexpr float kArtSize     = 60.f;
constexpr float kBarW        = 3.f;
constexpr float kBarMinH     = 6.f;
constexpr float kBarMaxH     = 22.f;
constexpr float kButtonW     = 56.f;
// Taller than the glyphs so the row's center sits halfway between the progress
// bar and the island's bottom edge (equal space above and below the icons).
constexpr float kControlsH   = 42.f;
constexpr float kDimOpacity  = 0.45f;
constexpr float kPressedTone = 0.7f;  // icon brightness while held
constexpr float kPressedSize = 0.86f; // icon scale while held

constexpr int kBarCount = 5;

constexpr float kIconScale = 1.25f; // all transport glyph dimensions below are x this
constexpr float kHeartSize = 24.f * kIconScale;

// Transport is centered on the island; the heart's right edge lines up with
// the end of the progress row (like AirPlay in iOS's player).
constexpr float           kSlotStep = 0.225f;
constexpr array<float, 3> kSlotCenter{0.5f - kSlotStep, 0.5f, 0.5f + kSlotStep};
constexpr float           kFavoriteSlot = 1.f - 20.f * kIconScale * 0.5f / kContentW;

constexpr auto kTick = milliseconds{64};

constexpr Timing kRevealFade{260.f, Ease::OutCubic};
constexpr float  kRevealDelay = 70.f;

// Press feedback: quick, critically damped squeeze; release springs back with a
// slight overshoot (damping ratio ~0.6), like iOS buttons.
constexpr Spring kPressSpring{520.f, 46.f, 1.f};
constexpr Spring kReleaseSpring{320.f, 22.f, 1.f};


struct Nodes {
    Handle                   art{};
    Handle                   title{}, artist{};
    array<Handle, kBarCount> bars{};
    Handle                   time_l{},     time_r{};
    Handle                   time_l_box{}, time_r_box{};
    Handle                   track{},      fill{},      fill_rest{};
    Handle                   prev{},       play{},      next{};
    Handle                   prev_icon{},  play_icon{}, next_icon{};
    Handle                   pause_icon{};
    Handle                   fav{}, heart_box{}, heart_outline{}, heart_fill{};
};

Nodes              n{};
Handle             bg{};
Handle             content{};
bool               running{};
bool               playing{};
float              phase{};
Timer::EventHandle ticker{};

string     shown_title, shown_artist, shown_time_l, shown_time_r;
string     shown_time_shape; // e.g. "0:00" / "00:00" / "0:00:00", sizes the time boxes
FontHandle time_font{};

// Natural (unscaled) size of each glyph node, for press / pop scaling.
struct IconBase {
    Handle h{};
    float  w{}, hgt{};
};
array<IconBase, 6> icon_base{};
size_t             icon_count{};
bool               shown_pause{};

// Optimistic play/pause: the UI flips on click and holds that until the player
// confirms (Spotify reports "Paused" ~265 ms after the command) or the timeout
// gives up and shows the real state again.
constexpr auto           kPredictTimeout = milliseconds{1500};
optional<bool>           predicted_playing{};
milliseconds             predicted_position{}; // frozen progress while predicting a pause
Timer::EventHandle       predict_timer{};

// Favorite heart, as last displayed.
string shown_fav_uri;
bool   shown_liked{};

Object::Context& ctx() { return StateManager::context(); }

void set_text_cached(Handle h, string& cache, const string& s) {
    if (cache == s)
        return;
    cache = s;
    ctx().text(h).set_text(s);
}

[[nodiscard]] string format_time(milliseconds t) {
    const auto total = max<long long>(0, duration_cast<seconds>(t).count());
    const auto h     = total / 3600;
    const auto m     = total / 60 % 60;
    const auto s     = total % 60;
    return h > 0 ? format("{}:{:02}:{:02}", h, m, s) : format("{}:{:02}", m, s);
}

[[nodiscard]] float playing_level(int i) {
    const float p         = phase + static_cast<float>(i) * 0.78f;
    const float primary   = (sin(p) + 1.f) * 0.5f;
    const float secondary = (sin(p * 2.f + static_cast<float>(i) * 0.95f) + 1.f) * 0.5f;
    return 0.22f + primary * 0.42f + secondary * 0.24f;
}

[[nodiscard]] float paused_level(int i) {
    constexpr array<float, kBarCount> levels{0.34f, 0.58f, 0.82f, 0.58f, 0.34f};
    return levels[static_cast<size_t>(i)];
}

void set_bars(bool use_playing) {
    auto& c = ctx();
    for (int i = 0; i < kBarCount; ++i) {
        const Handle bar   = n.bars[static_cast<size_t>(i)];
        const float  level = use_playing ? playing_level(i) : paused_level(i);
        c.set_size(bar, kBarW, kBarMinH + (kBarMaxH - kBarMinH) * level);
        c.rect(bar).set_color(use_playing ? kBarPlaying : kBarPaused);
    }
}

void fit_time_boxes(const string& longest) {
    string shape = longest;
    for (char& ch : shape)
        if (ch >= '0' && ch <= '9')
            ch = '0';
    if (shape == shown_time_shape)
        return;
    shown_time_shape = shape;

    float w = 0.f;
    for (char d = '0'; d <= '9'; ++d) {
        string sample = shape;
        for (char& ch : sample)
            if (ch == '0')
                ch = d;
        const Text::Bitmap b = Text::draw(sample, time_font, 1.f);
        w                    = max({w, static_cast<float>(b.width), b.advance_x});
    }
    w = ceil(w) + 1.f;

    auto& c = ctx();
    c.set_size(n.time_l_box, w, c.frame(n.time_l_box).height);
    c.set_size(n.time_r_box, w, c.frame(n.time_r_box).height);
}

void update_progress() {
    const auto*  s = Media::state();
    milliseconds pos{}, len{};
    if (s && s->has_player) {
        pos = predicted_playing == false ? predicted_position : Media::position();
        len = s->duration.value_or(milliseconds{});
    }

    const string total = format_time(len);
    fit_time_boxes(format_time(max(pos, len)));
    set_text_cached(n.time_l, shown_time_l, format_time(pos));
    set_text_cached(n.time_r, shown_time_r, total);

    // The fill and a spacer split the track by weight, so the fill follows the
    // track's width on every layout (e.g. while the island is still expanding),
    // not only when this runs.
    auto&       c        = ctx();
    const float fraction = len.count() > 0
        ? clamp(static_cast<float>(pos.count()) / static_cast<float>(len.count()), 0.f, 1.f)
        : 0.f;
    c.set_grow(n.fill, Object::Axis::Horizontal, {.weight = fraction});
    c.set_grow(n.fill_rest, Object::Axis::Horizontal, {.weight = 1.f - fraction});
}

void schedule_tick();

void on_tick() {
    ticker = {};
    if (!running || !playing)
        return;

    phase += 0.18f;
    if (phase > 2.f * numbers::pi_v<float>)
        phase -= 2.f * numbers::pi_v<float>;
    set_bars(true);
    update_progress();

    Island::request_redraw(true);
    schedule_tick();
}

void schedule_tick() {
    if (!running || !playing || Timer::is_pending(ticker))
        return;
    ticker = API::push_event(duration_cast<microseconds>(kTick), on_tick, 0);
}

void set_enabled(Handle button, bool enabled) {
    ctx().set_opacity(button, enabled ? 1.f : kDimOpacity);
}

// ---- Favorite (Spotify Liked Songs) ------------------------------------------

[[nodiscard]] string current_spotify_uri() {
    const auto* s = Media::state();
    if (!s || !s->has_player)
        return {};
    return SpotifyBridge::track_uri_from_mpris(s->service, s->track_id, s->media_url);
}

[[nodiscard]] bool spotify_player() {
    const auto* s = Media::state();
    return s && s->has_player && SpotifyBridge::is_spotify_service(s->service);
}

void set_heart_scale(Handle h, float scale) {
    auto&       c   = ctx();
    const float sz  = kHeartSize * scale;
    const float off = (kHeartSize - sz) * 0.5f;
    for (const Prop p : {Prop::X, Prop::Y, Prop::W, Prop::H})
        c.cancel_animation(h, p);
    c.set_pos(h, off, off);
    c.set_size(h, sz, sz);
}

void animate_heart_scale(Handle h, float scale, float ms, float delay, AnimCallback done = {}) {
    auto&        c   = ctx();
    const float  sz  = kHeartSize * scale;
    const float  off = (kHeartSize - sz) * 0.5f;
    const Timing t{ms, Ease::InOutQuad};
    c.animate(Object::TAnimDesc{.h = h, .to = off, .t = t, .delay = delay, .p = Prop::X});
    c.animate(Object::TAnimDesc{.h = h, .to = off, .t = t, .delay = delay, .p = Prop::Y});
    c.animate(Object::TAnimDesc{.h = h, .to = sz, .t = t, .delay = delay, .p = Prop::W});
    c.animate(Object::TAnimDesc{.h = h, .to = sz, .t = t, .delay = delay, .p = Prop::H, .on_done = std::move(done)});
}

void settle_heart(bool liked) {
    auto& c = ctx();
    for (const Handle h : {n.heart_outline, n.heart_fill}) {
        c.cancel_animation(h, Prop::Opacity);
        set_heart_scale(h, 1.f);
    }
    c.set_opacity(n.heart_outline, liked ? 0.f : 1.f);
    c.set_opacity(n.heart_fill, liked ? 1.f : 0.f);
}

// Same choreography as the original FavoriteHeart.qml: the outline pulses
// 100% -> 90% -> 100% while the fill grows in from 50% (or shrinks out).
void play_heart(bool liked) {
    auto& c = ctx();
    settle_heart(!liked);
    c.set_opacity(n.heart_outline, 1.f);
    animate_heart_scale(n.heart_outline, 0.9f, 167.f, 0.f, [] {
        if (running)
            animate_heart_scale(n.heart_outline, 1.f, 166.f, 0.f);
    });

    const auto done = [liked] {
        if (running)
            settle_heart(liked);
    };
    if (liked) {
        set_heart_scale(n.heart_fill, 0.5f);
        c.set_opacity(n.heart_fill, 0.f);
        animate_heart_scale(n.heart_fill, 1.f, 267.f, 66.f, done);
        c.animate(Object::TAnimDesc{.h = n.heart_fill, .to = 1.f, .t = {167.f, Ease::Linear}, .delay = 66.f, .p = Prop::Opacity});
    }
    else {
        c.set_opacity(n.heart_fill, 1.f);
        animate_heart_scale(n.heart_fill, 0.5f, 267.f, 0.f);
        c.animate(Object::TAnimDesc{.h = n.heart_fill, .to = 0.f, .t = {167.f, Ease::Linear}, .delay = 100.f, .p = Prop::Opacity, .on_done = done});
    }
}

void refresh_favorite() {
    auto&       c       = ctx();
    const bool  visible = spotify_player();
    c.set_visible(n.fav, visible);
    if (!visible) {
        shown_fav_uri.clear();
        return;
    }

    const string uri       = current_spotify_uri();
    const auto&  b         = SpotifyBridge::state();
    const bool   available = b.connected && b.known && !uri.empty() && b.track_uri == uri;
    const bool   liked     = available && b.liked;
    set_enabled(n.fav, available && !b.busy);

    if (uri != shown_fav_uri) // new track: show its state without animating
        settle_heart(liked);
    else if (liked != shown_liked) {
        if (available)
            play_heart(liked);
        else
            settle_heart(liked);
    }
    shown_fav_uri = uri;
    shown_liked   = liked;
}

void on_bridge() {
    if (!running)
        return;
    refresh_favorite();
    Island::request_redraw(true);
}

void toggle_favorite() {
    const string uri = current_spotify_uri();
    const auto&  b   = SpotifyBridge::state();
    if (b.connected && b.known && !b.busy && b.track_uri == uri)
        SpotifyBridge::set_favorite(uri, !b.liked);
}

void pop_in(Handle icon);
void refresh();

void clear_prediction() {
    predicted_playing.reset();
    API::cancel_event(predict_timer);
    predict_timer = {};
}

void on_predict_timeout() {
    predict_timer = {};
    if (!running || !predicted_playing)
        return;
    predicted_playing.reset(); // never confirmed: show what the player says
    refresh();
    Island::request_redraw(true);
}

void toggle_play() {
    const auto* s = Media::state();
    if (!s || !s->has_player)
        return;
    const bool  from = predicted_playing.value_or(s->playback == lyricsmpris::Playback::Playing);
    const auto& caps = s->capabilities;
    if (!caps.control || !(from ? caps.pause : caps.play))
        return; // the player would refuse; don't fake a state change
    Media::play_pause();
    predicted_playing  = !from;
    predicted_position = Media::position();
    API::cancel_event(predict_timer);
    predict_timer = API::push_event(duration_cast<microseconds>(kPredictTimeout), on_predict_timeout, 0);
    refresh();
}

void refresh() {
    auto&       c           = ctx();
    const auto* s           = Media::state();
    const bool  has         = s && s->has_player;
    const bool  actual      = has && s->playback == lyricsmpris::Playback::Playing;
    if (predicted_playing && (*predicted_playing == actual || !has))
        clear_prediction(); // confirmed (or the player went away)
    const bool now_playing = predicted_playing.value_or(actual);

    string artists;
    if (has) {
        for (const auto& a : s->artists) {
            if (!artists.empty())
                artists += ", ";
            artists += a;
        }
    }
    set_text_cached(n.title, shown_title, has && !s->title.empty() ? s->title : string{"Not Playing"});
    set_text_cached(n.artist, shown_artist, artists);

    const lyricsmpris::Capabilities caps = has ? s->capabilities : lyricsmpris::Capabilities{};
    set_enabled(n.prev, caps.control && caps.previous);
    set_enabled(n.play, caps.control && (caps.play || caps.pause));
    set_enabled(n.next, caps.control && caps.next);

    if (now_playing != shown_pause) {
        shown_pause = now_playing;
        const Handle in  = now_playing ? n.pause_icon : n.play_icon;
        const Handle out = now_playing ? n.play_icon : n.pause_icon;
        c.set_visible(out, false);
        c.set_visible(in, true);
        pop_in(in);
    }

    update_progress();
    refresh_favorite();

    if (now_playing != playing) {
        playing = now_playing;
        if (!playing)
            set_bars(false);
    }
    schedule_tick();
}

void refresh_artwork() {
    Texture tex{};
    if (const Image::Pixels* p = Media::artwork())
        tex.upload(p->width, p->height, Texture::Format::RGBA8, p->rgba);
    const bool valid = tex.valid();
    ctx().image(n.art).set_texture(std::move(tex));
    ctx().set_visible(n.art, valid);
}

void on_media(Media::Update u) {
    if (!running)
        return;
    bool shown = false; // lyrics-only updates are not displayed here: no frame for them
    if (Media::contains(u, Media::Update::State)) {
        refresh();
        shown = true;
    }
    if (Media::contains(u, Media::Update::Artwork)) {
        refresh_artwork();
        shown = true;
    }
    if (shown)
        Island::request_redraw(true);
}

// ---- Buttons ----------------------------------------------------------------

void animate_tone(Handle icon, float tone, const Timing& t) {
    auto& c = ctx();
    for (const Prop p : {Prop::ColorR, Prop::ColorG, Prop::ColorB})
        c.animate(Object::TAnimDesc{.h = icon, .to = tone, .t = t, .delay = 0, .p = p});
}

[[nodiscard]] const IconBase* base_of(Handle h) {
    for (size_t i = 0; i < icon_count; ++i)
        if (icon_base[i].h == h)
            return &icon_base[i];
    return nullptr;
}

// Scales a glyph about its center. Layout-placed icons only change size (their
// row re-centers them); the hearts sit in a fixed box, so they move too.
void spring_icon_scale(Handle icon, float s, const Spring& sp) {
    auto& c = ctx();
    if (icon == n.heart_outline || icon == n.heart_fill) {
        const float sz = kHeartSize * s, off = (kHeartSize - sz) * 0.5f;
        c.animate(Object::SAnimDesc{.h = icon, .to = off, .s = sp, .delay = 0, .p = Prop::X});
        c.animate(Object::SAnimDesc{.h = icon, .to = off, .s = sp, .delay = 0, .p = Prop::Y});
        c.animate(Object::SAnimDesc{.h = icon, .to = sz, .s = sp, .delay = 0, .p = Prop::W});
        c.animate(Object::SAnimDesc{.h = icon, .to = sz, .s = sp, .delay = 0, .p = Prop::H});
        return;
    }
    if (const IconBase* b = base_of(icon)) {
        c.animate(Object::SAnimDesc{.h = icon, .to = b->w * s, .s = sp, .delay = 0, .p = Prop::W});
        c.animate(Object::SAnimDesc{.h = icon, .to = b->hgt * s, .s = sp, .delay = 0, .p = Prop::H});
    }
}

// Play <-> pause swap: the incoming glyph pops in from 70% and fades up.
void pop_in(Handle icon) {
    auto& c = ctx();
    if (const IconBase* b = base_of(icon)) {
        c.cancel_animation(icon, Prop::W);
        c.cancel_animation(icon, Prop::H);
        c.set_size(icon, b->w * 0.7f, b->hgt * 0.7f);
    }
    c.set_opacity(icon, 0.f);
    c.animate(Object::TAnimDesc{.h = icon, .to = 1.f, .t = {160.f, Ease::OutCubic}, .delay = 0, .p = Prop::Opacity});
    spring_icon_scale(icon, 1.f, kReleaseSpring);
}

void set_pressed(Handle button, bool pressed) {
    array<Handle, 2> icons{};
    if (button == n.prev)
        icons = {n.prev_icon, {}};
    else if (button == n.next)
        icons = {n.next_icon, {}};
    else if (button == n.play)
        icons = {n.play_icon, n.pause_icon};
    else if (button == n.fav)
        icons = {n.heart_outline, n.heart_fill};
    else
        return;

    const Timing tone{pressed ? 90.f : 300.f, Ease::OutCubic};
    for (const Handle icon : icons) {
        if (!icon)
            continue;
        animate_tone(icon, pressed ? kPressedTone : 1.f, tone);
        spring_icon_scale(icon, pressed ? kPressedSize : 1.f, pressed ? kPressSpring : kReleaseSpring);
    }
}

void on_button(const Object::Event& e) {
    switch (e.type) {
    case Object::EventType::Press:
        set_pressed(e.target, true);
        break;
    case Object::EventType::Release:
        set_pressed(e.target, false);
        break;
    case Object::EventType::Click:
        if (e.button != Object::Button::Left)
            break;
        if (e.target == n.prev)
            Media::previous();
        else if (e.target == n.play)
            toggle_play();
        else if (e.target == n.next)
            Media::next();
        else if (e.target == n.fav)
            toggle_favorite();
        break;
    default:
        break;
    }
}

// ---- Icons ------------------------------------------------------------------

struct V2 {
    float x, y;
};

[[nodiscard]] V2    operator-(V2 a, V2 b) { return {a.x - b.x, a.y - b.y}; }
[[nodiscard]] V2    operator+(V2 a, V2 b) { return {a.x + b.x, a.y + b.y}; }
[[nodiscard]] V2    operator*(V2 a, float k) { return {a.x * k, a.y * k}; }
[[nodiscard]] float dot(V2 a, V2 b) { return a.x * b.x + a.y * b.y; }
[[nodiscard]] float cross(V2 a, V2 b) { return a.x * b.y - a.y * b.x; }
[[nodiscard]] float len(V2 a) { return sqrt(dot(a, a)); }

// Exact signed distance to a triangle (negative inside).
[[nodiscard]] float sd_triangle(V2 p, V2 a, V2 b, V2 c) {
    const V2    e0 = b - a, e1 = c - b, e2 = a - c;
    const V2    v0 = p - a, v1 = p - b, v2 = p - c;
    const V2    q0 = v0 - e0 * clamp(dot(v0, e0) / dot(e0, e0), 0.f, 1.f);
    const V2    q1 = v1 - e1 * clamp(dot(v1, e1) / dot(e1, e1), 0.f, 1.f);
    const V2    q2 = v2 - e2 * clamp(dot(v2, e2) / dot(e2, e2), 0.f, 1.f);
    const float s  = cross(e0, e2) > 0.f ? 1.f : -1.f;
    const float d  = min({dot(q0, q0), dot(q1, q1), dot(q2, q2)});
    const float w  = min({s * cross(v0, e0), s * cross(v1, e1), s * cross(v2, e2)});
    return -sqrt(d) * (w > 0.f ? 1.f : -1.f);
}

// Triangle with corner radius r whose outer silhouette still spans a, b, c.
[[nodiscard]] float sd_round_triangle(V2 p, V2 a, V2 b, V2 c, float r) {
    const float la     = len(c - b), lb = len(a - c), lc = len(b - a);
    const float per    = la + lb + lc;
    const V2    in     = (a * la + b * lb + c * lc) * (1.f / per);
    const float in_r   = abs(cross(b - a, c - a)) / per;
    const float k      = max(in_r - r, 0.f) / in_r;
    const auto  shrink = [&](V2 v) { return in + (v - in) * k; };
    return sd_triangle(p, shrink(a), shrink(b), shrink(c)) - r;
}

[[nodiscard]] float sd_round_rect(V2 p, V2 origin, V2 size, float r) {
    const V2 half = size * 0.5f;
    const V2 d    = p - (origin + half);
    const V2 q{abs(d.x) - half.x + r, abs(d.y) - half.y + r};
    return len({max(q.x, 0.f), max(q.y, 0.f)}) + min(max(q.x, q.y), 0.f) - r;
}

// Coverage mask (R8) of `sdf` over a w x h logical box, at the output scale.
[[nodiscard]] Texture rasterize(float scale, float sub_x, float sub_y, float w, float h, const function<float(V2)>& sdf) {
    scale = max(scale, 1.f);
    const int   pw    = static_cast<int>(ceil(w * scale + sub_x));
    const int   ph    = static_cast<int>(ceil(h * scale + sub_y));

    vector<uint8_t> px(static_cast<size_t>(pw) * static_cast<size_t>(ph));
    for (int y = 0; y < ph; ++y)
        for (int x = 0; x < pw; ++x) {
            const V2    p{(static_cast<float>(x) + 0.5f - sub_x) / scale, (static_cast<float>(y) + 0.5f - sub_y) / scale};
            const float d                                                                 = sdf(p) * scale; // in physical pixels
            const float coverage                                                          = clamp(0.5f - d, 0.f, 1.f);
            px[static_cast<size_t>(y) * static_cast<size_t>(pw) + static_cast<size_t>(x)] =
                static_cast<uint8_t>(lround(coverage * 255.f));
        }

    Texture tex{};
    tex.upload(pw, ph, Texture::Format::R8, px);
    tex.set_scale(scale);
    return tex;
}

constexpr float kAA = 1.f;

enum class Glyph : uint8_t { Play, Pause, Forward, Backward, Heart, HeartFilled };

// Heart in unit space: tip at the origin, lobes up to y ~= 1.1, |x| <= ~0.6 (Inigo Quilez).
[[nodiscard]] float sd_heart(V2 p) {
    p.x = abs(p.x);
    if (p.y + p.x > 1.f)
        return len(p - V2{0.25f, 0.75f}) - numbers::sqrt2_v<float> / 4.f;
    const float t = 0.5f * max(p.x + p.y, 0.f);
    const V2    a = p - V2{0.f, 1.f};
    const V2    b = p - V2{t, t};
    return sqrt(min(dot(a, a), dot(b, b))) * (p.x - p.y > 0.f ? 1.f : -1.f);
}

[[nodiscard]] Texture make_glyph(Glyph g, float scale, float sub_x, float sub_y) {
    switch (g) {
    case Glyph::Play: {
        constexpr float h = 22.f * kIconScale, w = h * 0.88f, r = 2.6f * kIconScale, nudge = 2.f * kIconScale, x0 = kAA + nudge;
        return rasterize(scale, sub_x, sub_y, w + nudge + 2 * kAA, h + 2 * kAA, [=](V2 p) {
            return sd_round_triangle(p, {x0, kAA}, {x0, kAA + h}, {x0 + w, kAA + h * 0.5f}, r);
        });
    }
    case Glyph::Pause: {
        constexpr float h = 21.f * kIconScale, bar = 6.f * kIconScale, gap = 6.f * kIconScale, r = 1.8f * kIconScale;
        return rasterize(scale, sub_x, sub_y, 2 * bar + gap + 2 * kAA, h + 2 * kAA, [=](V2 p) {
            return min(sd_round_rect(p, {kAA, kAA}, {bar, h}, r),
                sd_round_rect(p, {kAA + bar + gap, kAA}, {bar, h}, r));
        });
    }
    case Glyph::Forward:
    case Glyph::Backward: {
        constexpr float h     = 16.f * kIconScale, w = 15.f * kIconScale, step = w - 2.5f * kIconScale, r = 2.f * kIconScale;
        constexpr float total = step + w;
        const bool      flip  = g == Glyph::Backward;
        return rasterize(scale, sub_x, sub_y, total + 2 * kAA, h + 2 * kAA, [=](V2 p) {
            if (flip)
                p.x = total + 2 * kAA - p.x;
            float d = 1e9f;
            for (const float x0 : {kAA, kAA + step})
                d = min(d, sd_round_triangle(p, {x0, kAA}, {x0, kAA + h}, {x0 + w, kAA + h * 0.5f}, r));
            return d;
        });
    }
    case Glyph::Heart:
    case Glyph::HeartFilled: {
        // 20 x ~18.3 heart centered in the 24 box, tip slightly rounded; the
        // outline is a 2px stroke inside the filled silhouette so both match.
        constexpr float width = 20.f * kIconScale, r = 1.f * kIconScale, stroke = 2.f * kIconScale;
        constexpr float k      = (width - 2 * r) / 1.2071f;
        constexpr float height = 1.1036f * k + 2 * r;
        constexpr float bottom = (kHeartSize + height) * 0.5f - r;
        const bool      filled = g == Glyph::HeartFilled;
        return rasterize(scale, sub_x, sub_y, kHeartSize, kHeartSize, [=](V2 p) {
            const float d = sd_heart({(p.x - kHeartSize * 0.5f) / k, (bottom - p.y) / k}) * k - r;
            return filled ? d : abs(d + stroke * 0.5f) - stroke * 0.5f;
        });
    }
    }
    return {};
}

// ---- Construction -----------------------------------------------------------

Object::LayoutData row(float spacing, Object::Alignment align = Object::Alignment::Center,
    Object::Justify          justify                          = Object::Justify::Start) {
    Object::LayoutData l{};
    l.dir     = Object::Direction::Row;
    l.spacing = spacing;
    l.align   = align;
    l.justify = justify;
    return l;
}

Handle make_button(Handle parent, float slot_center) {
    auto&        c = ctx();
    const Handle b = c.create_group(parent, Frame{
        .x = kContentW * slot_center - kButtonW * 0.5f, .y = 0.f, .width = kButtonW, .height = kControlsH,
    });
    c.set_layout(b, row(0.f, Object::Alignment::Center, Object::Justify::Center));
    c.on(b, Object::EventType::Press | Object::EventType::Release | Object::EventType::Click, on_button);
    return b;
}

Handle make_icon(Handle parent, Glyph glyph) {
    auto&        c = ctx();
    const Handle t = c.create_text(parent, {});
    // Redrawn by the scene when the output scale changes, so edges stay crisp.
    c.text(t).set_color(Color::White).set_raster(
        [](float scale, float sub_x, float sub_y, uintptr_t g) { return make_glyph(static_cast<Glyph>(g), scale, sub_x, sub_y); }, static_cast<uintptr_t>(glyph));
    // Fixed at the glyph's natural size so press / pop animations can scale it.
    const float w = c.text(t).texture_width(), h = c.text(t).texture_height();
    c.set_size_mode(t, Object::Axis::Horizontal, Object::SizeMode::Fixed);
    c.set_size_mode(t, Object::Axis::Vertical, Object::SizeMode::Fixed);
    c.set_size(t, w, h);
    if (icon_count < icon_base.size())
        icon_base[icon_count++] = {t, w, h};
    return t;
}

void build(const MusicPlayerView::Fonts& f) {
    auto& c = ctx();
    icon_count  = 0;
    shown_pause = false;

    {
        Object::LayoutData l{};
        l.dir   = Object::Direction::Column;
        l.align = Object::Alignment::Center;
        c.set_layout(bg, l);
    }
    c.set_clip(bg, true);
    content = c.create_group(bg, Frame{.x = 0, .y = 0, .width = kExpandedW, .height = kExpandedH});

    Object::LayoutData column{};
    column.dir     = Object::Direction::Column;
    column.padding = Object::Insets::xy(kPadX, kPadY);
    column.spacing = 8.f;
    c.set_layout(content, column);

    // Header: art | title + artist | visualizer
    const Handle header = c.create_group(content, Frame{.x = 0, .y = 0, .width = 0, .height = kArtSize});
    c.set_size_mode(header, Object::Axis::Horizontal, Object::SizeMode::Grow);
    c.set_layout(header, row(16.f));

    const Handle art_box = c.create_rect(header, Object::RectDesc{
        .frame = {0, 0, kArtSize, kArtSize}, .color = kArtPlaceholder, .radius = 10.f,
    });
    n.art = c.create_image(art_box, Object::ImageDesc{.frame = {0, 0, kArtSize, kArtSize}, .texture = {}, .radius = 10.f});
    c.set_size_mode(n.art, Object::Axis::Horizontal, Object::SizeMode::Fixed);
    c.set_size_mode(n.art, Object::Axis::Vertical, Object::SizeMode::Fixed);
    c.set_visible(n.art, false);

    const Handle info = c.create_group(header, Frame{.x = 0, .y = 0, .width = 0, .height = kArtSize});
    c.set_size_mode(info, Object::Axis::Horizontal, Object::SizeMode::Grow);
    c.set_clip(info, true); // long titles are cut at the visualizer instead of overflowing
    {
        Object::LayoutData l{};
        l.dir     = Object::Direction::Column;
        l.spacing = 4.f;
        l.justify = Object::Justify::Center;
        c.set_layout(info, l);
    }
    n.title  = c.create_text(info, {});
    n.artist = c.create_text(info, {});
    c.text(n.title).set_font(f.title).set_color(Color::White);
    c.text(n.artist).set_font(f.artist).set_color(kSecondary);

    const Handle viz = c.create_group(header, Frame{.x = 0, .y = 0, .width = 32.f, .height = kBarMaxH});
    c.set_layout(viz, row(3.f, Object::Alignment::Center, Object::Justify::Center));
    for (int i = 0; i < kBarCount; ++i) {
        const float h                  = kBarMinH + (kBarMaxH - kBarMinH) * paused_level(i);
        n.bars[static_cast<size_t>(i)] = c.create_rect(viz, Object::RectDesc{
            .frame = {0, 0, kBarW, h}, .color = kBarPaused, .radius = 2.f,
        });
    }

    const Handle progress = c.create_group(content, Frame{.x = 0, .y = 0, .width = 0, .height = 16.f});
    c.set_size_mode(progress, Object::Axis::Horizontal, Object::SizeMode::Grow);
    c.set_layout(progress, row(12.f));
    n.time_l_box = c.create_group(progress, Frame{.x = 0, .y = 0, .width = 0, .height = 16.f});
    n.track      = c.create_rect(progress, Object::RectDesc{.frame = {0, 0, 0, 6.f}, .color = kTrack, .radius = 3.f});
    n.time_r_box = c.create_group(progress, Frame{.x = 0, .y = 0, .width = 0, .height = 16.f});
    c.set_layout(n.time_l_box, row(0.f, Object::Alignment::Center, Object::Justify::Start));
    c.set_layout(n.time_r_box, row(0.f, Object::Alignment::Center, Object::Justify::End));
    n.time_l  = c.create_text(n.time_l_box, {});
    n.time_r  = c.create_text(n.time_r_box, {});
    time_font = f.time;
    c.set_size_mode(n.track, Object::Axis::Horizontal, Object::SizeMode::Grow);
    c.text(n.time_l).set_font(f.time).set_color(kSecondary);
    c.text(n.time_r).set_font(f.time).set_color(kSecondary);
    c.set_layout(n.track, row(0.f));
    n.fill      = c.create_rect(n.track, Object::RectDesc{.frame = {0, 0, 0, 6.f}, .color = Color::White, .radius = 3.f});
    n.fill_rest = c.create_group(n.track, Frame{.x = 0, .y = 0, .width = 0, .height = 6.f});

    // Controls (absolute slots, no layout)
    const Handle controls = c.create_group(content, Frame{.x = 0, .y = 0, .width = 0, .height = kControlsH});
    c.set_size_mode(controls, Object::Axis::Horizontal, Object::SizeMode::Grow);


    n.prev       = make_button(controls, kSlotCenter[0]);
    n.play       = make_button(controls, kSlotCenter[1]);
    n.next       = make_button(controls, kSlotCenter[2]);
    n.prev_icon  = make_icon(n.prev, Glyph::Backward);
    n.play_icon  = make_icon(n.play, Glyph::Play);
    n.pause_icon = make_icon(n.play, Glyph::Pause);
    n.next_icon  = make_icon(n.next, Glyph::Forward);
    // Initial glyph without the pop animation (that is for changes while open).
    {
        const auto* s = Media::state();
        shown_pause   = s && s->has_player && s->playback == lyricsmpris::Playback::Playing;
        c.set_visible(n.play_icon, !shown_pause);
        c.set_visible(n.pause_icon, shown_pause);
    }

    // Favorite: two stacked hearts in a fixed box so they can scale about the center.
    n.fav       = make_button(controls, kFavoriteSlot);
    n.heart_box = c.create_group(n.fav, Frame{.x = 0, .y = 0, .width = kHeartSize, .height = kHeartSize});
    for (Handle* h : {&n.heart_outline, &n.heart_fill}) {
        *h = make_icon(n.heart_box, h == &n.heart_outline ? Glyph::Heart : Glyph::HeartFilled);
        c.set_size_mode(*h, Object::Axis::Horizontal, Object::SizeMode::Fixed);
        c.set_size_mode(*h, Object::Axis::Vertical, Object::SizeMode::Fixed);
        c.set_size(*h, kHeartSize, kHeartSize);
    }
    shown_fav_uri.clear();
    shown_liked = false;
    settle_heart(false);
}

}

void MusicPlayerView::start(Handle arg_bg, const Fonts& fonts) {
    if (!arg_bg) {
        Log::logger(Log::Error, "MusicPlayerView: invalid background");
        return;
    }

    stop();
    bg = arg_bg;
    SpotifyBridge::reload(); // one stat; picks up a setup run after launch
    build(fonts);

    running = true;
    playing = false;
    phase   = 0.f;
    shown_title.clear();
    shown_artist.clear();
    shown_time_l.clear();
    shown_time_r.clear();
    shown_time_shape.clear();

    ctx().layout();
    refresh();
    refresh_artwork();

    auto& c = ctx();
    c.set_opacity(content, 0.f);
    c.animate(Object::TAnimDesc{.h = content, .to = 1.f, .t = kRevealFade, .delay = kRevealDelay, .p = Prop::Opacity});
    Media::set_listener(on_media);
    SpotifyBridge::set_listener(on_bridge);
    Media::resync_position(); // the local estimate may have drifted while hidden
}

void MusicPlayerView::stop() {
    if (!running)
        return;
    running = false;
    playing = false;
    Media::set_listener(nullptr);
    SpotifyBridge::set_listener(nullptr);
    clear_prediction();
    API::cancel_event(ticker);
    ticker = {};

    auto&          c = ctx();
    vector<Handle> kids;
    for (const Handle h : c.children(bg))
        kids.push_back(h);
    for (const Handle h : kids)
        c.destroy(h);
    c.clear_layout(bg);
    c.set_clip(bg, false);

    n       = {};
    content = {};
    bg      = {};
}