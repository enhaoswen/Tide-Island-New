#include "scene/object.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <memory>
#include <ranges>
#include <utility>
#include <variant>
#include <vector>

#include "app/island.hpp"
#include "backend/wayland.hpp"
#include "graphics/renderer.hpp"
#include "utils/log.hpp"

using namespace std;
using namespace std::chrono;

namespace {

using SceneValue = variant<
    unique_ptr<Object::Rectangle>,
    unique_ptr<Object::Image>,
    unique_ptr<Object::Text>,
    unique_ptr<Object::ClickableRect>,
    unique_ptr<Object::ClickableImage>,
    unique_ptr<Object::ClickableText>,
    unique_ptr<Object::Layout> >;

struct SceneObject {
    SceneValue      value;
    Object::Layout* parent{};
};

// Append-only until clear(): indices survive vector reallocations.
vector<SceneObject> objects;
constexpr uint32_t no_object = numeric_limits<uint32_t>::max();

uint32_t find_object(const void* pointer) {
    if (!pointer) {
        return no_object;
    }
    for (size_t i = 0; i < objects.size(); ++i) {
        if (visit([pointer](const auto& item) {
            using T = remove_reference_t<decltype(*item)>;
            if constexpr (derived_from<T, Object::Rectangle>) {
                return pointer == item.get() || pointer == static_cast<Object::Rectangle*>(item.get());
            }
            else if constexpr (derived_from<T, Object::Image>) {
                return pointer == item.get() || pointer == static_cast<Object::Image*>(item.get());
            }
            else if constexpr (derived_from<T, Object::Text>) {
                return pointer == item.get() || pointer == static_cast<Object::Text*>(item.get());
            }
            else {
                return pointer == item.get();
            }
        }, objects[i].value)) {
            return static_cast<uint32_t>(i);
        }
    }
    return no_object;
}

template <typename T, typename Desc>
T* add_object(const Desc& desc) {
    // Compact membership indices are sufficient for up to 2^32 - 1 objects.
    if (objects.size() >= no_object) {
        Log::fatal("Scene object limit reached");
    }
    auto object = make_unique<T>(desc);
    auto* handle = object.get();
    objects.emplace_back(std::move(object));
    Island::request_redraw(true);
    return handle;
}

bool valid_align(Align align) {
    return align == Align::Left || align == Align::Center || align == Align::Right;
}

bool valid_frame(const Frame& frame) {
    return frame != EMPTY_FRAME && isfinite(frame.x) && isfinite(frame.y) &&
        isfinite(frame.width) && isfinite(frame.height) && frame.width >= 0 && frame.height >= 0;
}

bool valid_layout(const LayoutDesc& desc) {
    return valid_frame(desc.frame) && isfinite(desc.spacing) && desc.spacing >= 0 && valid_align(desc.cross_align);
}

Frame intersection(const Frame& a, const Frame& b) {
    const float x = max(a.x, b.x);
    const float y = max(a.y, b.y);
    return {x, y, max(0.0f, min(a.x + a.width, b.x + b.width) - x),
        max(0.0f, min(a.y + a.height, b.y + b.height) - y)};
}

Frame surface_frame() {
    const auto size = Wayland::get_surface_size();
    return {0, 0, static_cast<float>(size[0]), static_cast<float>(size[1])};
}

bool contains(const Frame& frame, float x, float y) {
    return x >= frame.x && x < frame.x + frame.width &&
        y >= frame.y && y < frame.y + frame.height;
}

bool click_object(const Frame& frame, float radius, const ClickCallback& callback, float x, float y) {
    if (frame.width <= 0 || frame.height <= 0 ||
        x < frame.x || x > frame.x + frame.width ||
        y < frame.y || y > frame.y + frame.height) {
        return false;
    }
    const float corner_radius = clamp(radius, 0.0f, max(0.0f, min(frame.width, frame.height) / 2));
    const float dx            = x - clamp(x, frame.x + corner_radius, frame.x + frame.width - corner_radius);
    const float dy            = y - clamp(y, frame.y + corner_radius, frame.y + frame.height - corner_radius);
    if (dx * dx + dy * dy > corner_radius * corner_radius || !callback) {
        return false;
    }
    callback();
    return true;
}

template <typename desc_type>
    requires (same_as<desc_type, RectDesc> ||
        same_as<desc_type, ImageDesc> ||
        same_as<desc_type, TextDesc> ||
        same_as<desc_type, LayoutDesc>
    )
float* animation_target(desc_type& type, AnimationTarget target) {
    switch (target) {
    case AnimationTarget::Width:
        return &type.frame.width;
    case AnimationTarget::Height:
        return &type.frame.height;
    case AnimationTarget::X:
        return &type.frame.x;
    case AnimationTarget::Y:
        return &type.frame.y;

    case AnimationTarget::Radius:
        if constexpr (requires { type.radius; })
            return &type.radius;
        return nullptr;

    case AnimationTarget::ColorR:
    case AnimationTarget::ColorG:
    case AnimationTarget::ColorB:
    case AnimationTarget::ColorA:
        if constexpr (requires { type.color; }) {
            const auto index = static_cast<unsigned>(target)
                - static_cast<unsigned>(AnimationTarget::ColorR);
            return &type.color[index];
        }
        return nullptr;
    default:
        return nullptr;
    }
}

void add_animation(
    vector<unique_ptr<Animation> >& animations, float* target_ptr, const AnimationDesc& anim_desc) {
    auto existing = find_if(animations.begin(), animations.end(), [target_ptr](const auto& anim) {
        return anim->target_ptr == target_ptr;
    });

    if (existing != animations.end()) {
        if (!((*existing)->priority <= anim_desc.priority)) {
            return;
        }
        if ((*existing)->priority == anim_desc.priority) {
            Log::logger(Log::Warning,
                "Animation conflict: target={}, from={}, to={}",
                static_cast<const void*>((*existing)->target_ptr),
                (*existing)->from,
                (*existing)->to
                );
        }
    }

    auto animation = make_unique<Animation>();

    animation->from       = *target_ptr;
    animation->to         = anim_desc.to;
    animation->target_ptr = target_ptr;
    animation->priority   = anim_desc.priority;
    animation->duration   = anim_desc.duration;
    animation->start_time = chrono::steady_clock::now();

    if (existing != animations.end()) {
        *existing = move(animation);
    }
    else {
        animations.push_back(move(animation));
    }
}

void update(vector<unique_ptr<Animation> >& animations) {
    const auto  now   = chrono::steady_clock::now();
    const float scale = Island::state().animation_scale;

    erase_if(animations, [now, scale](const unique_ptr<Animation>& animation) {
        const float elapsed =
            chrono::duration<float, milli>(
                now - animation->start_time
                ).count();

        const float progress = animation->duration == 0
            ? 1.0f
            : clamp(
                elapsed * scale / animation->duration,
                0.0f, 1.0f
                );

        const bool finished = progress >= 1.0f;

        *animation->target_ptr = finished
            ? animation->to
            : animation->from +
            (animation->to - animation->from) * progress;

        return finished;
    });
}

} // namespace

// Scene-only access keeps layout bookkeeping out of individual elements.
struct Object::DontUse::SceneAccess {
    static bool supports_auto_size(size_t index) {
        return visit([](const auto& object) {
            using T = remove_reference_t<decltype(*object)>;
            return derived_from<T, Image> || derived_from<T, Text>;
        }, objects[index].value);
    }

    static bool fit_content(size_t index) {
        return visit([](const auto& object) {
            using T = remove_reference_t<decltype(*object)>;
            if constexpr (derived_from<T, Image> || derived_from<T, Text>) {
                object->prepare();
                Frame frame = object->desc.frame;
                if (frame == EMPTY_FRAME) {
                    frame = {};
                }
                frame.width = static_cast<float>(object->texture.width());
                frame.height = static_cast<float>(object->texture.height());
                if constexpr (derived_from<T, Text>) {
                    // Match draw_text, including fractional-scale buffer rounding.
                    const auto surface = Wayland::get_surface_size();
                    const auto buffer = Wayland::get_buffer_size();
                    frame.width /= static_cast<float>(buffer[0]) / surface[0];
                    frame.height /= static_cast<float>(buffer[1]) / surface[1];
                }
                if (frame != object->desc.frame) {
                    object->desc.frame = frame;
                    return true;
                }
            }
            return false;
        }, objects[index].value);
    }

    static bool update_layout(Layout& layout) {
        bool changed = false;
        array<size_t, 3> counts{};
        layout.lengths = {};
        for (const auto& item : layout.elements) {
            if (item.auto_size) {
                changed = fit_content(item.index) || changed;
            }
            const Frame frame = visit([](const auto& object) { return object->get_frame(); }, objects[item.index].value);
            const auto group = static_cast<size_t>(item.align);
            if (counts[group]++) {
                layout.lengths[group] += layout.desc.spacing;
            }
            layout.lengths[group] += max(0.0f, layout.desc.vertical ? frame.height : frame.width);
        }

        const auto& desc = layout.desc;
        const float start = desc.vertical ? desc.frame.y : desc.frame.x;
        const float length = desc.vertical ? desc.frame.height : desc.frame.width;
        array<float, 3> cursor{
            start,
            start + (length - layout.lengths[1]) / 2,
            start + length - layout.lengths[2],
        };
        for (const auto& item : layout.elements) {
            visit([&](const auto& object) {
                Frame frame = object->desc.frame;
                const auto group = static_cast<size_t>(item.align);
                const float cross_length = desc.vertical ? desc.frame.width : desc.frame.height;
                const float cross_size = desc.vertical ? frame.width : frame.height;
                float cross = desc.vertical ? desc.frame.x : desc.frame.y;
                if (desc.cross_align == Align::Center) {
                    cross += (cross_length - cross_size) / 2;
                }
                else if (desc.cross_align == Align::Right) {
                    cross += cross_length - cross_size;
                }
                frame.x = desc.vertical ? cross : cursor[group];
                frame.y = desc.vertical ? cursor[group] : cross;
                cursor[group] += max(0.0f, desc.vertical ? frame.height : frame.width) + desc.spacing;
                if (frame != object->desc.frame) {
                    object->desc.frame = frame;
                    changed = true;
                }
                if constexpr (same_as<remove_reference_t<decltype(*object)>, Layout>) {
                    changed = update_layout(*object) || changed;
                }
            }, objects[item.index].value);
        }
        return changed;
    }

    static array<Frame, 3> group_clips(const Layout& layout, const Frame& inherited) {
        const auto& frame = layout.desc.frame;
        const float length = max(0.0f, layout.desc.vertical ? frame.height : frame.width);
        float left = min(layout.lengths[0], length);
        float right = min(layout.lengths[2], length);
        const float overlap = max(0.0f, left + right - length) / 2;
        left -= overlap;
        right -= overlap;
        const array<float, 4> edges{0, left, length - right, length};
        array<Frame, 3> clips;
        for (size_t i = 0; i < clips.size(); ++i) {
            Frame clip = frame;
            if (layout.desc.vertical) {
                clip.y += edges[i];
                clip.height = edges[i + 1] - edges[i];
            }
            else {
                clip.x += edges[i];
                clip.width = edges[i + 1] - edges[i];
            }
            clips[i] = intersection(inherited, clip);
        }
        return clips;
    }

    static void draw(size_t index, const Frame& clip) {
        if (clip.width <= 0 || clip.height <= 0) {
            return;
        }
        visit([&](const auto& object) {
            if constexpr (same_as<remove_reference_t<decltype(*object)>, Layout>) {
                const auto clips = group_clips(*object, clip);
                for (const auto& item : object->elements) {
                    draw(item.index, clips[static_cast<size_t>(item.align)]);
                }
            }
            else {
                Renderer::set_clip(clip);
                object->draw();
            }
        }, objects[index].value);
    }

    static bool click(size_t index, const Frame& clip, float x, float y, bool left) {
        if (!contains(clip, x, y)) {
            return false;
        }
        return visit([&](const auto& object) {
            if constexpr (same_as<remove_reference_t<decltype(*object)>, Layout>) {
                const auto clips = group_clips(*object, clip);
                for (const auto& item : views::reverse(object->elements)) {
                    if (click(item.index, clips[static_cast<size_t>(item.align)], x, y, left)) {
                        return true;
                    }
                }
            }
            else if constexpr (requires { object->click(x, y, left); }) {
                return object->click(x, y, left);
            }
            return false;
        }, objects[index].value);
    }
};

template <typename Desc>
Object::Element<Desc>::Element(const Desc& arg_desc) : desc(arg_desc) {
}

template <typename Desc>
void Object::Element<Desc>::replace_desc(const Desc& arg_desc) {
    desc = arg_desc;
    animations.clear();
    Island::request_redraw(true);
}

template <typename Desc>
void Object::Element<Desc>::set_frame(const Frame& frame) {
    if (desc.frame == frame) {
        return;
    }
    desc.frame = frame;
    Island::request_redraw(true);
}

template <typename Desc>
Frame Object::Element<Desc>::get_frame() const {
    return desc.frame;
}

template <typename Desc>
void Object::Element<Desc>::add_animation(AnimationDesc anim_desc, source_location l) {
    float* target_ptr = animation_target(desc, anim_desc.target);
    if (target_ptr == nullptr) {
        Log::logger(Log::Error,
            "{}:{}: Unsupported animation target, id = {}",
            l.file_name(), l.line(), static_cast<int>(anim_desc.target));
        return;
    }
    ::add_animation(animations, target_ptr, anim_desc);
    Island::request_redraw(true);
}

template <typename Desc>
void Object::Element<Desc>::update() {
    ::update(animations);
}

template <typename Desc>
bool Object::Element<Desc>::has_active_animations() const {
    return !animations.empty();
}

template class Object::Element<RectDesc>;
template class Object::Element<ImageDesc>;
template class Object::Element<TextDesc>;
template class Object::Element<LayoutDesc>;

void Object::DontUse::ClickableItem::set_callback_left(ClickCallback left) {
    callback_left = move(left);
}

void Object::DontUse::ClickableItem::set_callback_right(ClickCallback right) {
    callback_right = move(right);
}

void Object::Rectangle::set_desc(const RectDesc& arg_desc) {
    replace_desc(arg_desc);
}

void Object::Rectangle::set_radius(float radius) {
    desc.radius = radius;
    Island::request_redraw(true);
}

void Object::Rectangle::set_color(const Color& color) {
    desc.color = color;
    Island::request_redraw(true);
}

void Object::Rectangle::draw() {
    Renderer::draw_rectangle(desc);
}

Object::Image::Image(const ImageDesc& arg_desc) : Element(arg_desc) {
    Renderer::prepare_image(desc, texture);
}

void Object::Image::set_desc(const ImageDesc& arg_desc) {
    texture_dirty = texture_dirty || desc.path != arg_desc.path;
    replace_desc(arg_desc);
}

void Object::Image::set_radius(float radius) {
    desc.radius = radius;
    Island::request_redraw(true);
}

void Object::Image::set_path(string_view path) {
    if (desc.path == path) {
        return;
    }
    desc.path = path;
    reload();
}

void Object::Image::reload() {
    texture_dirty = true;
    Island::request_redraw(true);
}

void Object::Image::prepare() {
    if (texture_dirty) {
        Renderer::prepare_image(desc, texture);
        texture_dirty = false;
    }
}

void Object::Image::draw() {
    prepare();
    Renderer::draw_image(desc, texture);
}

Object::Text::Text(const TextDesc& arg_desc) : Element(arg_desc) {
    texture_scale = Wayland::get_scale();
    Renderer::prepare_text(desc, texture);
}

void Object::Text::set_desc(const TextDesc& arg_desc) {
    texture_dirty = texture_dirty || desc.text != arg_desc.text || desc.font.id != arg_desc.font.id;
    replace_desc(arg_desc);
}

void Object::Text::set_color(const Color& color) {
    desc.color = color;
    Island::request_redraw(true);
}

void Object::Text::set_text(string_view text) {
    if (desc.text == text) {
        return;
    }
    desc.text     = text;
    texture_dirty = true;
    Island::request_redraw(true);
}

void Object::Text::set_font(FontHandle font) {
    if (desc.font.id == font.id) {
        return;
    }
    desc.font     = font;
    texture_dirty = true;
    Island::request_redraw(true);
}

void Object::Text::prepare() {
    const float scale = Wayland::get_scale();
    if (texture_dirty || texture_scale != scale) {
        Renderer::prepare_text(desc, texture);
        texture_dirty = false;
        texture_scale = scale;
    }
}

void Object::Text::draw() {
    prepare();
    Renderer::draw_text(desc, texture);
}

bool Object::ClickableRect::click(float x, float y, bool left) {
    return click_object(desc.frame, desc.radius, left ? callback_left : callback_right, x, y);
}

bool Object::ClickableImage::click(float x, float y, bool left) {
    return click_object(desc.frame, desc.radius, left ? callback_left : callback_right, x, y);
}

bool Object::ClickableText::click(float x, float y, bool left) {
    return click_object(desc.frame, 0.0f, left ? callback_left : callback_right, x, y);
}

Object::Layout::Layout(const LayoutDesc& arg_desc) : Element(arg_desc) {
    if (!valid_layout(arg_desc)) {
        Log::logger(Log::Error, "Invalid layout descriptor");
        desc = {};
    }
}

void Object::Layout::set_desc(const LayoutDesc& arg_desc) {
    if (!valid_layout(arg_desc)) {
        Log::logger(Log::Error, "Invalid layout descriptor");
        return;
    }
    replace_desc(arg_desc);
}

void Object::Layout::set_frame(const Frame& frame) {
    if (!valid_frame(frame)) {
        Log::logger(Log::Error, "Invalid layout frame");
        return;
    }
    Element::set_frame(frame);
}

void Object::Layout::set_spacing(float spacing) {
    if (!isfinite(spacing) || spacing < 0) {
        Log::logger(Log::Error, "Layout spacing must be finite and nonnegative");
        return;
    }
    if (desc.spacing != spacing) {
        desc.spacing = spacing;
        Island::request_redraw(true);
    }
}

void Object::Layout::set_vertical(bool vertical) {
    if (desc.vertical != vertical) {
        desc.vertical = vertical;
        Island::request_redraw(true);
    }
}

void Object::Layout::set_cross_align(Align align) {
    if (!valid_align(align)) {
        Log::logger(Log::Error, "Invalid layout alignment");
        return;
    }
    if (desc.cross_align != align) {
        desc.cross_align = align;
        Island::request_redraw(true);
    }
}

bool Object::Layout::add_element_impl(const void* element, Align align) {
    const auto index = find_object(element);
    if (find_object(this) == no_object || index == no_object || !valid_align(align)) {
        Log::logger(Log::Error, "Layout and element must be registered in the scene, with a valid alignment");
        return false;
    }
    auto& entry = objects[index];
    if (entry.parent && entry.parent != this) {
        Log::logger(Log::Error, "Element already belongs to another layout");
        return false;
    }
    if (const auto* child = get_if<unique_ptr<Layout>>(&entry.value)) {
        for (auto* ancestor = this; ancestor; ancestor = objects[find_object(ancestor)].parent) {
            if (ancestor == child->get()) {
                Log::logger(Log::Error, "Layout membership would create a cycle");
                return false;
            }
        }
    }

    const Frame frame = visit([](const auto& object) { return object->get_frame(); }, entry.value);
    const bool auto_size = frame == EMPTY_FRAME;
    if (auto_size ? !DontUse::SceneAccess::supports_auto_size(index) : !valid_frame(frame)) {
        Log::logger(Log::Error, "Layout element needs a valid frame; only images and text support EMPTY_FRAME");
        return false;
    }

    auto existing = ranges::find(elements, index, &Item::index);
    if (existing != elements.end()) {
        if (existing->align == align && !auto_size) {
            return true;
        }
        existing->align = align;
        existing->auto_size = existing->auto_size || auto_size;
    }
    else {
        elements.push_back({index, align, auto_size});
        entry.parent = this;
    }
    if (auto_size) {
        DontUse::SceneAccess::fit_content(index);
    }
    Island::request_redraw(true);
    return true;
}

bool Object::Layout::delete_element_impl(const void* element) {
    const auto index = find_object(element);
    const auto existing = ranges::find(elements, index, &Item::index);
    if (existing == elements.end()) {
        return false;
    }
    objects[index].parent = nullptr;
    elements.erase(existing);
    Island::request_redraw(true);
    return true;
}

bool Object::Layout::set_auto_size_impl(const void* element, bool enabled) {
    const auto index = find_object(element);
    const auto existing = ranges::find(elements, index, &Item::index);
    if (existing == elements.end() || !DontUse::SceneAccess::supports_auto_size(index)) {
        return false;
    }
    bool changed = existing->auto_size != enabled;
    existing->auto_size = enabled;
    if (enabled) {
        changed = DontUse::SceneAccess::fit_content(index) || changed;
    }
    if (changed) {
        Island::request_redraw(true);
    }
    return true;
}

void Object::Layout::update() {
    if (DontUse::SceneAccess::update_layout(*this)) {
        Island::request_redraw(true);
    }
}

Object::Rectangle* Object::add_rectangle(const RectDesc& desc) {
    return add_object<Rectangle>(desc);
}

Object::Image* Object::add_image(const ImageDesc& desc) {
    return add_object<Image>(desc);
}

Object::Text* Object::add_text(const TextDesc& desc) {
    return add_object<Text>(desc);
}

Object::ClickableRect* Object::add_clickable_rect(const RectDesc& desc) {
    return add_object<ClickableRect>(desc);
}

Object::ClickableImage* Object::add_clickable_image(const ImageDesc& desc) {
    return add_object<ClickableImage>(desc);
}

Object::ClickableText* Object::add_clickable_text(const TextDesc& desc) {
    return add_object<ClickableText>(desc);
}

Object::Layout* Object::add_layout(const LayoutDesc& desc) {
    return add_object<Layout>(desc);
}

void Object::click(float x, float y, bool left) {
    const Frame clip = surface_frame();
    for (size_t i = objects.size(); i-- > 0;) {
        if (!objects[i].parent && DontUse::SceneAccess::click(i, clip, x, y, left)) {
            return;
        }
    }
}

void Object::draw() {
    const Frame clip = surface_frame();
    for (size_t i = 0; i < objects.size(); ++i) {
        if (!objects[i].parent) {
            DontUse::SceneAccess::draw(i, clip);
        }
    }
    Renderer::set_clip(clip);
}

void Object::update() {
    for (auto& object : objects) {
        visit([](const auto& item) {
            if constexpr (same_as<remove_reference_t<decltype(*item)>, Layout>) {
                item->Element<LayoutDesc>::update();
            }
            else {
                item->update();
            }
        }, object.value);
    }
    for (auto& object : objects) {
        if (!object.parent) {
            if (const auto* layout = get_if<unique_ptr<Layout>>(&object.value)) {
                DontUse::SceneAccess::update_layout(**layout);
            }
        }
    }
}

bool Object::has_active_animations() {
    return ranges::any_of(objects, [](const auto& object) {
        return visit([](const auto& item) { return item->has_active_animations(); }, object.value);
    });
}

void Object::clear() {
    objects.clear();
    Island::request_redraw(true);
}
