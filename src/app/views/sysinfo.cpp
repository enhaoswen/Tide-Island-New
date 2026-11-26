#include "app/views/sysinfo.hpp"
#include "backend/sysinfo/sysinfo.hpp"
#include "app/API.hpp"
#include "app/island.hpp"
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
constexpr float k_ring         = 30.f;

constexpr int   k_frames     = 30;
constexpr int   k_step_end   = 19; // transitions are settled from here on
constexpr int   k_mute_end   = 22;
constexpr float k_sign_shift = 5.f; // pulls the sign toward the speaker

// The sign strip is a seamless loop with no in/out frames, so those are opacity fades.

constexpr auto k_frame_time = 16667us;
constexpr auto k_hide_after = 1250ms;

const Color k_track{1.f, 1.f, 1.f, .16f};
const Color k_label{"#8e8e93"};

struct Clip {
    Object::Handle node{};
    int            frame{};
    int            end{};
    int            step{1};
    bool           active{};
};

// One opening of the OSD; stop() resets it as a whole. Timer callbacks carry no context,
// so this has to live at file scope.
struct Osd {
    Object::Handle     content{}, value{}, ring{};
    Clip               speaker{}, sign{};
    Level              shown{};
    int                sign_dir{};
    bool               sign_again{}; // another change arrived during this cycle: loop once more
    Timer::EventHandle hide_timer{}, frame_timer{};
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

Object::Context& ctx() { return *context; }

bool is_open() { return static_cast<bool>(osd.content); }

int percent(float v) { return static_cast<int>(lround(v * 100.f)); }

Level level_of(const SysInfo::Volume& v) {
    if (v.muted)
        return Level::Muted;
    const int quarter = clamp(static_cast<int>(ceil(v.level * 4.f - 1e-3f)), 1, 4);
    return static_cast<Level>(quarter - 1);
}

Level target() {
    const auto v = SysInfo::volume();
    return v ? level_of(*v) : Level::L100;
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

bool load(Object::Handle node, string_view sprite, int frames) {
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
    if (!Timer::is_pending(osd.frame_timer))
        osd.frame_timer = API::push_event(k_frame_time, on_frame, 0.f);
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
        if (load(osd.speaker.node, format("static/volume-{}.png", percent_of(l)), 1))
            ctx().image(osd.speaker.node).set_uv({});
        return;
    }
    if (load(osd.speaker.node, "mute/volume-mute.png", k_frames)) {
        osd.speaker.frame = k_frames - 1;
        show_frame(osd.speaker);
    }
}

// One clip per call; adjacent levels only, so 25 -> 100 plays three steps.
void step_speaker() {
    const Level want = target();
    if (osd.shown == want) {
        osd.speaker.active = false;
        show_rest(osd.shown);
        return;
    }
    if (want == Level::Muted) {
        osd.shown = Level::Muted;
        play(osd.speaker, "mute/volume-mute.png", 0, k_mute_end);
        return;
    }
    if (osd.shown == Level::Muted) {
        // The mute strip is drawn at another size, so unmuting ends on a cut to the static icon.
        osd.shown = want;
        play(osd.speaker, "mute/volume-mute.png", k_mute_end, 0);
        return;
    }
    const bool up   = to_underlying(want) > to_underlying(osd.shown);
    const auto next = static_cast<Level>(to_underlying(osd.shown) + (up ? 1 : -1));
    play(osd.speaker, format("transition/volume-{}-{}-{}.png", up ? "up" : "down", percent_of(osd.shown), percent_of(next)),
        0, k_step_end);
    osd.shown = next;
}

void fade_sign(float to) {
    const Object::Handle h = osd.sign.node;
    ctx().animate(Object::TAnimDesc{
        .h       = h,
        .to      = to,
        .t       = Timings::Quick,
        .delay   = 0.f,
        .p       = Prop::Opacity,
        .on_done = [h, to] {
            if (to == 0.f && !osd.sign.active && ctx().alive(h))
                ctx().set_visible(h, false);
        },
    });
}

void play_sign(int dir) {
    if (osd.sign.active && osd.sign_dir == dir) {
        osd.sign_again = true;
        return;
    }
    const bool visible = osd.sign.active || ctx().is_animating(osd.sign.node, Prop::Opacity);
    osd.sign_dir         = dir;
    osd.sign_again       = false;
    play(osd.sign, dir > 0 ? "sign/sign-plus.png" : "sign/sign-minus.png", 0, k_frames - 1);
    ctx().set_visible(osd.sign.node, true);
    if (!visible)
        ctx().set_opacity(osd.sign.node, 0.f);
    fade_sign(1.f);
}

void on_frame() {
    osd.frame_timer = {};
    if (!is_open())
        return;
    if (osd.speaker.active) {
        if (osd.speaker.frame == osd.speaker.end) {
            step_speaker();
        }
        else {
            osd.speaker.frame += osd.speaker.step;
            show_frame(osd.speaker);
        }
    }
    if (osd.sign.active) {
        if (++osd.sign.frame < k_frames) {
            show_frame(osd.sign);
        }
        else if (osd.sign_again) {
            osd.sign.frame = 0;
            osd.sign_again = false;
            show_frame(osd.sign);
        }
        else {
            osd.sign.active = false;
            osd.sign.frame  = k_frames - 1;
            fade_sign(0.f);
        }
    }
    if (osd.speaker.active || osd.sign.active)
        schedule_frame();
    Island::request_redraw(true);
}

// ---- Content ----------------------------------------------------------------

Object::Handle make_text(Object::Handle parent, FontHandle font, const Color& color) {
    const Object::Handle t = ctx().create_text(parent, {});
    ctx().text(t).set_font(font).set_color(color);
    return t;
}

Object::Handle make_icon(Object::Handle box, float dx = 0.f) {
    auto&                c = ctx();
    const Object::Handle h = c.create_image(box, Object::ImageDesc{
        .frame = {-k_art_left * k_icon + dx, (.5f - k_art_center_y) * k_icon, k_icon, k_icon},
    });
    c.set_size_mode(h, Object::Axis::Horizontal, Object::SizeMode::Fixed);
    c.set_size_mode(h, Object::Axis::Vertical, Object::SizeMode::Fixed);
    return h;
}

void build(Object::Handle bg) {
    auto& c = ctx();

    Object::LayoutData centered{};
    centered.align   = Object::Alignment::Center;
    centered.justify = Object::Justify::Center;
    c.set_layout(bg, centered);
    c.set_clip(bg, true);

    osd.content = c.create_group(bg, Frame{0.f, 0.f, static_cast<float>(SysInfoView::width()), k_height});

    if (kind == Kind::Power) {
        c.set_layout(osd.content, centered);
        osd.value = make_text(osd.content, fonts.label, Color::White);
        return;
    }

    Object::LayoutData bar{};
    bar.align   = Object::Alignment::Center;
    bar.justify = Object::Justify::SpaceBetween;
    bar.padding = {.left = 18.f, .top = 0.f, .right = 16.f, .bottom = 0.f};
    c.set_layout(osd.content, bar);

    const Object::Handle left = c.create_group(osd.content, Frame{0.f, 0.f, 0.f, 0.f});
    c.set_size_mode(left, Object::Axis::Horizontal, Object::SizeMode::Fit);
    c.set_size_mode(left, Object::Axis::Vertical, Object::SizeMode::Fit);
    Object::LayoutData row{};
    row.align   = Object::Alignment::Center;
    row.spacing = kind == Kind::Volume ? 4.f : 12.f;
    c.set_layout(left, row);

    if (kind == Kind::Volume) {
        const Object::Handle box = c.create_group(left, Frame{0.f, 0.f, (k_art_right - k_art_left) * k_icon - k_sign_shift, k_icon});
        osd.speaker                  = Clip{.node = make_icon(box)};
        osd.sign                     = Clip{.node = make_icon(box, -k_sign_shift)};
        c.set_visible(osd.sign.node, false);
    }
    else {
        c.text(make_text(left, fonts.label, k_label)).set_text("Brightness");
    }
    osd.value = make_text(left, fonts.value, Color::White);

    osd.ring = c.create_ring(osd.content, Object::RingDesc{
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
        c.text(osd.value).set_text(format("{}%", percent(v->level)));
        progress = v->level;
        if (!osd.speaker.active)
            step_speaker();
        break;
    }
    case Kind::Brightness: {
        const auto b = SysInfo::brightness();
        if (!b)
            return;
        c.text(osd.value).set_text(format("{}%", percent(*b)));
        progress = *b;
        break;
    }
    case Kind::Power: {
        if (const auto p = SysInfo::power())
            c.text(osd.value).set_text(power_text(*p));
        return;
    }
    }

    progress = clamp(progress, 0.f, 1.f);
    if (animate)
        c.animate(Object::TAnimDesc{.h = osd.ring, .to = progress, .t = Timings::Quick, .delay = 0.f, .p = Prop::Progress});
    else
        c.ring(osd.ring).set_progress(progress);
}

void on_hide() {
    osd.hide_timer = {};
    if (is_open() && Island::get_state() == StateType::Osd)
        switch_state(previous);
}

void restart_hide() {
    API::cancel_event(osd.hide_timer);
    osd.hide_timer = API::push_event(duration_cast<microseconds>(k_hide_after), on_hide, 0.f);
}

// Opens the OSD for `k`, or refreshes it if already showing. switch_state is synchronous,
// so on true the nodes exist and the caller can start extra animations right away.
bool show(Kind k) {
    const StateType s = Island::get_state();
    if (s == StateType::MusicPLayer || s == StateType::ControlCenter)
        return false;
    if (is_open() && kind == k) {
        refresh(true);
        restart_hide();
        Island::request_redraw(true);
        return false;
    }
    if (s != StateType::Osd)
        previous = s;
    kind = k;
    switch_state(StateType::Osd);
    return is_open();
}

void on_sysinfo(SysInfo::Update u) {
    if (SysInfo::contains(u, SysInfo::Update::Power)) {
        const auto p    = SysInfo::power();
        const auto prev = exchange(last_power, p);
        if (prev && p && prev->plugged != p->plugged) {
            show(Kind::Power);
        }
        else if (is_open() && kind == Kind::Power) {
            refresh(true);
            Island::request_redraw(true);
        }
    }

    else if (SysInfo::contains(u, SysInfo::Update::Volume)) {
        const auto v    = SysInfo::volume();
        const auto prev = exchange(last_volume, v);
        // nullopt -> value is (re)connecting, not the user changing anything.
        if (prev && v && *prev != *v) {
            const int  now    = percent(v->level), was = percent(prev->level);
            const bool opened = show(Kind::Volume);
            if (!is_open())
                return;
            if (opened && level_of(*prev) != level_of(*v)) {
                osd.shown = level_of(*prev);
                step_speaker();
            }
            if (v->muted == prev->muted && now != was)
                play_sign(now > was ? 1 : -1);
        }
    }

    else if (SysInfo::contains(u, SysInfo::Update::Brightness)) {
        const auto b    = SysInfo::brightness();
        const auto prev = exchange(last_brightness, b);
        if (prev && b && *prev != *b)
            show(Kind::Brightness);
    }

}

}

namespace SysInfoView {

void init(Object::Context& arg_ctx, const Fonts& f, void (*arg_switch_state)(StateType)) {
    context         = &arg_ctx;
    switch_state    = arg_switch_state;
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

void start(Object::Handle arg_bg) {
    stop();
    if (!arg_bg)
        return;
    build(arg_bg);

    if (kind == Kind::Volume) {
        osd.shown = target();
        show_rest(osd.shown);
    }
    refresh(false);

    auto& c = ctx();
    c.set_opacity(osd.content, 0.f);
    c.animate(Object::TAnimDesc{.h = osd.content, .to = 1.f, .t = Timings::Smooth, .delay = 0.f, .p = Prop::Opacity});
    restart_hide();
}

void stop() {
    API::cancel_event(osd.hide_timer);
    API::cancel_event(osd.frame_timer);
    if (osd.content) {
        auto& c = ctx();
        c.set_clip(c.parent(osd.content), false);
        c.destroy(osd.content);
    }
    osd = {};
}

uint32_t width() { return kind == Kind::Power ? k_text_w : k_progress_w; }

}