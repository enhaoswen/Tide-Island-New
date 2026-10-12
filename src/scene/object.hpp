#pragma once

#include "utils/struct.hpp"

#include <source_location>
#include <string_view>
#include <vector>

namespace Object {

class ItemBase {
private:
    std::vector<Animation> animations;

protected:
    virtual Frame& mutable_frame() = 0;
    virtual float* radius();
    virtual Color* color();

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
    virtual bool click(float x, float y, bool left) = 0;
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
    bool click(float x, float y, bool left) override;
};

class Image : public ItemBase {
private:
    ImageDesc desc;
    Frame& mutable_frame() override;
    float* radius() override;

public:
    explicit Image(const ImageDesc& desc);
    const Frame& frame() const override;
    void set_desc(const ImageDesc& desc);
    void set_radius(float radius);
    void set_path(std::string_view path);
    void draw() override;
    bool click(float x, float y, bool left) override;
};

class Text : public ItemBase {
private:
    TextDesc desc;
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
    bool click(float x, float y, bool left) override;
};

Rectangle* add_rectangle(const RectDesc& desc);
Image* add_image(const ImageDesc& desc);
Text* add_text(const TextDesc& desc);
void click(float x, float y, bool left);
void draw();
void clear();

} // namespace Object
