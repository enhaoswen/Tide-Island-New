#include "app/views/sysinfo.hpp"
#include "app/API.hpp"
#include "app/island.hpp"
#include "app/state_manager.hpp"
#include "backend/sysinfo/sysinfo.hpp"
#include "graphics/image.hpp"
#include "graphics/renderer.hpp"
#include "scene/animation.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include <optional>
#include <string>
#include <utility>

using namespace std;
using namespace std::chrono;
using Object::Handle;

namespace {

enum class Kind : uint8_t { Volume, Brightness, Power };

enum class Level : uint8_t { L25, L50, L75, L100, Muted };

constexpr uint32_t k_progress_w = 248;
constexpr uint32_t k_text_w     = 220;
constexpr float    k_height     = 38.f;
constexpr float    k_icon       = 36.f;

// The speaker art sits low-left in its 64px cell, leaving the top-right for the +/- sign.
constexpr float k_art_left     = 7.f / 64.f;
constexpr float k_art_right    = 61.f / 64.f; // right edge of the sign
constexpr float k_art_center_y = 35.5f / 64.f;
constexpr float    k_ring       = 30.f;

constexpr int k_frames    = 30;
constexpr int k_step_end  = 19; // transitions are settled from here on
constexpr int k_mute_end  = 22;
constexpr float k_sign_shift = 5.f; // pulls the sign toward the speaker

// The sign strip is a seamless loop with no in/out frames, so those are opacity fades.

constexpr auto k_frame_time = 16667us;
constexpr auto k_hide_after = 1250ms;

const Color k_track{1.f, 1.f, 1.f, .16f};
const Color k_label{"#8e8e93"};

struct Clip {
    Handle node{};
    int    frame{};
    int    end{};
    int    step{1};
    bool   active{};
};

SysInfoView::Fonts fonts{};
Kind               kind{};
bool               running{};
StateType          previous{StateType::Clock};

Handle bg{},       content{}, value{}, ring{};
Clip   speaker{},  sign{};
Level  shown{},    target{};
int    sign_dir{}, pending_sign{};
bool   sign_again{}; // another change arrived during this cycle: loop once more

optional<Level>    from_level{};
Timer::EventHandle hide_timer{}, frame_timer{};

optional<SysInfo::Volume> last_volume{};
optional<float>           last_brightness{};
optional<SysInfo::Power>  last_power{};

Object::Context& ctx() { return StateManager::context(); }

int percent(float v) { return static_cast<int>(lround(v * 100.f)); }

Level level_of(const SysInfo::Volume& v) {
    if (v.muted)
        return Level::Muted;
    const int quarter = clamp(static_cast<int>(ceil(v.level * 4.f - 1e-3f)), 1, 4);
    return static_cast<Level>(quarter - 1);
}

int percent_of(Level l) { return (to_underlying(l) + 1) * 25; }

string power_text(const SysInfo::Power& p) {
    string s = !p.plugged
        ? "On Battery"
        : p.status == SysInfo::Charge::Charging
        ? "Charging"
        : "Plugged In";
    if (p.battery)
        s += format(" · {}%", *p.battery);
    return s;
}

// ---- Sprites ----------------------------------------------------------------

bool load(Handle node, string_view sprite, int frames) {
    const float scale = Renderer::text_scale();
    const int   side  = max(1, static_cast<int>(lround(k_icon * scale)));
    const auto  path  = format("{}/volume-icons/{}", TIDE_RESOURCE_DIR, sprite);
    auto        px    = Image::load_strip(path.c_str(), frames, side);
    if (!px)
        return false;
    Texture t{};
    t.upload(px->width, px->height, Texture::Format::RGBA8, px->rgba);
    t.set_scale(scale);
    ctx().image(node).set_texture(std::move(t));
    return true;
}

void show_frame(const Clip& c) {
    constexpr float w = 1.f / k_frames;
    ctx().image(c.node).set_uv({.u0 = static_cast<float>(c.frame) * w, .v0     = 0.f,
                                .u1 = static_cast<float>(c.frame + 1) * w, .v1 = 1.f});
}

void on_frame();

void schedule_frame() {
    if (!Timer::is_pending(frame_timer))
        frame_timer = API::push_event(k_frame_time, on_frame, 0.f);
}

void play(Clip& c, string_view sprite, int from, int to) {
    c.active = load(c.node, sprite, k_frames);
    if (!c.active)
        return;
    c.frame = from;
    c.end   = to;
    c.step  = to >= from ? 1 : -1;
    show_frame(c);
    schedule_frame();
}

void show_rest(Level l) {
    if (l != Level::Muted) {
        if (load(speaker.node, format("static/volume-{}.png", percent_of(l)), 1))
            ctx().image(speaker.node).set_uv({});
        return;
    }
    if (load(speaker.node, "mute/volume-mute.png", k_frames)) {
        speaker.frame = k_frames - 1;
        show_frame(speaker);
    }
}

// One clip per call; adjacent levels only, so 25 -> 100 plays three steps.
void step_speaker() {
    if (shown == target) {
        speaker.active = false;
        show_rest(shown);
        return;
    }
    if (target == Level::Muted) {
        shown = Level::Muted;
        play(speaker, "mute/volume-mute.png", 0, k_mute_end);
        return;
    }
    if (shown == Level::Muted) {
        // The mute strip is drawn at another size, so unmuting ends on a cut to the static icon.
        shown = target;
        play(speaker, "mute/volume-mute.png", k_mute_end, 0);
        return;
    }
    const bool  up   = to_underlying(target) > to_underlying(shown);
    const Level next = static_cast<Level>(to_underlying(shown) + (up ? 1 : -1));
    play(speaker, format("transition/volume-{}-{}-{}.png", up ? "up" : "down", percent_of(shown), percent_of(next)),
        0, k_step_end);
    shown = next;
}

void fade_sign(float to) {
    const Handle h = sign.node;
    ctx().animate(Object::TAnimDesc{
        .h       = h,
        .to      = to,
        .t       = Timings::Quick,
        .delay   = 0.f,
        .p       = Prop::Opacity,
        .on_done = [h, to] {
            if (to == 0.f && !sign.active && ctx().alive(h))
                ctx().set_visible(h, false);
        },
    });
}

void play_sign(int dir) {
    if (sign.active && sign_dir == dir) {
        sign_again = true;
        return;
    }
    const bool shown = sign.active || ctx().is_animating(sign.node, Prop::Opacity);
    sign_dir         = dir;
    sign_again       = false;
    play(sign, dir > 0 ? "sign/sign-plus.png" : "sign/sign-minus.png", 0, k_frames - 1);
    ctx().set_visible(sign.node, true);
    if (!shown)
        ctx().set_opacity(sign.node, 0.f);
    fade_sign(1.f);
}

void on_frame() {
    frame_timer = {};
    if (!running)
        return;
    if (speaker.active) {
        if (speaker.frame == speaker.end)
            step_speaker();
        else {
            speaker.frame += speaker.step;
            show_frame(speaker);
        }
    }
    if (sign.active) {
        if (++sign.frame < k_frames)
            show_frame(sign);
        else if (sign_again) {
            sign.frame = 0;
            sign_again = false;
            show_frame(sign);
        }
        else {
            sign.active = false;
            sign.frame  = k_frames - 1;
            fade_sign(0.f);
        }
    }
    if (speaker.active || sign.active)
        schedule_frame();
    Island::request_redraw(true);
}

// ---- Content ----------------------------------------------------------------

Handle make_text(Handle parent, FontHandle font, const Color& color) {
    const Handle t = ctx().create_text(parent, {});
    ctx().text(t).set_font(font).set_color(color);
    return t;
}

Handle make_icon(Handle box, float dx = 0.f) {
    auto&        c = ctx();
    const Handle h = c.create_image(box, Object::ImageDesc{
        .frame = {-k_art_left * k_icon + dx, (.5f - k_art_center_y) * k_icon, k_icon, k_icon},
    });
    c.set_size_mode(h, Object::Axis::Horizontal, Object::SizeMode::Fixed);
    c.set_size_mode(h, Object::Axis::Vertical, Object::SizeMode::Fixed);
    return h;
}

void build() {
    auto& c = ctx();

    Object::LayoutData centered{};
    centered.align   = Object::Alignment::Center;
    centered.justify = Object::Justify::Center;
    c.set_layout(bg, centered);
    c.set_clip(bg, true);

    content = c.create_group(bg, Frame{0.f, 0.f, static_cast<float>(SysInfoView::width()), k_height});

    if (kind == Kind::Power) {
        c.set_layout(content, centered);
        value = make_text(content, fonts.label, Color::White);
        return;
    }

    Object::LayoutData bar{};
    bar.align   = Object::Alignment::Center;
    bar.justify = Object::Justify::SpaceBetween;
    bar.padding = {.left = 18.f, .top = 0.f, .right = 16.f, .bottom = 0.f};
    c.set_layout(content, bar);

    const Handle left = c.create_group(content, Frame{0.f, 0.f, 0.f, 0.f});
    c.set_size_mode(left, Object::Axis::Horizontal, Object::SizeMode::Fit);
    c.set_size_mode(left, Object::Axis::Vertical, Object::SizeMode::Fit);
    Object::LayoutData row{};
    row.align   = Object::Alignment::Center;
    row.spacing = kind == Kind::Volume ? 4.f : 12.f;
    c.set_layout(left, row);

    if (kind == Kind::Volume) {
        const Handle box = c.create_group(left, Frame{0.f, 0.f, (k_art_right - k_art_left) * k_icon - k_sign_shift, k_icon});
        speaker          = Clip{.node = make_icon(box)};
        sign             = Clip{.node = make_icon(box, -k_sign_shift)};
        c.set_visible(sign.node, false);
    }
    else {
        c.text(make_text(left, fonts.label, k_label)).set_text("Brightness");
    }
    value = make_text(left, fonts.value, Color::White);

    ring = c.create_ring(content, Object::RingDesc{
        .frame     = {0.f, 0.f, k_ring, k_ring},
        .color     = Color::White,
        .track     = k_track,
        .progress  = 0.f,
        .thickness = 3.5f,
    });
}

void refresh(bool animate) {
    auto& c = ctx();
    float progress{};
    switch (kind) {
    case Kind::Volume: {
        const auto v = SysInfo::volume();
        if (!v)
            return;
        c.text(value).set_text(format("{}%", percent(v->level)));
        progress = v->level;
        target   = level_of(*v);
        if (!speaker.active)
            step_speaker();
        if (pending_sign)
            play_sign(exchange(pending_sign, 0));
        break;
    }
    case Kind::Brightness: {
        const auto b = SysInfo::brightness();
        if (!b)
            return;
        c.text(value).set_text(format("{}%", percent(*b)));
        progress = *b;
        break;
    }
    case Kind::Power: {
        if (const auto p = SysInfo::power())
            c.text(value).set_text(power_text(*p));
        return;
    }
    }

    progress = clamp(progress, 0.f, 1.f);
    if (animate)
        c.animate(Object::TAnimDesc{.h = ring, .to = progress, .t = Timings::Quick, .delay = 0.f, .p = Prop::Progress});
    else
        c.ring(ring).set_progress(progress);
}

void on_hide() {
    hide_timer = {};
    if (running && Island::get_state() == StateType::Osd)
        StateManager::switch_state(previous);
}

void restart_hide() {
    API::cancel_event(hide_timer);
    hide_timer = API::push_event(duration_cast<microseconds>(k_hide_after), on_hide, 0.f);
}

void show(Kind k) {
    const StateType s = Island::get_state();
    if (s == StateType::MusicPLayer || s == StateType::ControlCenter) {
        pending_sign = 0;
        from_level.reset();
        return;
    }
    if (running && kind == k) {
        from_level.reset();
        refresh(true);
        restart_hide();
        Island::request_redraw(true);
        return;
    }
    if (s != StateType::Osd)
        previous = s;
    kind = k;
    StateManager::switch_state(StateType::Osd);
}

void on_sysinfo(SysInfo::Update u) {
    if (SysInfo::contains(u, SysInfo::Update::Volume)) {
        const auto v    = SysInfo::volume();
        const auto prev = exchange(last_volume, v);
        // nullopt -> value is (re)connecting, not the user changing anything.
        if (prev && v && *prev != *v) {
            const int now = percent(v->level), was = percent(prev->level);
            pending_sign  = v->muted != prev->muted ? 0 : (now > was) - (now < was);
            from_level    = level_of(*prev);
            show(Kind::Volume);
        }
    }
    if (SysInfo::contains(u, SysInfo::Update::Brightness)) {
        const auto b    = SysInfo::brightness();
        const auto prev = exchange(last_brightness, b);
        if (prev && b && *prev != *b)
            show(Kind::Brightness);
    }
    if (SysInfo::contains(u, SysInfo::Update::Power)) {
        const auto p    = SysInfo::power();
        const auto prev = exchange(last_power, p);
        if (prev && p && prev->plugged != p->plugged)
            show(Kind::Power);
        else if (running && kind == Kind::Power) {
            refresh(true);
            Island::request_redraw(true);
        }
    }
}

}

namespace SysInfoView {

void init(const Fonts& f) {
    fonts           = f;
    last_volume     = SysInfo::volume();
    last_brightness = SysInfo::brightness();
    last_power      = SysInfo::power();
    SysInfo::set_listener(on_sysinfo);
}

void shutdown() {
    SysInfo::set_listener(nullptr);
    stop();
}

void start(Handle arg_bg) {
    stop();
    if (!arg_bg)
        return;
    bg      = arg_bg;
    running = true;
    build();

    if (kind == Kind::Volume) {
        const auto v = SysInfo::volume();
        shown        = from_level.value_or(v ? level_of(*v) : Level::L100);
        from_level.reset();
        show_rest(shown);
    }
    refresh(false);

    auto& c = ctx();
    c.set_opacity(content, 0.f);
    c.animate(Object::TAnimDesc{.h = content, .to = 1.f, .t = Timings::Smooth, .delay = 0.f, .p = Prop::Opacity});
    restart_hide();
}

void stop() {
    running = false;
    API::cancel_event(hide_timer);
    API::cancel_event(frame_timer);
    hide_timer = frame_timer = {};
    if (content)
        ctx().destroy(content);
    content  = value = ring = {};
    speaker  = sign  = {};
    sign_dir   = 0;
    sign_again = false;
    if (bg)
        ctx().set_clip(bg, false);
    bg = {};
}

uint32_t width() { return kind == Kind::Power ? k_text_w : k_progress_w; }

}