#pragma once

#include <glaze/json.hpp>

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace lyricsmpris::detail::responses {

// Providers use strings and numbers interchangeably. Unsupported field types
// are validated and skipped, rather than building a dynamic JSON subtree.
using Scalar = std::variant<std::string, std::uint64_t, std::int64_t, double,
    bool, std::nullptr_t, glz::skip>;

template<class T>
using Object = std::variant<T, std::string, double, bool, std::nullptr_t, std::vector<glz::skip>>;

template<class T>
using Array = std::variant<std::vector<Object<T>>, T, std::string, double, bool, std::nullptr_t>;

// Direct lyric endpoints may return one object or a list of objects.
template<class T>
using Rows = Array<T>;

struct Lrclib {
    Scalar trackName, name, artistName, artist, albumName, album;
    Scalar duration, syncedLyrics, plainLyrics, instrumental;
};

struct Lrcx {
    Scalar title, trackName, name, artist, artistName, album, albumName;
    Scalar duration, lyrics, lyric, lrc, syncedLyrics, lrc_ttml, plainLyrics;
};

struct Artist { Scalar name; };
struct Album { Scalar name; };
struct Lyric { Scalar lyric; };

struct NeteaseSong {
    Scalar name, duration, dt, id;
    Array<Artist> artists, ar;
    Object<Album> album, al;
};
struct NeteaseResult { Array<NeteaseSong> songs; };
struct NeteaseSearch { Object<NeteaseResult> result; };
struct NeteaseLyric { Object<Lyric> lrc, tlyric; };

struct QqSong {
    Scalar songname, title, albumname, interval, songmid, mid;
    Array<Artist> singer;
};
struct QqSongs { Array<QqSong> list; };
struct QqData { Object<QqSongs> song; };
struct QqSearch { Object<QqData> data; };
struct QqLyric { Scalar lyric; };

struct KugouSong {
    Scalar SongName, FileName, SingerName, AlbumName, Duration, FileHash, Hash;
};
struct KugouData { Array<KugouSong> lists; };
struct KugouSearch { Object<KugouData> data; };
struct KugouCandidate { Scalar id, accesskey; };
struct KugouLyricSearch { Array<KugouCandidate> candidates; };
struct KugouDownload { Scalar content; };

struct Subtitle { Scalar subtitle_body; };
struct PlainLyrics { Scalar lyrics_body; };
struct MusixmatchBody { Object<Subtitle> subtitle; Object<PlainLyrics> lyrics; };
struct MusixmatchMessage { Object<MusixmatchBody> body; };
struct Musixmatch { Object<MusixmatchMessage> message; };

} // namespace lyricsmpris::detail::responses
