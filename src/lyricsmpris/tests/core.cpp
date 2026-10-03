#include "Check.hpp"
#include "lyricsmpris/Lyrics.hpp"
#include "lyricsmpris/detail/Normalize.hpp"
#include "lyricsmpris/detail/Providers.hpp"
#include "lyricsmpris/detail/Mpris.hpp"

#include <iostream>
#include <limits>

using namespace lyricsmpris;
using namespace std::chrono_literals;

void parsing() {
    auto result = parse_lyrics("\xef\xbb\xbf[ti:Track]\r\n[00:05.25]late\r[00:01.1][00:03.123]早 &amp; 晚\n[00:04.00]\n[offset:-100]\n", "local");
    CHECK(result);
    CHECK(result->synced());
    CHECK(result->size() == 4);
    CHECK(result->line(0).start_time == 1s);
    CHECK(result->line(1).start_time == 3023ms);
    CHECK(result->line(0).text == "早 & 晚");
    CHECK(result->line(0).text.data() == result->line(1).text.data());
    CHECK(!result->line_at(999ms));
    CHECK(result->line_at(1s) == 0);
    CHECK(result->line_at(4500ms) == 2);
    CHECK(result->line(2).text.empty());
    CHECK(result->line_at(100s) == 3);
    CHECK(result->provider() == "local");

    auto repeated = parse_lyrics("[00:01]原文\n[00:01]translation\n[00:02]next");
    CHECK(repeated->line_at(1s) == 1);
    CHECK(repeated->line(0).text == "原文");
    auto small = parse_lyrics("[00:01]x");
    auto moved = std::move(*small);
    CHECK(moved.line(0).text == "x");
    auto copied = moved;
    moved = {};
    CHECK(copied.line(0).text == "x");

    auto plain = parse_lyrics("one\ntwo\n");
    CHECK(plain->size() == 2);
    CHECK(!plain->synced());
    CHECK(!plain->line(0).start_time);
    CHECK(plain->line_at(100s) == 0);
    CHECK(parse_lyrics("暂无歌词")->empty());
    CHECK(parse_lyrics("[00:00]純音樂")->instrumental());
    CHECK(parse_lyrics("")->empty());
    CHECK(parse_lyrics("[00:01]<10,20,0>hi<b>there</b> &#x4e16;&#30028; &lt;3")->line(0).text == "hithere 世界 <3");
    CHECK(parse_lyrics("[00:01]valid\ncomment")->size() == 1);
    CHECK(parse_lyrics("[offset:-9223372036854775808]\n[00:01]x")->line(0).start_time == 0ms);
    CHECK(parse_lyrics("[offset:9223372036854775807]\n[00:01]x")->line(0).start_time->count() == std::numeric_limits<std::int64_t>::max());
    CHECK(!parse_lyrics("[00:01]xx", {}, {3, 100}));
    CHECK(!parse_lyrics("[00:01][00:02]x", {}, {100, 1}));
    CHECK(!parse_lyrics("[00:01]x\n[00:02]y", {}, {100, 1}));
    CHECK(!parse_lyrics(std::string("a\0b", 3)));
    CHECK(!parse_lyrics("\xff"));
    bool throws = false;
    try { (void)copied.line(2); } catch (const std::out_of_range&) { throws = true; }
    CHECK(throws);
}

void matching() {
    using namespace lyricsmpris::detail;
    CHECK(normalize("臺灣的愛") == normalize("台湾的爱"));
    CHECK(normalize("CAFÉ") == normalize("Cafe\xcc\x81"));
    CHECK(normalize("１. Song （Official Audio）", true) == "song");
    CHECK(normalize("Love feat. Artist", true) == "love");
    TrackQuery query{"Song", "Artist", "Album", 180s};
    Candidate candidate;
    candidate.title = "Song"; candidate.artist = "Artist"; candidate.album = "Album";
    candidate.duration = 180s; candidate.synced = "[00:01]lyrics";
    CHECK(evaluate(query, candidate).high_confidence);
    candidate.title = "Unrelated";
    CHECK(!evaluate(query, candidate).accepted);
    candidate.title = "Song (Live)";
    CHECK(!evaluate(query, candidate).accepted);
    candidate.title = "Song"; candidate.artist = "Different";
    CHECK(!evaluate(query, candidate).accepted);
    candidate.artist = "Artist, Guest";
    CHECK(evaluate(query, candidate).accepted);
    candidate.duration = 240s;
    CHECK(!evaluate(query, candidate).accepted);
    candidate.duration = 180s; candidate.artist = "Artist";
    candidate.synced = "[ti:Wrong]\n[00:01]lyrics";
    CHECK(!evaluate(query, candidate).accepted);
    candidate.synced = "[ti:Song]\n[ar:Artist]\n[00:01]lyrics";
    candidate.trusted = false;
    CHECK(evaluate(query, candidate).accepted);
    candidate.synced = "[00:01]lyrics";
    CHECK(!evaluate(query, candidate).accepted);
    candidate.trusted = true;
    candidate.synced.clear();
    candidate.instrumental = true;
    CHECK(evaluate(query, candidate).high_confidence);
}

void providers() {
    using namespace lyricsmpris::detail;
    auto lrclib = parse_candidates(R"([{"trackName":"Song","artistName":"Artist","duration":180.25,"syncedLyrics":"[00:01]x"}])", Provider::Lrclib, Stage::LrclibSearch);
    CHECK(lrclib && lrclib->size() == 1 && lrclib->front().duration == 180250ms);
    CHECK(!parse_candidates("bad", Provider::Lrclib, Stage::LrclibGet));
    CHECK(!parse_candidates("{} trailing", Provider::Lrclib, Stage::LrclibGet));
    CHECK(parse_candidates("null", Provider::Lrclib, Stage::LrclibGet)->empty());
    auto netease = parse_candidates(R"({"result":{"songs":[{"name":"Song","ar":[{"name":"Artist"}],"al":{"name":"Album"},"dt":180000,"id":42}]}})", Provider::Netease, Stage::NeteaseSearch);
    CHECK(netease->front().resource_id == "42");
    CHECK(netease->front().artist == "Artist");
    CHECK(parse_candidates(R"({"lrc":{"lyric":"[00:01]x"}})", Provider::Netease, Stage::NeteaseLyric)->front().synced == "[00:01]x");
    auto qq = parse_candidates(R"({"data":{"song":{"list":[{"songname":"Song","songmid":"abc","interval":180,"singer":[{"name":"Artist"}]}]}}})", Provider::Qq, Stage::QqSearch);
    CHECK(qq->front().resource_id == "abc");
    CHECK(parse_candidates(R"({"lyric":"WzAwOjAxXXg="})", Provider::Qq, Stage::QqLyric)->front().synced == "[00:01]x");
    CHECK(parse_candidates(R"({"lyric":"plain words"})", Provider::Qq, Stage::QqLyric)->front().synced == "plain words");
    auto kugou = parse_candidates(R"({"data":{"lists":[{"SongName":"Song","SingerName":"Artist","FileHash":"hash","Duration":180}]}})", Provider::Kugou, Stage::KugouSearch);
    CHECK(kugou->front().resource_id == "hash");
    auto resource = parse_candidates(R"({"candidates":[{"id":4,"accesskey":"key"}]})", Provider::Kugou, Stage::KugouLyricSearch);
    CHECK(resource->front().access_key == "key");
    CHECK(parse_candidates(R"({"content":"WzAwOjAxXXg="})", Provider::Kugou, Stage::KugouDownload)->front().synced == "[00:01]x");
    auto lrcx = parse_candidates(R"({"title":"Song","artist":"Artist","duration":180,"lrc_ttml":"<tt><body><p begin='00:00:01.250'><span>x</span></p></body></tt>"})", Provider::Lrcx, Stage::LrcxJson);
    CHECK(parse_lyrics(lrcx->front().synced)->line(0).start_time == 1250ms);
    auto musixmatch = parse_candidates(R"({"message":{"body":{"subtitle":{"subtitle_body":"[00:01]x"}}}})", Provider::Musixmatch, Stage::MusixmatchSubtitle);
    CHECK(musixmatch->front().synced == "[00:01]x");
    CHECK(decode_base64("WzAwOjAxXXg=\n") == "[00:01]x");
    CHECK(decode_base64("***").empty());
    TrackQuery query{"歌 & Song", "Artist", "", 180s};
    Candidate candidate; candidate.resource_id = "42";
    const auto request = provider_request(1, Stage::LrclibGet, query, candidate);
    CHECK(request.url.find("%E6%AD%8C%20%26%20Song") != request.url.npos);
    CHECK(provider_request(2, Stage::NeteaseLyric, query, candidate).referer == "https://music.163.com/");
}

void progress_and_memory() {
    using lyricsmpris::detail::estimated_position;
    State state;
    state.playback = Playback::Playing; state.playback_rate = 2;
    state.position = 3s; state.sampled_at = Clock::now(); state.duration = 10s;
    CHECK(estimated_position(state, state.sampled_at + 2s) == 7s);
    CHECK(estimated_position(state, state.sampled_at + 9s) == 10s);
    state.playback = Playback::Paused;
    CHECK(estimated_position(state, state.sampled_at + 9s) == 3s);
    state.playback = Playback::Playing; state.playback_rate = -2;
    CHECK(estimated_position(state, state.sampled_at + 2s) == 0ms);
    std::string text;
    for (unsigned i = 0; i < 300; ++i) text += '[' + std::to_string(i) + ":00]示例歌词 line\n";
    const auto document = parse_lyrics(text);
    CHECK(document && document->size() == 300);
    CHECK(document->storage_bytes() < 20000);
    std::cout << "300-line document storage: " << document->storage_bytes() << " bytes\n";
}

int main() {
    try { parsing(); matching(); providers(); progress_and_memory(); }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
    std::cout << "Core checks passed\n";
}
