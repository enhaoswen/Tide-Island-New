# Tide Island — C++ Code Style Guide

> **Scope.** This guide applies to all new and modified C++ code in Tide Island.
> Existing code is not yet migrated. Do not reformat whole files you are not otherwise changing.
>
> **Tooling.** `.clang-format` handles formatting and `.clang-tidy` handles static analysis.
> This document covers what the tools cannot enforce and explains the reasoning behind what they do enforce.

---

## Table of Contents

1. [Naming](#1-naming)
2. [Formatting](#2-formatting)
3. [Headers & Includes](#3-headers--includes)
4. [Language Usage](#4-language-usage)
5. [Comments & Documentation](#5-comments--documentation)
6. [Logging & Debugging](#6-logging--debugging)
7. [Build & Tooling](#7-build--tooling)

---

## 1. Naming

### 1.1 At a Glance

| Entity                                    | Style               | Example                         |
|-------------------------------------------|---------------------|---------------------------------|
| Class / struct / union / enum / concept   | `PascalCase`        | `IslandState`, `GpuRenderer`    |
| Type alias                                | `PascalCase`        | `using Callback = ...;`         |
| Function / method                         | `snake_case`        | `set_anchor_top()`              |
| Local variable / parameter                | `snake_case`        | `island_width`                  |
| Member variable (all members)             | `snake_case_`       | `width_`, `is_running_`         |
| Constant (`constexpr`, global/static const) | `k_snake_case`    | `k_priority_hover`              |
| Enumerator                                | `PascalCase`        | `Button::Left`                  |
| Namespace                                 | `PascalCase`        | `Island`, `StateManager`        |
| Template parameter                        | `T` or `PascalCase` | `T`, `Callback`                 |
| Macro                                     | `ALL_CAPS`          | `TIDE_RESOURCE_DIR`             |
| File                                      | `snake_case`        | `state_manager.hpp`             |

### 1.2 Rules

**Acronyms are treated as ordinary words.**

```cpp
class GpuRenderer;      // not GPURenderer
string get_url();       // not get_URL
```

**Booleans carry a predicate prefix** (`is_`, `has_`, `should_`, `can_`, ...).

```cpp
bool is_running_{true};
bool has_focus() const;
```

**Accessors use `get_` / `set_`.**

```cpp
const IslandState& get_island();
void set_radius(float radius);
```

**Every member variable ends in `_`**, including public fields of a plain struct.

```cpp
struct IslandState {
	uint16_t width_{};
	uint16_t height_{};
	bool     need_redraw_{true};
};
```

**Template parameters** use a single letter when the meaning is obvious, and a descriptive name otherwise.

```cpp
template<typename T> T clamp(T v, T lo, T hi);
template<typename Callback> void on_tick(Callback&& cb);
```

**Enums are always scoped.** The default underlying type is `uint8_t`.

```cpp
enum class Button : uint8_t {
	Left,
	Right,
	Middle,
};
```

---

## 2. Formatting

Run `clang-format` on what you change. The settings that matter most:

| Topic               | Rule                                                          |
|---------------------|---------------------------------------------------------------|
| Indentation         | Tabs, displayed as 4 columns                                  |
| Line length         | 120 columns                                                   |
| Braces              | K&R (attached); `} else {` on one line                        |
| Pointers / refs     | `Type* ptr`, `Type& ref`                                      |
| `const` placement   | West const: `const T&`                                        |
| Parentheses         | No space before them: `if(x)`, `for(...)`, `f(x)`             |
| Templates           | No inner spaces: `vector<int>`                                |
| Binary operators    | Surrounded by spaces: `a + b`                                 |
| Blank lines         | One between function definitions                              |
| End of file         | Exactly one trailing newline                                  |

### 2.1 Control Flow

A single-statement `if` may drop its braces, but the statement goes on its own line.

```cpp
if(!ready)
	return;

if(x > 0) {
	grow();
} else {
	shrink();
}
```

`case` labels are indented one level inside `switch`.

```cpp
switch(button) {
	case Button::Left:
		select();
		break;
	case Button::Right:
		open_menu();
		break;
}
```

### 2.2 Short Functions & Lambdas

One-line function bodies are allowed **only inside class definitions**. Short lambdas may always be one line.

```cpp
struct Size {
	int width() const { return w_; }
	int w_{};
};

auto by_priority = [](const Item& a, const Item& b) { return a.priority_ < b.priority_; };
```

### 2.3 Classes & Namespaces

Access specifiers sit at column 0. Namespace contents are not indented.

```cpp
namespace Island {

class Animator {
public:
	void start();

private:
	float progress_{};
};

}
```

### 2.4 Alignment

Consecutive declarations and assignments are aligned into columns.

```cpp
constexpr float k_priority_normal     = 0.f;
constexpr float k_priority_hover      = 1.f;
constexpr float k_priority_transition = 2.f;
```

### 2.5 Wrapping

When a parameter or argument list has to wrap, put one item per line.
Constructor initializer lists break after the colon, with trailing commas.

```cpp
void draw_text(string_view text,
	float x,
	float y,
	Color color);

Renderer::Renderer(int width, int height) :
	width_{width},
	height_{height} {
}
```

---

## 3. Headers & Includes

### 3.1 Header Guards

Use `#pragma once`. Do not use `#ifndef` guards.

### 3.2 Include Order

clang-format groups and sorts includes automatically, alphabetically within each group, with a blank line between groups.

```cpp
#include "app/island.hpp"         // 1. The header this file implements

#include <algorithm>              // 2. Standard library
#include <cstdint>

#include <wayland-client.h>       // 3. Third-party

#include "graphics/renderer.hpp"  // 4. Project headers, path relative to src/
#include "utils/log.hpp"
```

### 3.3 Principles

- **Include what you use.** Every file includes the headers it directly needs and never relies on transitive includes.
- **Prefer forward declarations** over includes wherever possible.
- **Headers live next to their sources** in the same directory.
- Implementation may live in headers when it makes sense (templates, `constexpr`, small inline functions).
- Any reasonable extension is fine: `.hpp`, `.cpp`, `.h`, `.ixx`.

---

## 4. Language Usage

Tide Island targets **C++23**, and modern features are encouraged.

### 4.1 Namespaces

| Context              | Rule                                            |
|----------------------|-------------------------------------------------|
| `.cpp` files         | Always write `using namespace std;`             |
| Header files         | `using namespace` is **forbidden**              |
| Fixed-width integers | Write `uint16_t`, not `std::uint16_t`           |
| File-local symbols   | Put them in an anonymous namespace, not `static` |

Global state at namespace scope is allowed. Prefer a namespace with free functions over a class when no invariant needs protecting. This is a preference, not a requirement.

### 4.2 Variables & Constants

- **Brace-initialize everything:** `int count{}`, `Color color{255, 0, 0}`.
- **`auto`** only when the type is obvious or very long (iterators, `make_unique`).
- **Be const-correct:** `const` wherever possible, including locals and by-value parameters.
- **`constexpr` wherever possible.** `consteval` and `constinit` are encouraged.

| Constant kind              | Declaration          |
|----------------------------|----------------------|
| Global constant in header  | `inline constexpr`   |
| Translation-unit-local     | `constexpr`          |
| Class static constant      | `static constexpr`   |

- Use `size_t` for sizes and indices. Fixed-width integer types are encouraged.
- Float literals are written `0.f`.

### 4.3 Types

- Prefer `struct`. Reach for `class` when there are invariants to protect.
- Member order: `public` → `protected` → `private`.
- Prefer default member initializers to constructor initializer lists.
- Use `= default` whenever possible.
- Overrides are marked `override` and drop `virtual`.
- Multiple inheritance and `final` are allowed.

### 4.4 Functions

- **Parameter passing:** pass cheap types by value, larger types by `const&`, and sink parameters by value followed by `std::move`.
- **Prefer returning results** (`struct`, `optional`, `expected`) over output parameters.
- Default arguments, function overloading and operator overloading are all allowed.
- **No trailing return types** unless the language requires one.
- `[[nodiscard]]` on functions whose return value must not be ignored.
- `noexcept` only on move operations and `swap`.
- Attributes like `[[maybe_unused]]` and `[[likely]]` are encouraged.
- **Prefer guard clauses** over deep nesting.

```cpp
void update(Island& island) {
	if(!island.is_visible_)
		return;
	if(!island.need_redraw_)
		return;

	redraw(island);
}
```

### 4.5 Casts & Ownership

- C++ casts only (`static_cast`, `reinterpret_cast`, ...). **C-style casts are forbidden.**
- Nullable means pointer, non-nullable means reference.

### 4.6 Error Handling

Report errors through `std::optional` / `std::expected` plus a log line, or log and then fail loudly. `assert` may be used freely.

### 4.7 Modern C++

All of the following are encouraged:

`std::expected` · `std::print` / `std::format` · ranges & views · concepts · modules · deducing `this` · `std::span` · `std::mdspan` · coroutines · designated initializers

- Constrain templates with **concepts**.
- Prefer **algorithms and ranges** over hand-written loops.
- Take string parameters as **`std::string_view`**. Format with **`std::format`**.

```cpp
template<std::integral T>
[[nodiscard]] constexpr T align_up(T value, T alignment) {
	return (value + alignment - 1) / alignment * alignment;
}
```

---

## 5. Comments & Documentation

- **Implementation comments only for genuinely hard logic.** Code should explain itself.
- **Public header APIs get Doxygen.** A one-line `/// @brief` is enough.
- Comments are written in **English**. Both `//` and `/* */` are fine.
- **No** section banners (`// ======`), and **no** closing comments such as `} // namespace Island`.
- **No `TODO`s.** Track pending work in issues.
- Commented-out code may stay.

```cpp
/// @brief Sets the island corner radius in pixels.
void set_radius(float radius);
```

---

## 6. Logging & Debugging

- All output goes through **`Log::logger`**. Do not call `printf` or `std::cout` directly.
- Developers choose the log level.
- Wrap debug-only code in `#ifndef NDEBUG`.

---

## 7. Build & Tooling

| Tool          | Configuration                                                    |
|---------------|------------------------------------------------------------------|
| Compiler      | `-Wall -Wextra -Wpedantic -Werror` on every project target       |
| clang-format  | `.clang-format`, the source of truth for layout                  |
| clang-tidy    | `.clang-tidy`, warnings only (`WarningsAsErrors` disabled)       |

To format only your changes:

```sh
git clang-format
```
