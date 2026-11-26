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
constexpr uint32_t k_icon_w     = 140; // icon-only split, as in the QML island
constexpr float    k_height     = 38.f;
constexpr float    k_icon       = 36.f;
constexpr float    k_ring       = 30.f;

// Where the drawing sits in its 64px cell. `right` includes the +/- sign when there is one.
struct Art {
    float left, right, center_y;
};

struct Sprites {
    string_view dir;
    string_view name;
    int         step_end; // transitions are settled from this frame on
    Art         art;
};

constexpr Sprites k_volume{"volume-icons", "volume", 19, {7.f / 64.f, 61.f / 64.f, 35.5f / 64.f}};
constexpr Sprites k_brightness{"brightness-icons", "brightness", 26, {3.f / 64.f, 62.f / 64.f, 38.f / 64.f}};
constexpr Art     k_battery_art{6.f / 64.f, 59.f / 64.f, 32.f / 64.f};

constexpr int   k_frames     = 30;
constexpr int   k_mute_end   = 22;
constexpr int   k_plug_end   = 18;
constexpr int   k_unplug_end = 15;
constexpr float k_sign_shift = 5.f; // pulls the sign toward the icon

// The sign strip is a seamless loop with no in/out frames, so those are opacity fades.

constexpr auto k_frame_time = 16667us;
constexpr auto k_hide_after = 1250ms;

const Color k_track{1.f, 1.f, 1.f, .16f};

struct Clip {
    Object::Handle node{};
    string         strip{}; // loaded sprite, so replaying it (loops) skips the decode
    int            frame{};
    int            end{};
    int            step{1};
    bool           active{};
};

// One opening of the OSD; stop() resets it as a whole. Timer callbacks carry no context,
// so this has to live at file scope.
struct Osd {
    Object::Handle     content{}, value{}, ring{};
    Clip               icon{}, sign{};
    Level              shown{};
    bool               shown_plugged{};
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

const Sprites& sprites() { return kind == Kind::Brightness ? k_brightness : k_volume; }

int percent(float v) { return static_cast<int>(lround(v * 100.f)); }

Level quarter(float v) { return static_cast<Level>(clamp(static_cast<int>(ceil(v * 4.f - 1e-3f)), 1, 4) - 1); }

Level level_of(const SysInfo::Volume& v) { return v.muted ? Level::Muted : quarter(v.level); }

Level target() {
    if (kind == Kind::Brightness) {
        const auto b = SysInfo::brightness();
        return b ? quarter(*b) : Level::L100;
    }
    const auto v = SysInfo::volume();
    return v ? level_of(*v) : Level::L100;
}

int percent_of(Level l) { return (to_underlying(l) + 1) * 25; }

bool plugged() {
    const auto p = SysInfo::power();
    return p && p->plugged;
}

bool charging() {
    const auto p = SysInfo::power();
    return p && p->plugged && p->status == SysInfo::Charge::Charging;
}

// ---- Sprites ----------------------------------------------------------------

bool load(Clip& c, const string& sprite, int frames) {
    if (c.strip == sprite)
        return true;
    const float scale = Renderer::text_scale();
    const int   side  = max(1, static_cast<int>(lround(k_icon * scale)));
    const auto  path  = format("{}/{}", TIDE_RESOURCE_DIR, sprite);
    auto        px    = Image::load_strip(path.c_str(), frames, side);
    if (!px) {
        c.strip.clear();
        return false;
    }
    Texture t{};
    t.upload(px->width, px->height, Texture::Format::RGBA8, px->rgba);
    t.set_scale(scale);
    ctx().image(c.node).set_texture(std::move(t));
    c.strip = sprite;
    return true;
}

void show_frame(const Clip& c) {
    constexpr float w = 1.f / k_frames;
    ctx().image(c.node).set_uv({.u0 = static_cast<float>(c.frame) * w, .v0 = 0.f,
                                .u1 = static_cast<float>(c.frame + 1) * w, .v1 = 1.f});
}

void on_frame();

void schedule_frame() {
    if (!Timer::is_pending(osd.frame_timer))
        osd.frame_timer = API::push_event(k_frame_time, on_frame, 0.f);
}

void play(Clip& c, const string& sprite, int from, int to) {
    c.active = load(c, sprite, k_frames);
    if (!c.active)
        return;
    c.frame = from;
    c.end   = to;
    c.step  = to >= from ? 1 : -1;
    show_frame(c);
    schedule_frame();
}

void rest(const string& sprite) {
    osd.icon.active = false;
    if (load(osd.icon, sprite, 1))
        ctx().image(osd.icon.node).set_uv({});
}

void rest_muted() {
    osd.icon.active = false;
    if (load(osd.icon, format("{}/mute/volume-mute.png", k_volume.dir), k_frames)) {
        osd.icon.frame = k_frames - 1;
        show_frame(osd.icon);
    }
}

// One clip per call; adjacent levels only, so 25 -> 100 plays three steps.
void step_level() {
    const Sprites& s    = sprites();
    const Level    want = target();
    const string   mute = format("{}/mute/volume-mute.png", s.dir);
    if (osd.shown == want) {
        if (want == Level::Muted)
            rest_muted();
        else
            rest(format("{}/static/{}-{}.png", s.dir, s.name, percent_of(want)));
        return;
    }
    if (want == Level::Muted) {
        osd.shown = Level::Muted;
        play(osd.icon, mute, 0, k_mute_end);
        return;
    }
    if (osd.shown == Level::Muted) {
        // The mute strip is drawn at another size, so unmuting ends on a cut to the static icon.
        osd.shown = want;
        play(osd.icon, mute, k_mute_end, 0);
        return;
    }
    const bool up   = to_underlying(want) > to_underlying(osd.shown);
    const auto next = static_cast<Level>(to_underlying(osd.shown) + (up ? 1 : -1));
    play(osd.icon, format("{}/transition/{}-{}-{}-{}.png", s.dir, s.name, up ? "up" : "down", percent_of(osd.shown), percent_of(next)),
         0, s.step_end);
    osd.shown = next;
}

// Plug in / unplug transition first, then the charging loop for as long as it charges.
void step_battery() {
    const bool now = plugged();
    if (osd.shown_plugged != now) {
        osd.shown_plugged = now;
        if (now)
            play(osd.icon, "charging-icons/transition/charge-plug-in.png", 1, k_plug_end); // 0 == 1
        else
            play(osd.icon, "charging-icons/transition/charge-unplug.png", 1, k_unplug_end);
        return;
    }
    if (charging())
        play(osd.icon, "charging-icons/loop/charging-loop.png", 0, k_frames - 1);
    else
        rest(now ? "charging-icons/static/battery-charging.png" : "charging-icons/static/battery.png");
}

void step_icon() {
    if (kind == Kind::Power)
        step_battery();
    else
        step_level();
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
    osd.sign_dir       = dir;
    osd.sign_again     = false;
    play(osd.sign, format("{}/sign/sign-{}.png", sprites().dir, dir > 0 ? "plus" : "minus"), 0, k_frames - 1);
    ctx().set_visible(osd.sign.node, true);
    if (!visible)
        ctx().set_opacity(osd.sign.node, 0.f);
    fade_sign(1.f);
}

void on_frame() {
    osd.frame_timer = {};
    if (!is_open())
        return;
    if (osd.icon.active) {
        if (osd.icon.frame == osd.icon.end) {
            step_icon();
        }
        else {
            osd.icon.frame += osd.icon.step;
            show_frame(osd.icon);
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
    if (osd.icon.active || osd.sign.active)
        schedule_frame();
    Island::request_redraw(true);
}

// ---- Content ----------------------------------------------------------------

Object::Handle make_text(Object::Handle parent, FontHandle font, const Color& color) {
    const Object::Handle t = ctx().create_text(parent, {});
    ctx().text(t).set_font(font).set_color(color);
    return t;
}

Object::Handle make_icon(Object::Handle box, const Art& art, float dx = 0.f) {
    auto&                c = ctx();
    const Object::Handle h = c.create_image(box, Object::ImageDesc{
        .frame = {-art.left * k_icon + dx, (.5f - art.center_y) * k_icon, k_icon, k_icon},
    });
    c.set_size_mode(h, Object::Axis::Horizontal, Object::SizeMode::Fixed);
    c.set_size_mode(h, Object::Axis::Vertical, Object::SizeMode::Fixed);
    return h;
}

// Sized to the drawing rather than the cell, so layout spacing is measured from the art.
Object::Handle make_icon_box(Object::Handle parent, const Art& art, float shrink = 0.f) {
    return ctx().create_group(parent, Frame{0.f, 0.f, (art.right - art.left) * k_icon - shrink, k_icon});
}

void build(Object::Handle bg) {
    auto& c = ctx();

    Object::LayoutData centered{};
    centered.align   = Object::Alignment::Center;
    centered.justify = Object::Justify::Center;
    c.set_layout(bg, centered);
    c.set_clip(bg, true);

    osd.content = c.create_group(bg, Frame{0.f, 0.f, static_cast<float>(SysInfoView::width()), k_height});

    Object::LayoutData row{};
    row.align = Object::Alignment::Center;

    if (kind == Kind::Power) {
        row.justify = Object::Justify::Center;
        c.set_layout(osd.content, row);
        osd.icon = Clip{.node = make_icon(make_icon_box(osd.content, k_battery_art), k_battery_art)};
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
    row.spacing = 4.f;
    c.set_layout(left, row);

    const Art&           art = sprites().art;
    const Object::Handle box = make_icon_box(left, art, k_sign_shift);
    osd.icon                 = Clip{.node = make_icon(box, art)};
    osd.sign                 = Clip{.node = make_icon(box, art, -k_sign_shift)};
    c.set_visible(osd.sign.node, false);
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
        if (!osd.icon.active)
            step_icon();
        return;
    }
    }
    if (!osd.icon.active)
        step_icon();

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

void restart_hide(microseconds after = k_hide_after) {
    API::cancel_event(osd.hide_timer);
    osd.hide_timer = API::push_event(after, on_hide, 0.f);
}

// Opens the OSD for `k`, or refreshes it if already showing. switch_state is synchronous,
// so on true the nodes exist and the caller can start extra animations right away.
bool show(Kind k) {
    if (Island::get_island().current_priority > Island::k_priority_sys_info)
        return false;
    const StateType s = Island::get_state();
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

// Shared by volume and brightness: animate from the old level when freshly opened, and
// show +/- for the direction of the change.
void level_changed(Kind k, Level from, Level to, int was, int now, bool sign) {
    const bool opened = show(k);
    if (!is_open() || kind != k)
        return;
    if (opened && from != to) {
        osd.shown = from;
        step_icon();
    }
    if (sign && now != was)
        play_sign(now > was ? 1 : -1);
}

void on_sysinfo(SysInfo::Update u) {
    // Several sources can change in one wakeup, so each is checked on its own.
    if (SysInfo::contains(u, SysInfo::Update::Power)) {
        const auto p    = SysInfo::power();
        const auto prev = exchange(last_power, p);
        if (prev && p && prev->plugged != p->plugged) {
            const bool opened = show(Kind::Power);
            if (is_open() && kind == Kind::Power) {
                if (opened)
                    osd.shown_plugged = prev->plugged;
                step_icon();
                restart_hide();
            }
        }
        else if (is_open() && kind == Kind::Power) {
            refresh(true);
            Island::request_redraw(true);
        }
    }

    if (SysInfo::contains(u, SysInfo::Update::Volume)) {
        const auto v    = SysInfo::volume();
        const auto prev = exchange(last_volume, v);
        // nullopt -> value is (re)connecting, not the user changing anything.
        if (prev && v && *prev != *v)
            level_changed(Kind::Volume, level_of(*prev), level_of(*v), percent(prev->level), percent(v->level),
                          v->muted == prev->muted);
    }

    if (SysInfo::contains(u, SysInfo::Update::Brightness)) {
        const auto b    = SysInfo::brightness();
        const auto prev = exchange(last_brightness, b);
        if (prev && b && *prev != *b)
            level_changed(Kind::Brightness, quarter(*prev), quarter(*b), percent(*prev), percent(*b), true);
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

    if (kind == Kind::Power)
        osd.shown_plugged = plugged();
    else
        osd.shown = target();
    refresh(false);

    auto& c = ctx();
    c.set_opacity(osd.content, 0.f);
    // Power keeps the clock's size, so nothing to wait for: show it almost at once.
    const Timing& fade = kind == Kind::Power ? Timings::Instant : Timings::Smooth;
    c.animate(Object::TAnimDesc{.h = osd.content, .to = 1.f, .t = fade, .delay = 0.f, .p = Prop::Opacity});
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

uint32_t width() { return kind == Kind::Power ? k_icon_w : k_progress_w; }

}
