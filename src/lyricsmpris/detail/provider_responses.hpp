#pragma once

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

#include <glaze/json/read.hpp>

namespace LyricsMpris::Detail::Responses {

// Providers use strings and numbers interchangeably. Unsupported field types
// are validated and skipped, rather than building a dynamic JSON subtree.
/// @brief Scalar value accepted from provider JSON.
using Scalar = std::variant<std::string, uint64_t, int64_t, double, bool, std::nullptr_t, glz::skip>;

/// @brief JSON value that may hold an object of T.
template<class T>
using Object = std::variant<T, std::string, double, bool, std::nullptr_t, std::vector<glz::skip>>;

/// @brief JSON value that may hold an array of T.
template<class T>
using Array = std::variant<std::vector<Object<T>>, T, std::string, double, bool, std::nullptr_t>;

// Direct lyric endpoints may return one object or a list of objects.
/// @brief JSON value that holds one row or a list of rows.
template<class T>
using Rows = Array<T>;

/// @brief Lrclib response row.
struct Lrclib {
	Scalar trackName{}, name{}, artistName{}, artist{}, albumName{}, album{};
	Scalar duration{}, syncedLyrics{}, plainLyrics{}, instrumental{};
};

/// @brief Lrcx response row.
struct Lrcx {
	Scalar title{}, trackName{}, name{}, artist{}, artistName{}, album{}, albumName{};
	Scalar duration{}, lyrics{}, lyric{}, lrc{}, syncedLyrics{}, lrc_ttml{}, plainLyrics{};
};

/// @brief Artist entry in provider responses.
struct Artist {
	Scalar name{};
};

/// @brief Album entry in provider responses.
struct Album {
	Scalar name{};
};

/// @brief Lyric payload in provider responses.
struct Lyric {
	Scalar lyric{};
};

/// @brief NetEase song search result.
struct NeteaseSong {
	Scalar        name{}, duration{}, dt{}, id{};
	Array<Artist> artists{}, ar{};
	Object<Album> album{}, al{};
};

/// @brief NetEase search result payload.
struct NeteaseResult {
	Array<NeteaseSong> songs{};
};

/// @brief NetEase search response.
struct NeteaseSearch {
	Object<NeteaseResult> result{};
};

/// @brief NetEase lyric response.
struct NeteaseLyric {
	Object<Lyric> lrc{}, tlyric{};
};

/// @brief QQ Music song search result.
struct QqSong {
	Scalar        songname{}, title{}, albumname{}, interval{}, songmid{}, mid{};
	Array<Artist> singer{};
};

/// @brief QQ Music song list payload.
struct QqSongs {
	Array<QqSong> list{};
};

/// @brief QQ Music song data payload.
struct QqData {
	Object<QqSongs> song{};
};

/// @brief QQ Music search response.
struct QqSearch {
	Object<QqData> data{};
};

/// @brief QQ Music lyric response.
struct QqLyric {
	Scalar lyric{};
};

/// @brief Kugou song search result.
struct KugouSong {
	Scalar SongName{}, FileName{}, SingerName{}, AlbumName{}, Duration{}, FileHash{}, Hash{};
};

/// @brief Kugou song data payload.
struct KugouData {
	Array<KugouSong> lists{};
};

/// @brief Kugou search response.
struct KugouSearch {
	Object<KugouData> data{};
};

/// @brief Kugou lyric search candidate.
struct KugouCandidate {
	Scalar id{}, accesskey{};
};

/// @brief Kugou lyric search response.
struct KugouLyricSearch {
	Array<KugouCandidate> candidates{};
};

/// @brief Kugou lyric download response.
struct KugouDownload {
	Scalar content{};
};

/// @brief Musixmatch subtitle payload.
struct Subtitle {
	Scalar subtitle_body{};
};

/// @brief Musixmatch plain lyric payload.
struct PlainLyrics {
	Scalar lyrics_body{};
};

/// @brief Musixmatch body payload.
struct MusixmatchBody {
	Object<Subtitle>    subtitle{};
	Object<PlainLyrics> lyrics{};
};

/// @brief Musixmatch message payload.
struct MusixmatchMessage {
	Object<MusixmatchBody> body{};
};

/// @brief Musixmatch response.
struct Musixmatch {
	Object<MusixmatchMessage> message{};
};

}
