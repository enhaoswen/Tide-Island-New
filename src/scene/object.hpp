#pragma once

#include "utils/struct.hpp"
#include "graphics/texture.hpp"

#include <source_location>
#include <string_view>
#include <vector>

namespace Object {

class ItemBase {
private:
    std::vector<Animation> animations;
    void (*left_callback)(){};
    void (*right_callback)(){};

protected:
    virtual Frame& mutable_frame() = 0;
    virtual float* radius();
    virtual Color* color();
    void set_click_callbacks(void (*left)(), void (*right)());

public:
    ItemBase() = default;
    virtual ~ItemBase() = default;
    ItemBase(const ItemBase&) = delete;
    ItemBase& operator=(const ItemBase&) = delete;

    virtual const Frame& frame() const = 0;
    void set_frame(const Frame& frame);
    void add_animation(Animation animation, std::source_location location = std::source_location::current());
    void update();
    virtual void draw() = 0;
    virtual bool click(float x, float y, bool left);
};

class Rectangle : public ItemBase {
private:
    RectDesc desc;
    Frame& mutable_frame() override;
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

class Image : public ItemBase {
private:
    ImageDesc desc;
    Texture texture;
    bool texture_dirty{true};
    Frame& mutable_frame() override;
    float* radius() override;

public:
    explicit Image(const ImageDesc& desc);
    const Frame& frame() const override;
    void set_desc(const ImageDesc& desc);
    void set_radius(float radius);
    void set_path(std::string_view path);
    // Refresh a file whose contents changed at the same path.
    void reload();
    void draw() override;
};

class Text : public ItemBase {
private:
    TextDesc desc;
    Texture texture;
    bool texture_dirty{true};
    double texture_scale{};
    Frame& mutable_frame() override;
    Color* color() override;

public:
    explicit Text(const TextDesc& desc);
    const Frame& frame() const override;
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
