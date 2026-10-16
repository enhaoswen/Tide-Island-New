#pragma once

#include "utils/struct.hpp"
#include "graphics/texture.hpp"

#include <source_location>
#include <string_view>
#include <vector>

template<typename T>
concept is_desc =
    std::same_as<T, RectDesc> ||
    std::same_as<T, ImageDesc> ||
    std::same_as<T, TextDesc>;
template<typename T>
concept is_rect = std::same_as<T, RectDesc>;
template<typename T>
concept is_img = std::same_as<T, ImageDesc>;
template<typename T>
concept is_text = std::same_as<T, TextDesc>;

template <typename desc_type>
requires is_desc<desc_type>
class Object {
private:
    desc_type desc{};
    Texture texture;
public:

    Object(desc_type arg_desc);
    Object(const Object& arg_obj) = delete;
    Object& operator=(const Object& arg_obj) = delete;

    void add_animation(AnimationDesc anim_desc, std::source_location location = std::source_location::current());

    bool click(float x, float y, bool left);
    void update();
    void draw();

    void set_click_callback_right(void (*right)());
    void set_click_callback_left(void (*left)());
};

namespace Object_t {

class ItemBase_t {
private:
    std::vector<Animation> animations;

protected:
    using ClickCallback = void (*)();

    virtual Frame& mutable_frame() = 0;
    virtual ClickCallback& mutable_click_callback(bool left) = 0;
    virtual float* radius();
    virtual Color* color();

public:
    ItemBase_t() = default;
    virtual ~ItemBase_t() = default;
    ItemBase_t(const ItemBase_t&) = delete;
    ItemBase_t& operator=(const ItemBase_t&) = delete;
    void set_click_callback_right(void (*right)());
    void set_click_callback_left(void (*left)());

    [[nodiscard]] virtual const Frame& frame() const = 0;
    void set_frame(const Frame& frame);
    void add_animation(AnimationDesc anim_desc, std::source_location location = std::source_location::current());
    void update();
    virtual void draw() = 0;
    virtual bool click(float x, float y, bool left);
};

class Rectangle : public ItemBase_t {
private:
    RectDesc desc;
    Frame& mutable_frame() override;
    ClickCallback& mutable_click_callback(bool left) override;
    float* radius() override;
    Color* color() override;

public:
    explicit Rectangle(const RectDesc& desc);
    const Frame& frame() const override;
    void set_desc(const RectDesc& desc);
    void set_radius(float radius);
    void set_color(const Color& color);
    void draw() override;
};

class Image : public ItemBase_t {
private:
    ImageDesc desc;
    Texture texture;
    bool texture_dirty{true};
    Frame& mutable_frame() override;
    ClickCallback& mutable_click_callback(bool left) override;
    float* radius() override;

public:
    explicit Image(const ImageDesc& desc);
    [[nodiscard]] const Frame& frame() const override;
    void set_desc(const ImageDesc& desc);
    void set_radius(float radius);
    void set_path(std::string_view path);
    void reload();
    void draw() override;
};

class Text : public ItemBase_t {
private:
    TextDesc desc;
    Texture texture;
    bool texture_dirty{true};
    float texture_scale{};
    Frame& mutable_frame() override;
    ClickCallback& mutable_click_callback(bool left) override;
    Color* color() override;

public:
    explicit Text(const TextDesc& desc);
    [[nodiscard]] const Frame& frame() const override;
    void set_desc(const TextDesc& desc);
    void set_text(std::string_view text);
    void set_font(FontHandle font);
    void set_color(const Color& color);
    void draw() override;
};

Rectangle* add_rectangle(const RectDesc& desc);
Image* add_image(const ImageDesc& desc);
Text* add_text(const TextDesc& desc);
void click(float x, float y, bool left);
void draw();
void clear();

} // namespace Object
