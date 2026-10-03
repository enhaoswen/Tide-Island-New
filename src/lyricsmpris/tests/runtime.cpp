#include "Check.hpp"
#include "lyricsmpris/Client.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <source_location>
#include <systemd/sd-bus.h>
#include <unistd.h>

using namespace lyricsmpris;
using namespace std::chrono_literals;

constexpr auto path = "/org/mpris/MediaPlayer2";
constexpr auto interface = "org.mpris.MediaPlayer2.Player";

struct FakePlayer {
    sd_bus* bus = nullptr;
    sd_bus_slot* slot = nullptr;
    sd_bus_slot* filter_slot = nullptr;
    sd_bus_message* held_position = nullptr;
    std::string service;
    std::string title = "Song";
    std::string track = "/track/one";
    std::string media;
    std::string text;
    std::string status = "Playing";
    std::string last_method;
    bool can_next = true;
    bool fail_next = false;
    bool defer_position = false;
    unsigned position_reads = 0;
    double rate = 1;
    std::int64_t base_position = 0;
    Clock::time_point anchor = Clock::now();

    std::int64_t position() const {
        const auto elapsed = std::chrono::duration<double, std::micro>(Clock::now() - anchor).count();
        return base_position + (status == "Playing" ? static_cast<std::int64_t>(elapsed * rate) : 0);
    }

    static int property(sd_bus*, const char*, const char*, const char* name,
        sd_bus_message* reply, void* user, sd_bus_error*) {
        auto& self = *static_cast<FakePlayer*>(user);
        const std::string_view key = name;
        if (key == "PlaybackStatus") return sd_bus_message_append(reply, "s", self.status.c_str());
        if (key == "Rate") return sd_bus_message_append(reply, "d", self.rate);
        if (key == "Position") { ++self.position_reads; return sd_bus_message_append(reply, "x", self.position()); }
        if (key == "CanGoNext") return sd_bus_message_append(reply, "b", self.can_next);
        if (key != "Metadata") return sd_bus_message_append(reply, "b", 1);
        int result = sd_bus_message_open_container(reply, 'a', "{sv}");
        auto string = [&](const char* key, const char* signature, const std::string& value) {
            if (result < 0) return;
            result = sd_bus_message_open_container(reply, 'e', "sv");
            if (result >= 0) result = sd_bus_message_append(reply, "s", key);
            if (result >= 0) result = sd_bus_message_open_container(reply, 'v', signature);
            if (result >= 0) result = sd_bus_message_append(reply, signature, value.c_str());
            if (result >= 0) result = sd_bus_message_close_container(reply);
            if (result >= 0) result = sd_bus_message_close_container(reply);
        };
        string("mpris:trackid", "o", self.track);
        string("xesam:title", "s", self.title);
        string("xesam:album", "s", "Album");
        string("mpris:artUrl", "s", "file:///tmp/cover.png");
        string("xesam:url", "s", self.media);
        string("xesam:asText", "s", self.text);
        if (result >= 0) result = sd_bus_message_open_container(reply, 'e', "sv");
        if (result >= 0) result = sd_bus_message_append(reply, "s", "xesam:artist");
        if (result >= 0) result = sd_bus_message_open_container(reply, 'v', "as");
        if (result >= 0) result = sd_bus_message_open_container(reply, 'a', "s");
        if (result >= 0) result = sd_bus_message_append(reply, "ss", "Artist", "Guest");
        if (result >= 0) result = sd_bus_message_close_container(reply);
        if (result >= 0) result = sd_bus_message_close_container(reply);
        if (result >= 0) result = sd_bus_message_close_container(reply);
        if (result >= 0) result = sd_bus_message_open_container(reply, 'e', "sv");
        if (result >= 0) result = sd_bus_message_append(reply, "s", "mpris:length");
        if (result >= 0) result = sd_bus_message_open_container(reply, 'v', "x");
        if (result >= 0) result = sd_bus_message_append(reply, "x", std::int64_t{180000000});
        if (result >= 0) result = sd_bus_message_close_container(reply);
        if (result >= 0) result = sd_bus_message_close_container(reply);
        if (result >= 0) result = sd_bus_message_close_container(reply);
        return result;
    }

    static int method(sd_bus_message* message, void* user, sd_bus_error*) {
        auto& self = *static_cast<FakePlayer*>(user);
        self.last_method = sd_bus_message_get_member(message);
        if (self.last_method == "Next" && self.fail_next)
            return sd_bus_reply_method_errorf(message, "org.mpris.MediaPlayer2.Error.Failed", "Fake next failure");
        self.base_position = self.position();
        self.anchor = Clock::now();
        if (self.last_method == "Pause") self.status = "Paused";
        if (self.last_method == "Play") self.status = "Playing";
        if (self.last_method == "PlayPause") self.status = self.status == "Playing" ? "Paused" : "Playing";
        sd_bus_emit_properties_changed(self.bus, path, interface, "PlaybackStatus", nullptr);
        return sd_bus_reply_method_return(message, "");
    }

    static int filter(sd_bus_message* message, void* user, sd_bus_error*) {
        auto& self = *static_cast<FakePlayer*>(user);
        if (!self.defer_position || self.held_position
            || !sd_bus_message_is_method_call(message, "org.freedesktop.DBus.Properties", "Get")) return 0;
        const char *requested_interface = nullptr, *property = nullptr;
        sd_bus_message_read(message, "ss", &requested_interface, &property);
        sd_bus_message_rewind(message, true);
        if (!property || std::string_view(property) != "Position") return 0;
        self.held_position = sd_bus_message_ref(message);
        return 1;
    }

    explicit FakePlayer(std::string name) : service(std::move(name)) {
        static const sd_bus_vtable vtable[] = {
            SD_BUS_VTABLE_START(0),
            SD_BUS_METHOD("Play", "", "", method, SD_BUS_VTABLE_UNPRIVILEGED),
            SD_BUS_METHOD("Pause", "", "", method, SD_BUS_VTABLE_UNPRIVILEGED),
            SD_BUS_METHOD("PlayPause", "", "", method, SD_BUS_VTABLE_UNPRIVILEGED),
            SD_BUS_METHOD("Next", "", "", method, SD_BUS_VTABLE_UNPRIVILEGED),
            SD_BUS_METHOD("Previous", "", "", method, SD_BUS_VTABLE_UNPRIVILEGED),
            SD_BUS_PROPERTY("Metadata", "a{sv}", property, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
            SD_BUS_PROPERTY("PlaybackStatus", "s", property, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
            SD_BUS_PROPERTY("Rate", "d", property, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
            SD_BUS_PROPERTY("Position", "x", property, 0, 0),
            SD_BUS_PROPERTY("CanControl", "b", property, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
            SD_BUS_PROPERTY("CanPlay", "b", property, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
            SD_BUS_PROPERTY("CanPause", "b", property, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
            SD_BUS_PROPERTY("CanGoNext", "b", property, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
            SD_BUS_PROPERTY("CanGoPrevious", "b", property, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
            SD_BUS_SIGNAL("Seeked", "x", 0),
            SD_BUS_VTABLE_END
        };
        CHECK(sd_bus_open_user(&bus) >= 0);
        CHECK(sd_bus_add_filter(bus, &filter_slot, filter, this) >= 0);
        CHECK(sd_bus_add_object_vtable(bus, &slot, path, interface, vtable, this) >= 0);
        CHECK(sd_bus_request_name(bus, service.c_str(), 0) >= 0);
    }

    ~FakePlayer() {
        sd_bus_message_unref(held_position);
        sd_bus_slot_unref(filter_slot);
        sd_bus_slot_unref(slot);
        sd_bus_close_unref(bus);
    }

    void changed(const char* property = "Metadata") {
        CHECK(sd_bus_emit_properties_changed(bus, path, interface, property, nullptr) >= 0);
    }
    void seek(std::int64_t value) {
        base_position = value; anchor = Clock::now();
        CHECK(sd_bus_emit_signal(bus, path, interface, "Seeked", "x", value) >= 0);
    }
    void finish_held_position(std::int64_t value) {
        CHECK(held_position);
        sd_bus_message* reply = nullptr;
        CHECK(sd_bus_message_new_method_return(held_position, &reply) >= 0);
        CHECK(sd_bus_message_open_container(reply, 'v', "x") >= 0);
        CHECK(sd_bus_message_append(reply, "x", value) >= 0);
        CHECK(sd_bus_message_close_container(reply) >= 0);
        CHECK(sd_bus_send(bus, reply, nullptr) >= 0);
        sd_bus_message_unref(reply);
        held_position = sd_bus_message_unref(held_position);
        defer_position = false;
    }
};

void drive(Client& client, std::span<FakePlayer* const> players, const std::function<bool()>& done,
    Milliseconds limit = 1500ms, std::source_location location = std::source_location::current()) {
    const auto end = Clock::now() + limit;
    std::vector<pollfd> ready;
    while (Clock::now() < end) {
        for (auto* player : players) if (player) {
            int result;
            while ((result = sd_bus_process(player->bus, nullptr)) > 0) {}
            CHECK(result >= 0);
        }
        auto result = client.process(ready);
        if (!result) throw std::runtime_error(result.error().message);
        if (done()) return;
        const auto interests = client.poll_fds();
        ready.assign(interests.begin(), interests.end());
        for (auto* player : players) if (player) ready.push_back({sd_bus_get_fd(player->bus), static_cast<short>(sd_bus_get_events(player->bus)), 0});
        auto deadline = end;
        if (auto value = client.next_deadline()) deadline = std::min(deadline, *value);
        const auto remaining = std::chrono::ceil<Milliseconds>(deadline - Clock::now()).count();
        CHECK(poll(ready.data(), ready.size(), static_cast<int>(std::clamp<std::int64_t>(remaining, 0, 50))) >= 0);
    }
    throw std::runtime_error("Runtime wait timed out at line " + std::to_string(location.line())
        + "; service=" + client.state().service + " title=" + client.state().title
        + " playback=" + std::to_string(static_cast<int>(client.state().playback))
        + " lyric=" + std::string(client.current_lyric()));
}

void metadata_and_controls() {
    Options options; options.providers.clear(); options.local_lyrics = false; options.max_search_attempts = 1;
    auto created = Client::create(options);
    CHECK(created);
    auto client = std::move(*created);
    CHECK(!client.play());
    FakePlayer fake("org.mpris.MediaPlayer2.test_one");
    fake.text = "[00:00]first\n[00:02]second\n[00:03]\n[00:04]last";
    FakePlayer* players[] = {&fake};
    drive(client, players, [&] { return client.state().title == "Song" && client.current_lyric() == "first"; });
    CHECK(client.state().artwork_url == "file:///tmp/cover.png");
    CHECK(client.state().artists == std::vector<std::string>({"Artist", "Guest"}));
    CHECK(client.state().duration == 180s);
    CHECK(client.lyrics().line(1).start_time == 2s);
    CHECK(client.next_deadline());

    CHECK(client.pause());
    drive(client, players, [&] { return client.state().playback == Playback::Paused && fake.position_reads >= 2; });
    const auto frozen = client.position();
    CHECK(client.position(Clock::now() + 10s) == frozen);
    // Drain the command acknowledgement; paused synced lyrics need no timer.
    const auto settle = Clock::now() + 30ms;
    drive(client, players, [&] { return Clock::now() >= settle; });
    CHECK(!client.next_deadline());
    CHECK(client.play());
    drive(client, players, [&] { return client.state().playback == Playback::Playing; });
    CHECK(client.position(Clock::now() + 1s) >= client.position() + 999ms);
    CHECK(client.play_pause());
    drive(client, players, [&] { return client.state().playback == Playback::Paused; });
    fake.seek(2100000);
    drive(client, players, [&] { return client.current_lyric() == "second"; });
    fake.seek(3100000);
    drive(client, players, [&] { return client.state().current_line == 2; });
    CHECK(client.current_lyric().empty());
    CHECK(client.next());
    drive(client, players, [&] { return fake.last_method == "Next"; });
    CHECK(client.previous());
    drive(client, players, [&] { return fake.last_method == "Previous"; });
    fake.can_next = false; fake.changed("CanGoNext");
    drive(client, players, [&] { return !client.state().capabilities.next; });
    CHECK(!client.next());
    fake.can_next = true; fake.fail_next = true; fake.changed("CanGoNext");
    drive(client, players, [&] { return client.state().capabilities.next; });
    CHECK(client.next());
    drive(client, players, [&] { return client.state().last_error.has_value(); });
    CHECK(client.state().last_error->message == "Fake next failure");

    fake.text = "[00:00]updated"; fake.changed();
    // Updated embedded lyrics replace the previous document for the same track.
    drive(client, players, [&] { return client.current_lyric() == "updated"; });
    client.retry_lyrics();
    drive(client, players, [&] { return client.current_lyric() == "updated"; });
    fake.rate = 2; fake.changed("Rate");
    CHECK(client.play());
    drive(client, players, [&] { return client.state().playback_rate == 2 && client.state().playback == Playback::Playing; });
    CHECK(client.position(Clock::now() + 1s) >= client.position() + 1999ms);

    // A Get(Position) initiated before a seek must not overwrite its anchor.
    CHECK(client.pause());
    drive(client, players, [&] { return client.state().playback == Playback::Paused; });
    fake.defer_position = true;
    fake.track = "/track/two"; fake.title = "New Song"; fake.text = "[00:00]new first\n[00:05]new second";
    fake.changed();
    drive(client, players, [&] { return fake.held_position != nullptr && client.state().title == "New Song"; });
    fake.seek(6000000);
    drive(client, players, [&] { return client.current_lyric() == "new second"; });
    fake.finish_held_position(0);
    const auto after = Clock::now() + 40ms;
    drive(client, players, [&] { return Clock::now() >= after; });
    CHECK(client.current_lyric() == "new second");
    CHECK(client.position() == 6s);
}

void late_lyrics_and_selection() {
    FakePlayer fake("org.mpris.MediaPlayer2.test_late");
    Options options; options.providers.clear(); options.local_lyrics = false;
    options.max_search_attempts = 1;
    auto client = Client::create(options);
    CHECK(client);
    FakePlayer* players[] = {&fake};
    drive(*client, players, [&] { return client->state().has_player; });
    CHECK(client->lyrics().empty());
    const auto revision = client->state().track_revision;
    fake.text = "[00:00]late line"; fake.changed();
    drive(*client, players, [&] { return client->current_lyric() == "late line"; });
    CHECK(client->state().track_revision == revision);
    CHECK(!client->select_player("bad"));
    CHECK(!client->select_player("org.mpris.MediaPlayer2.missing"));
    // An alias with the same unique owner still receives property signals.
    const std::string alias = "org.mpris.MediaPlayer2.test_alias";
    CHECK(sd_bus_request_name(fake.bus, alias.c_str(), 0) >= 0);
    const auto alias_ready = Clock::now() + 40ms;
    drive(*client, players, [&] { return Clock::now() >= alias_ready; });
    CHECK(client->select_player(alias));
    drive(*client, players, [&] { return client->state().service == alias; });
    fake.text = "[00:00]alias updated"; fake.changed();
    drive(*client, players, [&] { return client->current_lyric() == "alias updated"; });
    CHECK(sd_bus_release_name(fake.bus, alias.c_str()) >= 0);
    drive(*client, players, [&] { return client->state().service == fake.service; });
    FakePlayer other("org.mpris.MediaPlayer2.test_other");
    other.text = "[00:00]other line";
    FakePlayer* both[] = {&fake, &other};
    auto blocked_options = options;
    blocked_options.blocked_services = {"test_other"};
    auto blocked_client = Client::create(blocked_options);
    CHECK(blocked_client);
    drive(*blocked_client, both, [&] { return blocked_client->state().has_player; });
    CHECK(!blocked_client->select_player(other.service));
    // Give discovery time to complete before explicit selection.
    const auto discovered = Clock::now() + 40ms;
    drive(*client, both, [&] { return Clock::now() >= discovered; });
    CHECK(client->select_player(other.service));
    drive(*client, both, [&] { return client->current_lyric() == "other line"; });
    fake.text = "[00:00]ignored"; fake.changed();
    const auto unchanged = Clock::now() + 40ms;
    drive(*client, both, [&] { return Clock::now() >= unchanged; });
    CHECK(client->current_lyric() == "other line");
    CHECK(sd_bus_release_name(other.bus, other.service.c_str()) >= 0);
    drive(*client, both, [&] { return client->state().service == fake.service; });
    CHECK(client->current_lyric() == "ignored");
    CHECK(sd_bus_release_name(fake.bus, fake.service.c_str()) >= 0);
    drive(*client, both, [&] { return !client->state().has_player; });
    CHECK(client->lyrics().empty());
    CHECK(!client->next());
}

void local_retry() {
    const auto directory = std::filesystem::temp_directory_path() / ("tide-lyrics-" + std::to_string(getpid()));
    std::filesystem::create_directories(directory);
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::filesystem::remove_all(path); } } cleanup{directory};
    FakePlayer fake("org.mpris.MediaPlayer2.test_local");
    fake.status = "Paused";
    fake.media = "file://" + (directory / "song.mp3").native();
    Options options; options.providers.clear(); options.retry_delay = 30ms; options.max_search_attempts = 3;
    auto client = Client::create(options);
    CHECK(client);
    FakePlayer* players[] = {&fake};
    drive(*client, players, [&] { return client->state().lyrics_search == SearchState::Retrying; });
    { std::ofstream file(directory / "song.lrc"); file << "[00:00]local late\n[00:01]second"; }
    drive(*client, players, [&] { return client->current_lyric() == "local late"; });
    CHECK(client->lyrics().provider() == "local");
    client->set_lyric_offset(1s);
    drive(*client, players, [&] { return !client->state().current_line; });
    client->set_lyric_offset(-1s);
    drive(*client, players, [&] { return client->current_lyric() == "second"; });

    // Retry exhaustion must stop creating deadlines rather than restart forever.
    std::filesystem::remove(directory / "song.lrc");
    fake.title = "Missing"; fake.track = "/track/missing"; fake.changed();
    drive(*client, players, [&] { return client->state().lyrics_search == SearchState::NotFound; });
    const auto after = Clock::now() + 40ms;
    drive(*client, players, [&] { return Clock::now() >= after; });
    CHECK(!client->next_deadline());
    // A local URL may arrive later, without changing the track ID or title.
    fake.media.clear(); fake.changed();
    const auto wait = Clock::now() + 30ms;
    drive(*client, players, [&] { return Clock::now() >= wait; });
    { std::ofstream file(directory / "song.lrc"); file << "[00:00]late url"; }
    fake.media = "file://" + (directory / "song.mp3").native(); fake.changed();
    client->set_lyric_offset(0ms);
    drive(*client, players, [&] { return client->current_lyric() == "late url"; });
}

void lyric_deadlines() {
    FakePlayer fake("org.mpris.MediaPlayer2.test_deadline");
    fake.rate = 2;
    fake.text = "[00:00]first\n[00:00.150]second\n[00:00.250]\n[00:00.350]last";
    Options options; options.providers.clear(); options.local_lyrics = false; options.max_search_attempts = 1;
    auto client = Client::create(options);
    CHECK(client);
    FakePlayer* players[] = {&fake};
    drive(*client, players, [&] { return client->current_lyric() == "first"; });
    const auto deadline = client->next_deadline();
    CHECK(deadline && *deadline <= Clock::now() + 100ms);
    const auto reads = fake.position_reads;
    drive(*client, players, [&] { return client->current_lyric() == "second"; }, 300ms);
    drive(*client, players, [&] { return client->state().current_line == 2; }, 300ms);
    CHECK(client->current_lyric().empty());
    drive(*client, players, [&] { return client->current_lyric() == "last"; }, 300ms);
    CHECK(fake.position_reads == reads);
    CHECK(client->pause());
    drive(*client, players, [&] { return client->state().playback == Playback::Paused; });
    const auto settled = Clock::now() + 50ms;
    drive(*client, players, [&] { return Clock::now() >= settled; });
    CHECK(!client->next_deadline());
    // A stable paused module returns no changes across repeated dispatches.
    for (unsigned i = 0; i < 1000; ++i) {
        const auto result = client->process();
        CHECK(result && *result == Changes::None);
        CHECK(client->poll_fds().size() == 1);
    }
}

int main() {
    try { metadata_and_controls(); late_lyrics_and_selection(); local_retry(); lyric_deadlines(); }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
    std::cout << "MPRIS runtime checks passed\n";
}
