#include "app/views/music_player.hpp"

#include "app/API.hpp"
#include "app/island.hpp"
#include "app/state_manager.hpp"
#include "backend/media.hpp"
#include "backend/timer.hpp"
#include "graphics/renderer.hpp"
#include "utils/log.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <format>
#include <numbers>
#include <string>

using namespace std;
using namespace std::chrono;
using Object::Handle;

namespace {

const Color kArtPlaceholder{"#2c2c2e"};
const Color kSecondary{"#8e8e93"};
const Color kTrack{"#333333"};
const Color kPressed{"#888888"};
const Color kBarPlaying{"#b56cff"};
const Color kBarPaused{"#5f4b72"};

constexpr float kPadX       = 20.f;
constexpr float kPadY       = 18.f;
constexpr float kContentW   = 410.f - 2 * kPadX;
constexpr float kArtSize    = 60.f;
constexpr float kBarW       = 3.f;
constexpr float kBarMinH    = 6.f;
constexpr float kBarMaxH    = 22.f;
constexpr float kButtonW    = 56.f;
constexpr float kControlsH  = 36.f;
constexpr float kDimOpacity = 0.45f;

constexpr int kBarCount = 5;

constexpr array<float, 3> kSlotCenter{0.2425f, 0.4675f, 0.6925f};

constexpr auto kTick         = milliseconds{64};
constexpr auto kProgressTick = milliseconds{500};

const Timing kBarPlayingAnim{120.f, Ease::InOutQuad};
const Timing kBarPausedAnim{260.f, Ease::InOutQuad};
const Timing kProgressAnim{500.f, Ease::OutCubic};

struct Nodes {
    Handle                   art{};
    Handle                   title{}, artist{};
    array<Handle, kBarCount> bars{};
    Handle                   time_l{},    time_r{};
    Handle                   track{},     fill{};
    Handle                   prev{},      play{},      next{};
    Handle                   prev_icon{}, play_icon{}, next_icon{};
    Handle                   pause_icon{};
    array<Handle, 2>         pause_bars{};
};

Nodes                    n{};
Handle                   bg{};
bool                     running{};
bool                     playing{};
float                    phase{};
Timer::EventHandle       ticker{};
steady_clock::time_point last_progress{};

string shown_title, shown_artist, shown_time_l, shown_time_r;
string shown_track_id;

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

void animate_bars(bool use_playing) {
    auto& c = ctx();
    for (int i = 0; i < kBarCount; ++i) {
        const float level = use_playing ? playing_level(i) : paused_level(i);
        const float h     = kBarMinH + (kBarMaxH - kBarMinH) * level;
        (void)c.animate(Object::TAnimDesc{
            .h = n.bars[static_cast<size_t>(i)], .to                    = h,
            .t = use_playing ? kBarPlayingAnim : kBarPausedAnim, .delay = 0.f, .p = Prop::H,
        });
        c.rect(n.bars[static_cast<size_t>(i)]).set_color(use_playing ? kBarPlaying : kBarPaused);
    }
}

// Elapsed / total labels and the progress fill. `animate` = ease the fill.
void update_progress(bool animate) {
    const auto*  s = Media::state();
    milliseconds pos{}, len{};
    if (s && s->has_player) {
        pos = Media::position();
        len = s->duration.value_or(milliseconds{});
    }

    set_text_cached(n.time_l, shown_time_l, format_time(pos));
    set_text_cached(n.time_r, shown_time_r, format_time(len));

    auto&       c        = ctx();
    const float track_w  = c.frame(n.track).width;
    const float fraction = len.count() > 0
        ? clamp(static_cast<float>(pos.count()) / static_cast<float>(len.count()), 0.f, 1.f)
        : 0.f;
    const float w = track_w * fraction;

    if (animate)
        (void)c.animate(Object::TAnimDesc{.h = n.fill, .to = w, .t = kProgressAnim, .delay = 0.f, .p = Prop::W});
    else {
        c.cancel_animation(n.fill, Prop::W);
        c.set_size(n.fill, w, c.frame(n.fill).height);
    }
    last_progress = steady_clock::now();
}

void schedule_tick();

void on_tick() {
    ticker = {};
    if (!running || !playing)
        return;

    phase += 0.18f;
    if (phase > 2.f * numbers::pi_v<float>)
        phase -= 2.f * numbers::pi_v<float>;
    animate_bars(true);

    if (steady_clock::now() - last_progress >= kProgressTick)
        update_progress(true);

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

// Pull everything from Media into the scene.
void refresh(bool track_changed_hint) {
    auto&       c           = ctx();
    const auto* s           = Media::state();
    const bool  has         = s && s->has_player;
    const bool  now_playing = has && s->playback == lyricsmpris::Playback::Playing;

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

    c.set_visible(n.play_icon, !now_playing);
    c.set_visible(n.pause_icon, now_playing);

    const string track_id  = has ? s->track_id + '\n' + s->title : string{};
    const bool   new_track = track_changed_hint || track_id != shown_track_id;
    shown_track_id         = track_id;
    update_progress(!new_track);

    if (now_playing != playing) {
        playing = now_playing;
        if (!playing)
            animate_bars(false);
    }
    schedule_tick();
}

void refresh_artwork() {
    Texture    tex   = Renderer::decode_image(Media::artwork());
    const bool valid = tex.valid();
    ctx().image(n.art).set_texture(std::move(tex));
    ctx().set_visible(n.art, valid);
}

void on_media(Media::Update u) {
    if (!running)
        return;
    if (Media::contains(u, Media::Update::State))
        refresh(false);
    if (Media::contains(u, Media::Update::Artwork))
        refresh_artwork();
    Island::request_redraw(true);
}

// ---- Buttons ----------------------------------------------------------------

void tint_button(Handle button, const Color& color) {
    auto& c = ctx();
    if (button == n.prev)
        c.text(n.prev_icon).set_color(color);
    else if (button == n.next)
        c.text(n.next_icon).set_color(color);
    else if (button == n.play) {
        c.text(n.play_icon).set_color(color);
        for (const Handle b : n.pause_bars)
            c.rect(b).set_color(color);
    }
}

void on_button(const Object::Event& e) {
    switch (e.type) {
    case Object::EventType::Press:
        tint_button(e.target, kPressed);
        break;
    case Object::EventType::Release:
        tint_button(e.target, Color::White);
        break;
    case Object::EventType::Click:
        if (e.button != Object::Button::Left)
            break;
        if (e.target == n.prev)
            Media::previous();
        else if (e.target == n.play)
            Media::play_pause();
        else if (e.target == n.next)
            Media::next();
        break;
    default:
        break;
    }
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

Handle make_icon(Handle parent, FontHandle font, string_view glyph) {
    auto&        c = ctx();
    const Handle t = c.create_text(parent, {});
    c.text(t).set_font(font).set_color(Color::White).set_text(glyph);
    return t;
}

void build(const MusicPlayerView::Fonts& f) {
    auto& c = ctx();

    Object::LayoutData column{};
    column.dir     = Object::Direction::Column;
    column.padding = Object::Insets::xy(kPadX, kPadY);
    column.spacing = 8.f;
    c.set_layout(bg, column);

    // Header: art | title + artist | visualizer
    const Handle header = c.create_group(bg, Frame{.x = 0, .y = 0, .width = 0, .height = kArtSize});
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

    // Progress: elapsed | track | total
    const Handle progress = c.create_group(bg, Frame{.x = 0, .y = 0, .width = 0, .height = 16.f});
    c.set_size_mode(progress, Object::Axis::Horizontal, Object::SizeMode::Grow);
    c.set_layout(progress, row(12.f));
    n.time_l = c.create_text(progress, {});
    n.track  = c.create_rect(progress, Object::RectDesc{.frame = {0, 0, 0, 6.f}, .color = kTrack, .radius = 3.f});
    n.time_r = c.create_text(progress, {});
    c.set_size_mode(n.track, Object::Axis::Horizontal, Object::SizeMode::Grow);
    c.text(n.time_l).set_font(f.time).set_color(kSecondary);
    c.text(n.time_r).set_font(f.time).set_color(kSecondary);
    n.fill = c.create_rect(n.track, Object::RectDesc{.frame = {0, 0, 0, 6.f}, .color = Color::White, .radius = 3.f});

    // Controls (absolute slots, no layout)
    const Handle controls = c.create_group(bg, Frame{.x = 0, .y = 0, .width = 0, .height = kControlsH});
    c.set_size_mode(controls, Object::Axis::Horizontal, Object::SizeMode::Grow);

    n.prev      = make_button(controls, kSlotCenter[0]);
    n.play      = make_button(controls, kSlotCenter[1]);
    n.next      = make_button(controls, kSlotCenter[2]);
    n.prev_icon = make_icon(n.prev, f.icon, "◀◀");
    n.play_icon = make_icon(n.play, f.icon, "▶");
    n.next_icon = make_icon(n.next, f.icon, "▶▶");

    n.pause_icon = c.create_group(n.play, Frame{.x = 0, .y = 0, .width = 18.f, .height = 24.f});
    c.set_layout(n.pause_icon, row(6.f));
    for (auto& b : n.pause_bars)
        b = c.create_rect(n.pause_icon, Object::RectDesc{.frame = {0, 0, 6.f, 24.f}, .color = Color::White, .radius = 1.f});
    c.set_visible(n.pause_icon, false);

    // Favorite slot (kContentW * 0.9175) left empty on purpose.
}

}

void MusicPlayerView::start(Handle arg_bg, const Fonts& fonts) {
    if (!arg_bg) {
        Log::logger(Log::Error, "MusicPlayerView: invalid background");
        return;
    }

    stop();
    bg = arg_bg;
    build(fonts);

    running = true;
    playing = false;
    phase   = 0.f;
    shown_title.clear();
    shown_artist.clear();
    shown_time_l.clear();
    shown_time_r.clear();
    shown_track_id.clear();

    ctx().layout();
    refresh(true);
    refresh_artwork();
    Media::set_listener(on_media);
}

void MusicPlayerView::stop() {
    if (!running)
        return;
    running = false;
    playing = false;
    Media::set_listener(nullptr);
    API::cancel_event(ticker);
    ticker = {};

    auto&          c = ctx();
    vector<Handle> kids;
    for (const Handle h : c.children(bg))
        kids.push_back(h);
    for (const Handle h : kids)
        c.destroy(h);
    c.clear_layout(bg);

    n  = {};
    bg = {};
}