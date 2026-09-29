#include "graphics/text.hpp"
#include "utils/log.hpp"

#include <fontconfig/fontconfig.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <hb.h>
#include <hb-ft.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace std;

namespace {

struct GlyphBitmap {
    int left{};
    int top{};
    unsigned width{};
    unsigned height{};
    vector<uint8_t> pixels;
    bool failed{};

    static GlyphBitmap failure() {
        GlyphBitmap glyph;
        glyph.failed = true;
        return glyph;
    }
};

struct FaceState {
    string path;
    int face_index{};
    FT_Face face{};
    hb_font_t* hb_font{};
};

struct LoadedFont {
    string family;
    unsigned pixel_size{};
    vector<FaceState> faces;
    unordered_map<uint64_t, GlyphBitmap> glyph_cache;
    unordered_set<uint32_t> unsupported_codepoints;

    ~LoadedFont() {
        for (auto& state : faces) {
            if (state.hb_font) hb_font_destroy(state.hb_font);
            if (state.face) FT_Done_Face(state.face);
        }
    }
};

// No Fontconfig object escapes a font lookup. Destroy the result set before
// the configuration because result patterns can reference its font data.
struct FontQuery {
    FcConfig* config{};
    FcPattern* pattern{};
    FcFontSet* matches{};

    FontQuery(const string& family, unsigned pixel_size) {
        config = FcInitLoadConfigAndFonts();
        if (!config) {
            Log::logger(Log::Error, "Failed to initialize Fontconfig");
            return;
        }

        pattern = FcPatternCreate();
        if (!pattern) {
            Log::logger(Log::Error, "Failed to create font pattern");
            return;
        }
        if (!FcPatternAddString(pattern, FC_FAMILY,
                                reinterpret_cast<const FcChar8*>(family.c_str())) ||
            !FcPatternAddDouble(pattern, FC_PIXEL_SIZE, pixel_size)) {
            Log::logger(Log::Error, "Failed to configure font pattern");
            return;
        }
        if (!FcConfigSubstitute(config, pattern, FcMatchPattern)) {
            Log::logger(Log::Error, "Failed to apply font configuration");
            return;
        }
        FcDefaultSubstitute(pattern);

        FcResult match_result{};
        matches = FcFontSort(config, pattern, FcTrue, nullptr, &match_result);
        if (!matches || matches->nfont == 0)
            Log::logger(Log::Error, "No font matched {}", family);
    }

    FontQuery(const FontQuery&) = delete;
    FontQuery& operator=(const FontQuery&) = delete;

    [[nodiscard]] bool has_matches() const {
        return matches && matches->nfont > 0;
    }

    ~FontQuery() {
        if (matches) FcFontSetDestroy(matches);
        if (pattern) FcPatternDestroy(pattern);
        if (config) FcConfigDestroy(config);
    }
};

struct Codepoint {
    uint32_t value{};
};

struct Run {
    unsigned begin{};
    unsigned end{};
    unsigned face_index{};
    hb_script_t script{HB_SCRIPT_UNKNOWN};
};

struct PositionedGlyph {
    unsigned face_index{};
    unsigned glyph_id{};
    bool cacheable{};
    int64_t x_26_6{};
    int64_t y_26_6{};
};

FT_Library ft_library{};
unordered_map<uint64_t, unique_ptr<LoadedFont>> loaded_fonts;
uint64_t next_font_id{1};

bool cacheable_codepoint(uint32_t cp) {
    if (cp >= 0x20 && cp <= 0x7e) return true;

    switch (cp) {
        case U'，': case U'。': case U'、':
        case U'《': case U'》': case U'？': case U'！':
        case U'：': case U'；': case U'“': case U'”':
        case U'‘': case U'’': case U'（': case U'）':
        case U'【': case U'】': case U'—': case U'…':
            return true;
        default:
            return false;
    }
}

vector<Codepoint> decode_utf8(string_view text) {
    vector<Codepoint> result;
    result.reserve(text.size());

    for (size_t i = 0; i < text.size();) {
        auto first = static_cast<uint8_t>(text[i]);
        if (first < 0x80) {
            // This API produces one line. Treat line breaks and tabs as spaces.
            result.push_back({first == '\n' || first == '\r' || first == '\t' ?
                              static_cast<uint32_t>(' ') : first});
            ++i;
            continue;
        }

        unsigned length = first >= 0xf0 && first <= 0xf4 ? 4 :
                          first >= 0xe0 && first <= 0xef ? 3 :
                          first >= 0xc2 && first <= 0xdf ? 2 : 0;
        uint32_t cp = first & (length == 4 ? 0x07u : length == 3 ? 0x0fu : 0x1fu);
        bool valid = length != 0 && i + length <= text.size();
        if (valid) {
            for (unsigned j = 1; j < length; ++j) {
                auto byte = static_cast<uint8_t>(text[i + j]);
                if ((byte & 0xc0) != 0x80) {
                    valid = false;
                    break;
                }
                cp = (cp << 6) | (byte & 0x3f);
            }
            valid = valid && cp >= (length == 2 ? 0x80u : length == 3 ? 0x800u : 0x10000u)
                    && cp <= 0x10ffff && !(cp >= 0xd800 && cp <= 0xdfff);
        }

        if (valid) {
            result.push_back({cp});
            i += length;
        } else {
            result.push_back({0xfffd});
            ++i;
        }
    }

    return result;
}

bool extends_cluster(uint32_t cp, uint32_t previous) {
    return hb_unicode_combining_class(hb_unicode_funcs_get_default(), cp) != 0 ||
           (cp >= 0xfe00 && cp <= 0xfe0f) ||
           (cp >= 0xe0100 && cp <= 0xe01ef) ||
           cp == 0x200d || previous == 0x200d;
}

bool ignorable_for_coverage(uint32_t cp) {
    return cp == 0x200d || (cp >= 0xfe00 && cp <= 0xfe0f) ||
           (cp >= 0xe0100 && cp <= 0xe01ef);
}

unsigned choose_face(const LoadedFont& font, const vector<Codepoint>& cps,
                     unsigned begin, unsigned end) {
    auto cluster = ranges::subrange(cps.begin() + begin, cps.begin() + end);
    for (unsigned i = 0; i < font.faces.size(); ++i) {
        if (ranges::all_of(cluster, [&](const Codepoint& cp) {
                return ignorable_for_coverage(cp.value) ||
                       FT_Get_Char_Index(font.faces[i].face, cp.value) != 0;
            })) return i;
    }
    return static_cast<unsigned>(font.faces.size());
}

optional<FaceState> font_source(FcPattern* pattern) {
    FcChar8* path{};
    int index = 0;
    if (FcPatternGetString(pattern, FC_FILE, 0, &path) != FcResultMatch || !path) {
        Log::logger(Log::Error, "Matched font has no file");
        return nullopt;
    }
    FcPatternGetInteger(pattern, FC_INDEX, 0, &index);
    return FaceState{reinterpret_cast<const char*>(path), index};
}

FaceState* get_face(LoadedFont& font, unsigned index) {
    auto& state = font.faces[index];
    if (state.face && state.hb_font) return &state;

    if (FT_New_Face(ft_library, state.path.c_str(), state.face_index, &state.face)) {
        Log::logger(Log::Error, R"@(Failed to load font file, path="{}")@", state.path);
        return nullptr;
    }
    if (FT_Set_Pixel_Sizes(state.face, 0, font.pixel_size)) {
        Log::logger(Log::Error, R"@(Failed to set font size, path="{}")@", state.path);
        FT_Done_Face(state.face);
        state.face = nullptr;
        return nullptr;
    }

    state.hb_font = hb_ft_font_create_referenced(state.face);
    if (!state.hb_font) {
        Log::logger(Log::Error, "Failed to initialize HarfBuzz font");
        FT_Done_Face(state.face);
        state.face = nullptr;
        return nullptr;
    }
    return &state;
}

bool pattern_covers(FcPattern* pattern, const vector<Codepoint>& cps,
                    unsigned begin, unsigned end) {
    FcCharSet* charset{};
    if (FcPatternGetCharSet(pattern, FC_CHARSET, 0, &charset) != FcResultMatch)
        return false;
    auto cluster = ranges::subrange(cps.begin() + begin, cps.begin() + end);
    return ranges::all_of(cluster, [&](const Codepoint& cp) {
        return ignorable_for_coverage(cp.value) || FcCharSetHasChar(charset, cp.value);
    });
}

void resolve_missing_faces(LoadedFont& font, const vector<Codepoint>& cps) {
    struct MissingCluster { unsigned begin; unsigned end; };
    vector<MissingCluster> missing;

    for (unsigned begin = 0; begin < cps.size();) {
        unsigned end = begin + 1;
        while (end < cps.size() && extends_cluster(cps[end].value, cps[end - 1].value))
            ++end;

        if (choose_face(font, cps, begin, end) == font.faces.size()) {
            auto cluster = ranges::subrange(cps.begin() + begin, cps.begin() + end);
            bool already_unsupported = ranges::any_of(cluster, [&](const Codepoint& cp) {
                return font.unsupported_codepoints.contains(cp.value);
            });
            if (!already_unsupported) missing.push_back({begin, end});
        }
        begin = end;
    }
    if (missing.empty()) return;

    // One short-lived Fontconfig lookup handles every missing cluster in this
    // line. Only copied paths and face indices remain after this function.
    FontQuery query(font.family, font.pixel_size);
    if (!query.matches) return;
    for (const auto& cluster : missing) {
        if (choose_face(font, cps, cluster.begin, cluster.end) != font.faces.size())
            continue;

        bool resolved = false;
        bool failed_candidate = false;
        for (int i = 0; i < query.matches->nfont; ++i) {
            FcPattern* pattern = query.matches->fonts[i];
            if (!pattern_covers(pattern, cps, cluster.begin, cluster.end)) continue;

            auto source = font_source(pattern);
            if (!source) {
                failed_candidate = true;
                continue;
            }
            bool loaded = ranges::any_of(font.faces, [&](const FaceState& face) {
                return face.path == source->path && face.face_index == source->face_index;
            });
            if (loaded) continue;

            font.faces.push_back(std::move(*source));
            unsigned index = static_cast<unsigned>(font.faces.size() - 1);
            if (!get_face(font, index)) {
                font.faces.pop_back();
                failed_candidate = true;
                continue;
            }
            if (choose_face(font, cps, cluster.begin, cluster.end) == index) {
                resolved = true;
                break;
            }
        }

        if (!resolved && !failed_candidate) {
            for (unsigned i = cluster.begin; i < cluster.end; ++i)
                if (!ignorable_for_coverage(cps[i].value))
                    font.unsupported_codepoints.insert(cps[i].value);
        }
    }
}

vector<Run> make_runs(const LoadedFont& font, const vector<Codepoint>& cps) {
    vector<Run> runs;
    for (unsigned begin = 0; begin < cps.size();) {
        unsigned end = begin + 1;
        while (end < cps.size() && extends_cluster(cps[end].value, cps[end - 1].value))
            ++end;

        unsigned face_index = choose_face(font, cps, begin, end);
        if (face_index == font.faces.size()) face_index = 0; // .notdef glyph
        hb_script_t script = HB_SCRIPT_COMMON;
        for (unsigned j = begin; j < end; ++j) {
            auto candidate = hb_unicode_script(hb_unicode_funcs_get_default(), cps[j].value);
            if (candidate != HB_SCRIPT_COMMON && candidate != HB_SCRIPT_INHERITED) {
                script = candidate;
                break;
            }
        }
        if (script == HB_SCRIPT_COMMON && !runs.empty()) script = runs.back().script;

        if (!runs.empty() && runs.back().face_index == face_index &&
            (runs.back().script == script || script == HB_SCRIPT_COMMON)) {
            runs.back().end = end;
        } else {
            runs.push_back({begin, end, face_index, script});
        }
        begin = end;
    }
    return runs;
}

uint64_t glyph_key(unsigned face_index, unsigned glyph_id) {
    return (static_cast<uint64_t>(face_index) << 32) | glyph_id;
}

GlyphBitmap rasterize(LoadedFont& font, unsigned face_index, unsigned glyph_id) {
    FT_Face face = font.faces[face_index].face;
    if (FT_Load_Glyph(face, glyph_id, FT_LOAD_RENDER | FT_LOAD_COLOR)) {
        Log::logger(Log::Error, "Failed to rasterize glyph {}", glyph_id);
        return GlyphBitmap::failure();
    }

    const FT_Bitmap& bitmap = face->glyph->bitmap;
    GlyphBitmap result;
    result.left = face->glyph->bitmap_left;
    result.top = face->glyph->bitmap_top;
    result.width = bitmap.width;
    result.height = bitmap.rows;
    if (result.height && result.width > numeric_limits<size_t>::max() / result.height) {
        Log::logger(Log::Error, "Glyph bitmap dimensions are too large");
        return GlyphBitmap::failure();
    }
    result.pixels.resize(static_cast<size_t>(result.width) * result.height);

    if (!result.width || !result.height) return result;
    if (!bitmap.buffer) {
        Log::logger(Log::Error, "Glyph bitmap has no pixels");
        return GlyphBitmap::failure();
    }

    const auto pitch = static_cast<size_t>(abs(bitmap.pitch));
    for (unsigned y = 0; y < result.height; ++y) {
        const unsigned source_y = bitmap.pitch < 0 ? result.height - 1 - y : y;
        const auto* row = bitmap.buffer + static_cast<size_t>(source_y) * pitch;
        for (unsigned x = 0; x < result.width; ++x) {
            uint8_t alpha{};
            switch (bitmap.pixel_mode) {
                case FT_PIXEL_MODE_GRAY:
                    alpha = bitmap.num_grays == 256 ? row[x] :
                            bitmap.num_grays > 1 ? static_cast<uint8_t>(
                                row[x] * 255u / (bitmap.num_grays - 1)) : 0;
                    break;
                case FT_PIXEL_MODE_MONO:
                    alpha = (row[x / 8] & (0x80u >> (x % 8))) ? 255 : 0;
                    break;
                case FT_PIXEL_MODE_BGRA:
                    alpha = row[x * 4 + 3];
                    break;
                default:
                    Log::logger(Log::Error, "Unsupported glyph pixel mode {}", bitmap.pixel_mode);
                    return GlyphBitmap::failure();
            }
            result.pixels[static_cast<size_t>(y) * result.width + x] = alpha;
        }
    }
    return result;
}

const GlyphBitmap& get_glyph(LoadedFont& font, const PositionedGlyph& placement,
                             unordered_map<uint64_t, GlyphBitmap>& scratch) {
    auto key = glyph_key(placement.face_index, placement.glyph_id);
    if (placement.cacheable) {
        auto cached = font.glyph_cache.find(key);
        if (cached != font.glyph_cache.end()) return cached->second;
    }

    auto it = scratch.find(key);
    if (it == scratch.end()) {
        auto glyph = rasterize(font, placement.face_index, placement.glyph_id);
        if (placement.cacheable && !glyph.failed)
            return font.glyph_cache.emplace(key, std::move(glyph)).first->second;
        it = scratch.emplace(key, std::move(glyph)).first;
    }
    return it->second;
}

LoadedFont* require_font(Text::FontHandle handle) {
    auto it = loaded_fonts.find(handle.id);
    if (it == loaded_fonts.end()) {
        Log::logger(Log::Error, "Invalid text font handle {}", handle.id);
        return nullptr;
    }
    return it->second.get();
}

} // namespace

void Text::init() {
    if (ft_library) {
        Log::logger(Log::Warning, "Text is already initialized");
        return;
    }
    FT_Library library{};
    if (FT_Init_FreeType(&library)) Log::fatal("Failed to initialize FreeType");
    ft_library = library;
}

Text::FontHandle Text::load_font(string_view family, unsigned pixel_size) {
    if (!ft_library) {
        Log::logger(Log::Error, "Text is not initialized");
        return {};
    }
    if (family.empty() || !pixel_size) {
        Log::logger(Log::Error, "Font family and pixel size are required");
        return {};
    }

    auto font = make_unique<LoadedFont>();
    font->family = family;
    font->pixel_size = pixel_size;
    {
        FontQuery query(font->family, pixel_size);
        if (!query.has_matches()) return {};
        for (int i = 0; i < query.matches->nfont; ++i) {
            auto source = font_source(query.matches->fonts[i]);
            if (!source) continue;
            font->faces.push_back(std::move(*source));
            if (get_face(*font, 0)) break;
            font->faces.clear();
        }
    }
    if (font->faces.empty()) {
        Log::logger(Log::Error, "Failed to open a font for {}", family);
        return {};
    }

    if (!next_font_id) {
        Log::logger(Log::Error, "Text font handle limit reached");
        return {};
    }
    FontHandle handle{next_font_id++};
    loaded_fonts.emplace(handle.id, std::move(font));
    return handle;
}

void Text::unload_font(FontHandle font) {
    if (loaded_fonts.erase(font.id) != 1)
        Log::logger(Log::Warning, "Invalid text font handle {}", font.id);
}

Text::Bitmap Text::draw(string_view text, FontHandle handle) {
    LoadedFont* font = require_font(handle);
    Bitmap bitmap;
    if (!font || text.empty()) return bitmap;

    auto cps = decode_utf8(text);
    if (cps.size() > static_cast<size_t>(numeric_limits<int>::max())) {
        Log::logger(Log::Error, "Text is too long to shape");
        return {};
    }
    resolve_missing_faces(*font, cps);
    auto runs = make_runs(*font, cps);
    vector<uint32_t> values;
    values.reserve(cps.size());
    for (const auto& cp : cps) values.push_back(cp.value);

    vector<PositionedGlyph> placements;
    int64_t pen_x = 0;
    int64_t pen_y = 0;
    for (const auto& run : runs) {
        hb_buffer_t* buffer = hb_buffer_create();
        if (!buffer) {
            Log::logger(Log::Error, "Failed to create HarfBuzz buffer");
            return {};
        }
        hb_buffer_add_codepoints(buffer, values.data(), static_cast<int>(values.size()),
                                 run.begin, run.end - run.begin);
        hb_buffer_set_direction(buffer, HB_DIRECTION_LTR);
        if (run.script != HB_SCRIPT_UNKNOWN && run.script != HB_SCRIPT_COMMON)
            hb_buffer_set_script(buffer, run.script);
        hb_buffer_guess_segment_properties(buffer);
        hb_shape(font->faces[run.face_index].hb_font, buffer, nullptr, 0);

        unsigned count{};
        const auto* infos = hb_buffer_get_glyph_infos(buffer, &count);
        const auto* positions = hb_buffer_get_glyph_positions(buffer, &count);
        vector<unsigned> clusters;
        clusters.reserve(count);
        for (unsigned i = 0; i < count; ++i) clusters.push_back(infos[i].cluster);
        sort(clusters.begin(), clusters.end());
        clusters.erase(unique(clusters.begin(), clusters.end()), clusters.end());

        for (unsigned i = 0; i < count; ++i) {
            unsigned cluster_begin = infos[i].cluster;
            auto next = upper_bound(clusters.begin(), clusters.end(), cluster_begin);
            unsigned cluster_end = next == clusters.end() ? run.end : *next;
            bool cacheable = cluster_begin < cluster_end && cluster_end <= values.size();
            for (unsigned j = cluster_begin; cacheable && j < cluster_end; ++j)
                cacheable = cacheable_codepoint(values[j]);

            placements.push_back({run.face_index, infos[i].codepoint, cacheable,
                                  pen_x + positions[i].x_offset,
                                  pen_y + positions[i].y_offset});
            pen_x += positions[i].x_advance;
            pen_y += positions[i].y_advance;
        }
        hb_buffer_destroy(buffer);
    }

    bitmap.advance_x = static_cast<float>(pen_x) / 64.0f;
    unordered_map<uint64_t, GlyphBitmap> scratch;
    int64_t min_x = min<int64_t>(0, static_cast<int64_t>(floor(bitmap.advance_x)));
    int64_t max_x = max<int64_t>(0, static_cast<int64_t>(ceil(bitmap.advance_x)));
    int64_t min_y = 0;
    int64_t max_y = 0;
    bool has_pixels = false;

    for (const auto& placement : placements) {
        const auto& glyph = get_glyph(*font, placement, scratch);
        if (glyph.pixels.empty()) continue;
        const auto x = static_cast<int64_t>(lround(
            static_cast<double>(placement.x_26_6) / 64.0)) + glyph.left;
        const auto y = -static_cast<int64_t>(lround(
            static_cast<double>(placement.y_26_6) / 64.0)) - glyph.top;
        min_x = min(min_x, x);
        max_x = max(max_x, x + glyph.width);
        if (!has_pixels) {
            min_y = y;
            max_y = y + glyph.height;
            has_pixels = true;
        } else {
            min_y = min(min_y, y);
            max_y = max(max_y, y + glyph.height);
        }
    }

    if (max_x - min_x > numeric_limits<uint32_t>::max() ||
        max_y - min_y > numeric_limits<uint32_t>::max() ||
        min_x < numeric_limits<int32_t>::min() ||
        min_y < numeric_limits<int32_t>::min()) {
        Log::logger(Log::Error, "Text bitmap dimensions are too large");
        return {};
    }

    bitmap.width = static_cast<uint32_t>(max_x - min_x);
    bitmap.height = static_cast<uint32_t>(max_y - min_y);
    bitmap.stride = bitmap.width;
    bitmap.offset_x = static_cast<int32_t>(min_x);
    bitmap.offset_y = static_cast<int32_t>(min_y);
    if (bitmap.height && bitmap.width > numeric_limits<size_t>::max() / bitmap.height) {
        Log::logger(Log::Error, "Text bitmap dimensions are too large");
        return {};
    }
    bitmap.pixels.resize(static_cast<size_t>(bitmap.width) * bitmap.height);

    for (const auto& placement : placements) {
        const auto& glyph = get_glyph(*font, placement, scratch);
        if (glyph.pixels.empty()) continue;
        const auto x = static_cast<int64_t>(lround(
            static_cast<double>(placement.x_26_6) / 64.0)) + glyph.left - min_x;
        const auto y = -static_cast<int64_t>(lround(
            static_cast<double>(placement.y_26_6) / 64.0)) - glyph.top - min_y;
        for (unsigned row = 0; row < glyph.height; ++row) {
            for (unsigned col = 0; col < glyph.width; ++col) {
                auto source = glyph.pixels[static_cast<size_t>(row) * glyph.width + col];
                auto& target = bitmap.pixels[static_cast<size_t>(y + row) * bitmap.stride + x + col];
                target = static_cast<uint8_t>(source +
                    (static_cast<unsigned>(target) * (255u - source) + 127u) / 255u);
            }
        }
    }
    return bitmap;
}

void Text::shutdown() {
    loaded_fonts.clear();
    if (ft_library) {
        FT_Done_FreeType(ft_library);
        ft_library = nullptr;
    }
}
