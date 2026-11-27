#include "graphics/text.hpp"

#include <fontconfig/fontconfig.h>
#include <ft2build.h>
#include <hb-ft.h>
#include <hb.h>

#include "utils/log.hpp"
#include FT_FREETYPE_H
#include FT_OUTLINE_H

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
	int             left_{};
	int             top_{};
	unsigned        width_{};
	unsigned        height_{};
	vector<uint8_t> pixels_{};
	bool            is_failed_{};

	static GlyphBitmap failure() {
		GlyphBitmap glyph{};
		glyph.is_failed_ = true;
		return glyph;
	}
};

struct FaceState {
	string     path_{};
	int        face_index_{};
	FT_Face    face_{};
	hb_font_t* hb_font_{};
	unsigned   raster_size_{};
};

struct LoadedFont {
	string       family_{};
	Text::Weight weight_{};
	unsigned     pixel_size_{};
	// 26.6 fixed point: whole-pixel sizes make the same text a different size at each zoom.
	unsigned                             raster_size_{};
	vector<FaceState>                    faces_{};
	unordered_map<uint64_t, GlyphBitmap> glyph_cache_{};
	unordered_set<uint32_t>              unsupported_codepoints_{};

	~LoadedFont() {
		for(const auto& state : faces_) {
			if(state.hb_font_)
				hb_font_destroy(state.hb_font_);
			if(state.face_)
				FT_Done_Face(state.face_);
		}
	}
};

int fc_weight(Text::Weight weight) {
	switch(weight) {
		case Text::Weight::Regular:
			return FC_WEIGHT_REGULAR;
		case Text::Weight::Medium:
			return FC_WEIGHT_MEDIUM;
		case Text::Weight::DemiBold:
			return FC_WEIGHT_DEMIBOLD;
		case Text::Weight::Bold:
			return FC_WEIGHT_BOLD;
	}
	return FC_WEIGHT_REGULAR;
}

// No Fontconfig object escapes a font lookup. Destroy the result set before
// the configuration because result patterns can reference its font data.
struct FontQuery {
	FcConfig*  config_{};
	FcPattern* pattern_{};
	FcFontSet* matches_{};

	FontQuery(const string& family, unsigned pixel_size, Text::Weight weight) {
		config_ = FcInitLoadConfigAndFonts();
		if(!config_) {
			Log::logger(Log::LogLevel::Error, "Failed to initialize Fontconfig");
			return;
		}

		pattern_ = FcPatternCreate();
		if(!pattern_) {
			Log::logger(Log::LogLevel::Error, "Failed to create font pattern");
			return;
		}
		if(!FcPatternAddString(pattern_, FC_FAMILY, reinterpret_cast<const FcChar8*>(family.c_str())) ||
			!FcPatternAddDouble(pattern_, FC_PIXEL_SIZE, pixel_size) ||
			!FcPatternAddInteger(pattern_, FC_WEIGHT, fc_weight(weight))) {
			Log::logger(Log::LogLevel::Error, "Failed to configure font pattern");
			return;
		}
		if(!FcConfigSubstitute(config_, pattern_, FcMatchPattern)) {
			Log::logger(Log::LogLevel::Error, "Failed to apply font configuration");
			return;
		}
		FcDefaultSubstitute(pattern_);

		FcResult match_result{};
		matches_ = FcFontSort(config_, pattern_, FcTrue, nullptr, &match_result);
		if(!matches_ || matches_->nfont == 0)
			Log::logger(Log::LogLevel::Error, "No font matched {}", family);
	}

	FontQuery(const FontQuery&) = delete;

	FontQuery& operator=(const FontQuery&) = delete;

	[[nodiscard]] bool has_matches() const { return matches_ && matches_->nfont > 0; }

	~FontQuery() {
		if(matches_)
			FcFontSetDestroy(matches_);
		if(pattern_)
			FcPatternDestroy(pattern_);
		if(config_)
			FcConfigDestroy(config_);
	}
};

struct Codepoint {
	uint32_t value_{};
};

struct Run {
	unsigned    begin_{};
	unsigned    end_{};
	unsigned    face_index_{};
	hb_script_t script_{HB_SCRIPT_UNKNOWN};
};

struct PositionedGlyph {
	unsigned face_index_{};
	unsigned glyph_id_{};
	bool     is_cacheable_{};
	int64_t  x_26_6_{};
	int64_t  y_26_6_{};
};

FT_Library                                      ft_library{};
unordered_map<uint64_t, unique_ptr<LoadedFont>> loaded_fonts{};
uint64_t                                        next_font_id{1};

bool is_cacheable_codepoint(uint32_t cp) {
	if(cp >= 0x20 && cp <= 0x7e)
		return true;

	switch(cp) {
		case U'，':
		case U'。':
		case U'、':
		case U'《':
		case U'》':
		case U'？':
		case U'！':
		case U'：':
		case U'；':
		case U'“':
		case U'”':
		case U'‘':
		case U'’':
		case U'（':
		case U'）':
		case U'【':
		case U'】':
		case U'—':
		case U'…':
			return true;
		default:
			return false;
	}
}

vector<Codepoint> decode_utf8(string_view text) {
	vector<Codepoint> result{};
	result.reserve(text.size());

	for(size_t i{}; i < text.size();) {
		const auto first{static_cast<uint8_t>(text[i])};
		if(first < 0x80) {
			// This API produces one line. Treat line breaks and tabs as spaces.
			result.push_back({first == '\n' || first == '\r' || first == '\t' ? static_cast<uint32_t>(' ') : first});
			++i;
			continue;
		}

		const unsigned length = first >= 0xf0 && first <= 0xf4 ? 4
			: first >= 0xe0 && first <= 0xef                   ? 3
			: first >= 0xc2 && first <= 0xdf                   ? 2
															   : 0;
		uint32_t       cp{first & (length == 4 ? 0x07u : length == 3 ? 0x0fu : 0x1fu)};
		bool           is_valid{length != 0 && i + length <= text.size()};
		if(is_valid) {
			for(unsigned j{1}; j < length; ++j) {
				const auto byte{static_cast<uint8_t>(text[i + j])};
				if((byte & 0xc0) != 0x80) {
					is_valid = false;
					break;
				}
				cp = (cp << 6) | (byte & 0x3f);
			}
			is_valid = is_valid &&
				cp >= (length == 2          ? 0x80u
							  : length == 3 ? 0x800u
											: 0x10000u) &&
				cp <= 0x10ffff && (cp < 0xd800 || cp > 0xdfff);
		}

		if(is_valid) {
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
	return hb_unicode_combining_class(hb_unicode_funcs_get_default(), cp) != 0 || (cp >= 0xfe00 && cp <= 0xfe0f) ||
		(cp >= 0xe0100 && cp <= 0xe01ef) || cp == 0x200d || previous == 0x200d;
}

bool is_ignorable_for_coverage(uint32_t cp) {
	return cp == 0x200d || (cp >= 0xfe00 && cp <= 0xfe0f) || (cp >= 0xe0100 && cp <= 0xe01ef);
}

unsigned choose_face(const LoadedFont& font, const vector<Codepoint>& cps, unsigned begin, unsigned end) {
	const auto cluster{ranges::subrange(cps.begin() + begin, cps.begin() + end)};
	for(unsigned i{}; i < font.faces_.size(); ++i) {
		if(ranges::all_of(cluster, [&](const Codepoint& cp) {
			   return is_ignorable_for_coverage(cp.value_) || FT_Get_Char_Index(font.faces_[i].face_, cp.value_) != 0;
		   }))
			return i;
	}
	return static_cast<unsigned>(font.faces_.size());
}

optional<FaceState> font_source(FcPattern* pattern) {
	FcChar8* path{};
	int      index{};
	if(FcPatternGetString(pattern, FC_FILE, 0, &path) != FcResultMatch || !path) {
		Log::logger(Log::LogLevel::Error, "Matched font has no file");
		return nullopt;
	}
	FcPatternGetInteger(pattern, FC_INDEX, 0, &index);
	return FaceState{reinterpret_cast<const char*>(path), index};
}

FaceState* get_face(LoadedFont& font, unsigned index) {
	auto& state{font.faces_[index]};
	if(state.face_ && state.hb_font_) {
		if(state.raster_size_ != font.raster_size_) {
			if(FT_Set_Char_Size(state.face_, 0, static_cast<FT_F26Dot6>(font.raster_size_), 72, 72)) {
				Log::logger(Log::LogLevel::Error, R"_(Failed to set font size, path="{}")_", state.path_);
				return nullptr;
			}
			hb_ft_font_changed(state.hb_font_);
			state.raster_size_ = font.raster_size_;
		}
		return &state;
	}

	if(FT_New_Face(ft_library, state.path_.c_str(), state.face_index_, &state.face_)) {
		Log::logger(Log::LogLevel::Error, R"_(Failed to load font file, path="{}")_", state.path_);
		return nullptr;
	}
	if(FT_Set_Char_Size(state.face_, 0, static_cast<FT_F26Dot6>(font.raster_size_), 72, 72)) {
		Log::logger(Log::LogLevel::Error, R"_(Failed to set font size, path="{}")_", state.path_);
		FT_Done_Face(state.face_);
		state.face_ = nullptr;
		return nullptr;
	}

	state.hb_font_ = hb_ft_font_create_referenced(state.face_);
	if(!state.hb_font_) {
		Log::logger(Log::LogLevel::Error, "Failed to initialize HarfBuzz font");
		FT_Done_Face(state.face_);
		state.face_ = nullptr;
		return nullptr;
	}
	state.raster_size_ = font.raster_size_;
	return &state;
}

bool prepare_raster_size(LoadedFont& font, float scale) {
	if(!isfinite(scale) || scale <= 0.f) {
		Log::logger(Log::LogLevel::Error, "Invalid text scale {}", scale);
		return false;
	}
	const float size{max(64.f, round(static_cast<float>(font.pixel_size_) * scale * 64.f))};
	if(!isfinite(size) || size >= static_cast<float>(numeric_limits<int32_t>::max())) {
		Log::logger(Log::LogLevel::Error, "Text raster size is too large");
		return false;
	}
	const auto raster_size{static_cast<unsigned>(size)};
	if(font.raster_size_ != raster_size) {
		// Cached glyphs contain pixels for the previous raster size.
		font.glyph_cache_.clear();
		font.raster_size_ = raster_size;
	}
	for(unsigned index{}; index < font.faces_.size(); ++index) {
		if(!get_face(font, index))
			return false;
	}
	return true;
}

bool pattern_covers(FcPattern* pattern, const vector<Codepoint>& cps, unsigned begin, unsigned end) {
	FcCharSet* charset{};
	if(FcPatternGetCharSet(pattern, FC_CHARSET, 0, &charset) != FcResultMatch)
		return false;
	const auto cluster{ranges::subrange(cps.begin() + begin, cps.begin() + end)};
	return ranges::all_of(cluster, [&](const Codepoint& cp) {
		return is_ignorable_for_coverage(cp.value_) || FcCharSetHasChar(charset, cp.value_);
	});
}

void resolve_missing_faces(LoadedFont& font, const vector<Codepoint>& cps) {
	struct MissingCluster {
		unsigned begin_{};
		unsigned end_{};
	};

	vector<MissingCluster> missing{};

	for(unsigned begin{}; begin < cps.size();) {
		unsigned end{begin + 1};
		while(end < cps.size() && extends_cluster(cps[end].value_, cps[end - 1].value_))
			++end;

		if(choose_face(font, cps, begin, end) == font.faces_.size()) {
			const auto cluster{ranges::subrange(cps.begin() + begin, cps.begin() + end)};
			const bool is_already_unsupported{ranges::any_of(cluster,
				[&](const Codepoint& cp) { return font.unsupported_codepoints_.contains(cp.value_); })};
			if(!is_already_unsupported)
				missing.push_back({begin, end});
		}
		begin = end;
	}
	if(missing.empty())
		return;

	// One short-lived Fontconfig lookup handles every missing cluster in this
	// line. Only copied paths and face indices remain after this function.
	const FontQuery query(font.family_, font.pixel_size_, font.weight_);
	if(!query.matches_)
		return;
	for(const auto& cluster : missing) {
		if(choose_face(font, cps, cluster.begin_, cluster.end_) != font.faces_.size())
			continue;

		bool is_resolved{};
		bool has_failed_candidate{};
		for(int i{}; i < query.matches_->nfont; ++i) {
			FcPattern* pattern{query.matches_->fonts[i]};
			if(!pattern_covers(pattern, cps, cluster.begin_, cluster.end_))
				continue;

			auto source{font_source(pattern)};
			if(!source) {
				has_failed_candidate = true;
				continue;
			}
			const bool is_loaded{ranges::any_of(font.faces_, [&](const FaceState& face) {
				return face.path_ == source->path_ && face.face_index_ == source->face_index_;
			})};
			if(is_loaded)
				continue;

			font.faces_.push_back(std::move(*source));
			const unsigned index{static_cast<unsigned>(font.faces_.size() - 1)};
			if(!get_face(font, index)) {
				font.faces_.pop_back();
				has_failed_candidate = true;
				continue;
			}
			if(choose_face(font, cps, cluster.begin_, cluster.end_) == index) {
				is_resolved = true;
				break;
			}
		}

		if(!is_resolved && !has_failed_candidate) {
			for(unsigned i{cluster.begin_}; i < cluster.end_; ++i)
				if(!is_ignorable_for_coverage(cps[i].value_))
					font.unsupported_codepoints_.insert(cps[i].value_);
		}
	}
}

vector<Run> make_runs(const LoadedFont& font, const vector<Codepoint>& cps) {
	vector<Run> runs{};
	for(unsigned begin{}; begin < cps.size();) {
		unsigned end{begin + 1};
		while(end < cps.size() && extends_cluster(cps[end].value_, cps[end - 1].value_))
			++end;

		unsigned face_index{choose_face(font, cps, begin, end)};
		if(face_index == font.faces_.size())
			face_index = 0; // .notdef glyph
		hb_script_t script{HB_SCRIPT_COMMON};
		for(unsigned j{begin}; j < end; ++j) {
			const auto candidate{hb_unicode_script(hb_unicode_funcs_get_default(), cps[j].value_)};
			if(candidate != HB_SCRIPT_COMMON && candidate != HB_SCRIPT_INHERITED) {
				script = candidate;
				break;
			}
		}
		if(script == HB_SCRIPT_COMMON && !runs.empty())
			script = runs.back().script_;

		if(!runs.empty() && runs.back().face_index_ == face_index &&
			(runs.back().script_ == script || script == HB_SCRIPT_COMMON)) {
			runs.back().end_ = end;
		} else {
			runs.push_back({begin, end, face_index, script});
		}
		begin = end;
	}
	return runs;
}

struct PixelPhase {
	int64_t  whole_{};
	unsigned phase_{};
};

PixelPhase split_26_6(int64_t v) {
	const int64_t quarters{(v + 8) >> 4};
	return {quarters >> 2, static_cast<unsigned>(quarters & 3)};
}

uint64_t glyph_key(unsigned face_index, unsigned glyph_id, unsigned phase_x, unsigned phase_y) {
	return (static_cast<uint64_t>(face_index) << 36) | (static_cast<uint64_t>(phase_x) << 34) |
		(static_cast<uint64_t>(phase_y) << 32) | glyph_id;
}

GlyphBitmap rasterize(LoadedFont& font, unsigned face_index, unsigned glyph_id, unsigned phase_x, unsigned phase_y) {
	FT_Face face{font.faces_[face_index].face_};
	// Not full hinting: it reshapes glyphs per phase and size, so moving text would shimmer.
	if(FT_Load_Glyph(face, glyph_id, FT_LOAD_COLOR | FT_LOAD_TARGET_LIGHT)) {
		Log::logger(Log::LogLevel::Error, "Failed to load glyph {}", glyph_id);
		return GlyphBitmap::failure();
	}
	if(face->glyph->format == FT_GLYPH_FORMAT_OUTLINE) {
		FT_Outline_Translate(&face->glyph->outline,
			static_cast<FT_Pos>(phase_x) * 16,
			-static_cast<FT_Pos>(phase_y) * 16);
		if(FT_Render_Glyph(face->glyph, FT_RENDER_MODE_LIGHT)) {
			Log::logger(Log::LogLevel::Error, "Failed to rasterize glyph {}", glyph_id);
			return GlyphBitmap::failure();
		}
	}

	const FT_Bitmap& bitmap{face->glyph->bitmap};
	GlyphBitmap      result{};
	result.left_   = face->glyph->bitmap_left;
	result.top_    = face->glyph->bitmap_top;
	result.width_  = bitmap.width;
	result.height_ = bitmap.rows;
	if(result.height_ && result.width_ > numeric_limits<size_t>::max() / result.height_) {
		Log::logger(Log::LogLevel::Error, "Glyph bitmap dimensions are too large");
		return GlyphBitmap::failure();
	}
	result.pixels_.resize(static_cast<size_t>(result.width_) * result.height_);

	if(!result.width_ || !result.height_)
		return result;
	if(!bitmap.buffer) {
		Log::logger(Log::LogLevel::Error, "Glyph bitmap has no pixels");
		return GlyphBitmap::failure();
	}

	const auto pitch{static_cast<size_t>(abs(bitmap.pitch))};
	for(unsigned y{}; y < result.height_; ++y) {
		const unsigned source_y{bitmap.pitch < 0 ? result.height_ - 1 - y : y};
		const auto*    row{bitmap.buffer + static_cast<size_t>(source_y) * pitch};
		for(unsigned x{}; x < result.width_; ++x) {
			uint8_t alpha{};
			switch(bitmap.pixel_mode) {
				case FT_PIXEL_MODE_GRAY:
					alpha = bitmap.num_grays == 256 ? row[x]
						: bitmap.num_grays > 1      ? static_cast<uint8_t>(row[x] * 255u / (bitmap.num_grays - 1))
													: 0;
					break;
				case FT_PIXEL_MODE_MONO:
					alpha = (row[x / 8] & (0x80u >> (x % 8))) ? 255 : 0;
					break;
				case FT_PIXEL_MODE_BGRA:
					alpha = row[x * 4 + 3];
					break;
				default:
					Log::logger(Log::LogLevel::Error, "Unsupported glyph pixel mode {}", bitmap.pixel_mode);
					return GlyphBitmap::failure();
			}
			result.pixels_[static_cast<size_t>(y) * result.width_ + x] = alpha;
		}
	}
	return result;
}

const GlyphBitmap& get_glyph(LoadedFont&  font,
	const PositionedGlyph&                placement,
	unsigned                              phase_x,
	unsigned                              phase_y,
	unordered_map<uint64_t, GlyphBitmap>& scratch) {
	const auto key{glyph_key(placement.face_index_, placement.glyph_id_, phase_x, phase_y)};
	if(placement.is_cacheable_) {
		const auto cached{font.glyph_cache_.find(key)};
		if(cached != font.glyph_cache_.end())
			return cached->second;
	}

	auto it{scratch.find(key)};
	if(it == scratch.end()) {
		auto glyph{rasterize(font, placement.face_index_, placement.glyph_id_, phase_x, phase_y)};
		if(placement.is_cacheable_ && !glyph.is_failed_)
			return font.glyph_cache_.emplace(key, std::move(glyph)).first->second;
		it = scratch.emplace(key, std::move(glyph)).first;
	}
	return it->second;
}

LoadedFont* require_font(FontHandle handle) {
	const auto it{loaded_fonts.find(handle.id_)};
	if(it == loaded_fonts.end()) {
		Log::logger(Log::LogLevel::Error, "Invalid text font handle {}", handle.id_);
		return nullptr;
	}
	return it->second.get();
}

}

namespace Text {
void init() {
	if(ft_library) {
		Log::logger(Log::LogLevel::Warning, "Text is already initialized");
		return;
	}
	FT_Library library{};
	if(FT_Init_FreeType(&library))
		Log::fatal("Failed to initialize FreeType");
	ft_library = library;
}

FontHandle load_font(string_view family, unsigned pixel_size, Text::Weight weight) {
	if(!ft_library) {
		Log::logger(Log::LogLevel::Error, "Text is not initialized");
		return {};
	}
	if(family.empty() || !pixel_size) {
		Log::logger(Log::LogLevel::Error, "Font family and pixel size are required");
		return {};
	}

	auto font{make_unique<LoadedFont>()};
	font->family_      = family;
	font->weight_      = weight;
	font->pixel_size_  = pixel_size;
	font->raster_size_ = pixel_size * 64;
	{
		const FontQuery query(font->family_, pixel_size, weight);
		if(!query.has_matches())
			return {};
		for(int i{}; i < query.matches_->nfont; ++i) {
			auto source{font_source(query.matches_->fonts[i])};
			if(!source)
				continue;
			font->faces_.push_back(std::move(*source));
			if(get_face(*font, 0))
				break;
			font->faces_.clear();
		}
	}
	if(font->faces_.empty()) {
		Log::logger(Log::LogLevel::Error, "Failed to open a font for {}", family);
		return {};
	}

	if(!next_font_id) {
		Log::logger(Log::LogLevel::Error, "Text font handle limit reached");
		return {};
	}
	FontHandle handle{next_font_id++};
	loaded_fonts.emplace(handle.id_, std::move(font));
	return handle;
}

void unload_font(FontHandle font) {
	if(loaded_fonts.erase(font.id_) != 1)
		Log::logger(Log::LogLevel::Warning, "Invalid text font handle {}", font.id_);
}

unsigned raster_size(FontHandle handle, float scale) {
	const auto it{loaded_fonts.find(handle.id_)};
	if(it == loaded_fonts.end() || !isfinite(scale) || scale <= 0.f)
		return 0;
	const float size{max(64.f, round(static_cast<float>(it->second->pixel_size_) * scale * 64.f))};
	return size < static_cast<float>(numeric_limits<int32_t>::max()) ? static_cast<unsigned>(size) : 0;
}

Text::Bitmap draw(string_view text, FontHandle handle, float scale, float sub_x, float sub_y) {
	LoadedFont* const font{require_font(handle)};
	Bitmap            bitmap{};
	if(!font || text.empty())
		return bitmap;
	if(!prepare_raster_size(*font, scale))
		return bitmap;

	const auto cps{decode_utf8(text)};
	if(cps.size() > static_cast<size_t>(numeric_limits<int>::max())) {
		Log::logger(Log::LogLevel::Error, "Text is too long to shape");
		return {};
	}
	resolve_missing_faces(*font, cps);
	const auto       runs{make_runs(*font, cps)};
	vector<uint32_t> values{};
	values.reserve(cps.size());
	for(const auto& cp : cps)
		values.push_back(cp.value_);

	vector<PositionedGlyph> placements{};
	int64_t                 pen_x{};
	int64_t                 pen_y{};
	for(const auto& run : runs) {
		hb_buffer_t* const buffer{hb_buffer_create()};
		if(!buffer) {
			Log::logger(Log::LogLevel::Error, "Failed to create HarfBuzz buffer");
			return {};
		}
		hb_buffer_add_codepoints(buffer,
			values.data(),
			static_cast<int>(values.size()),
			run.begin_,
			static_cast<int>(run.end_ - run.begin_));
		hb_buffer_set_direction(buffer, HB_DIRECTION_LTR);
		if(run.script_ != HB_SCRIPT_UNKNOWN && run.script_ != HB_SCRIPT_COMMON)
			hb_buffer_set_script(buffer, run.script_);
		hb_buffer_guess_segment_properties(buffer);
		hb_shape(font->faces_[run.face_index_].hb_font_, buffer, nullptr, 0);

		unsigned         count{};
		const auto*      infos{hb_buffer_get_glyph_infos(buffer, &count)};
		const auto*      positions{hb_buffer_get_glyph_positions(buffer, &count)};
		vector<unsigned> clusters{};
		clusters.reserve(count);
		for(unsigned i{}; i < count; ++i)
			clusters.push_back(infos[i].cluster);
		sort(clusters.begin(), clusters.end());
		clusters.erase(unique(clusters.begin(), clusters.end()), clusters.end());

		for(unsigned i{}; i < count; ++i) {
			const unsigned cluster_begin{infos[i].cluster};
			const auto     next{upper_bound(clusters.begin(), clusters.end(), cluster_begin)};
			const unsigned cluster_end{next == clusters.end() ? run.end_ : *next};
			bool           is_cacheable{cluster_begin < cluster_end && cluster_end <= values.size()};
			for(unsigned j = cluster_begin; is_cacheable && j < cluster_end; ++j)
				is_cacheable = is_cacheable_codepoint(values[j]);

			placements.push_back({run.face_index_,
				infos[i].codepoint,
				is_cacheable,
				pen_x + positions[i].x_offset,
				pen_y + positions[i].y_offset});
			pen_x += positions[i].x_advance;
			pen_y += positions[i].y_advance;
		}
		hb_buffer_destroy(buffer);
	}

	bitmap.advance_x_ = static_cast<float>(pen_x) / 64.f;
	unordered_map<uint64_t, GlyphBitmap> scratch{};
	int64_t                              min_x{min<int64_t>(0, static_cast<int64_t>(floor(bitmap.advance_x_)))};
	int64_t                              max_x{max<int64_t>(0, static_cast<int64_t>(ceil(bitmap.advance_x_)))};
	int64_t                              min_y{};
	int64_t                              max_y{};
	bool                                 has_pixels{};

	const int64_t sub_x_26_6{lround(clamp(sub_x, 0.f, 1.f) * 64.f)};
	const int64_t sub_y_26_6{lround(clamp(sub_y, 0.f, 1.f) * 64.f)};
	const auto    pen = [&](const PositionedGlyph& placement) {
		return pair{split_26_6(placement.x_26_6_ + sub_x_26_6), split_26_6(sub_y_26_6 - placement.y_26_6_)};
	};

	for(const auto& placement : placements) {
		const auto [px, py] = pen(placement);
		const auto& glyph   = get_glyph(*font, placement, px.phase_, py.phase_, scratch);
		if(glyph.pixels_.empty())
			continue;
		const auto x = px.whole_ + glyph.left_;
		const auto y = py.whole_ - glyph.top_;
		min_x        = min(min_x, x);
		max_x        = max(max_x, x + glyph.width_);
		if(!has_pixels) {
			min_y      = y;
			max_y      = y + glyph.height_;
			has_pixels = true;
		} else {
			min_y = min(min_y, y);
			max_y = max(max_y, y + glyph.height_);
		}
	}

	if(max_x - min_x > numeric_limits<uint32_t>::max() || max_y - min_y > numeric_limits<uint32_t>::max() ||
		min_x < numeric_limits<int32_t>::min() || min_y < numeric_limits<int32_t>::min()) {
		Log::logger(Log::LogLevel::Error, "Text bitmap dimensions are too large");
		return {};
	}

	bitmap.width_    = static_cast<uint32_t>(max_x - min_x);
	bitmap.height_   = static_cast<uint32_t>(max_y - min_y);
	bitmap.stride_   = bitmap.width_;
	bitmap.offset_x_ = static_cast<int32_t>(min_x);
	bitmap.offset_y_ = static_cast<int32_t>(min_y);
	if(bitmap.height_ && bitmap.width_ > numeric_limits<size_t>::max() / bitmap.height_) {
		Log::logger(Log::LogLevel::Error, "Text bitmap dimensions are too large");
		return {};
	}
	bitmap.pixels_.resize(static_cast<size_t>(bitmap.width_) * bitmap.height_);

	for(const auto& placement : placements) {
		const auto [px, py] = pen(placement);
		const auto& glyph   = get_glyph(*font, placement, px.phase_, py.phase_, scratch);
		if(glyph.pixels_.empty())
			continue;
		const auto x = px.whole_ + glyph.left_ - min_x;
		const auto y = py.whole_ - glyph.top_ - min_y;
		for(unsigned row{}; row < glyph.height_; ++row) {
			for(unsigned col{}; col < glyph.width_; ++col) {
				const auto source = glyph.pixels_[static_cast<size_t>(row) * glyph.width_ + col];
				auto&      target =
					bitmap.pixels_[static_cast<size_t>(y + row) * bitmap.stride_ + static_cast<size_t>(x) + col];
				target = static_cast<uint8_t>(source + (static_cast<unsigned>(target) * (255u - source) + 127u) / 255u);
			}
		}
	}
	return bitmap;
}

void shutdown() {
	loaded_fonts.clear();
	if(ft_library) {
		FT_Done_FreeType(ft_library);
		ft_library = nullptr;
	}
}

}
