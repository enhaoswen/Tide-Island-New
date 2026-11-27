#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include <pipewire/extensions/metadata.h>
#include <pipewire/pipewire.h>
#include <spa/param/props.h>
#include <spa/pod/iter.h>
#include <spa/utils/json.h>
#include <sys/epoll.h>

#include "backend/sysinfo/source.hpp"
#include "backend/timer.hpp"
#include "utils/log.hpp"

using namespace std;

using namespace std::chrono_literals;

namespace {

struct Sink {
	uint32_t id_{};
	string   name_{};
};

pw_loop*     loop{};
pw_context*  context{};
pw_core*     core{};
pw_registry* registry{};
pw_metadata* metadata{};
pw_node*     node{};

spa_hook core_hook{};
spa_hook registry_hook{};
spa_hook metadata_hook{};
spa_hook node_hook{};

uint32_t metadata_id{SPA_ID_INVALID};
uint32_t node_id{SPA_ID_INVALID};

vector<Sink>              sinks{};
string                    default_sink{};
optional<SysInfo::Volume> current{};
bool                      has_pending_change{};
bool                      is_lost{};

void set_volume(optional<SysInfo::Volume> v) {
	if(current == v)
		return;
	current            = v;
	has_pending_change = true;
}

void on_node_param(void*, int, uint32_t id, uint32_t, uint32_t, const spa_pod* param) {
	if(id != SPA_PARAM_Props || !param || !spa_pod_is_object_type(param, SPA_TYPE_OBJECT_Props))
		return;

	array<float, 64> vols{};
	uint32_t         n{};
	bool             is_muted{current && current->is_muted_};

	const auto*         obj{reinterpret_cast<const spa_pod_object*>(param)};
	const spa_pod_prop* prop{};
	SPA_POD_OBJECT_FOREACH(obj, prop) {
		if(prop->key == SPA_PROP_channelVolumes)
			n = spa_pod_copy_array(&prop->value, SPA_TYPE_Float, vols.data(), vols.size());
		else if(prop->key == SPA_PROP_mute)
			spa_pod_get_bool(&prop->value, &is_muted);
	}
	// Nodes may emit several Props objects; only the one carrying volumes matters.
	if(n == 0)
		return;
	const float peak{*max_element(vols.begin(), vols.begin() + n)};
	set_volume(SysInfo::Volume{.level_ = cbrt(peak), .is_muted_ = is_muted});
}

const pw_node_events k_node_events{[] {
	pw_node_events e{};
	e.version = PW_VERSION_NODE_EVENTS;
	e.param   = on_node_param;
	return e;
}()};

void unbind_node() {
	if(!node)
		return;
	spa_hook_remove(&node_hook);
	pw_proxy_destroy(reinterpret_cast<pw_proxy*>(node));
	node    = nullptr;
	node_id = SPA_ID_INVALID;
}

void rebind_node() {
	const auto it{ranges::find(sinks, default_sink, &Sink::name_)};
	const auto id{it == sinks.end() ? SPA_ID_INVALID : it->id_};
	if(id == node_id)
		return;
	unbind_node();
	if(id == SPA_ID_INVALID) {
		set_volume(nullopt);
		return;
	}
	node = static_cast<pw_node*>(pw_registry_bind(registry, id, PW_TYPE_INTERFACE_Node, PW_VERSION_NODE, 0));
	if(!node)
		return;
	node_id = id;
	pw_node_add_listener(node, &node_hook, &k_node_events, nullptr);
	array<uint32_t, 1> ids{SPA_PARAM_Props};
	pw_node_subscribe_params(node, ids.data(), ids.size());
}

int on_metadata_property(void*, uint32_t subject, const char* key, const char*, const char* value) {
	if(subject != PW_ID_CORE || (key && strcmp(key, "default.audio.sink") != 0))
		return 0;
	// value is JSON: { "name": "alsa_output..." }
	array<char, 256> name{};
	if(!value || spa_json_str_object_find(value, strlen(value), "name", name.data(), name.size()) <= 0)
		name[0] = '\0';
	default_sink = name.data();
	rebind_node();
	return 0;
}

const pw_metadata_events k_metadata_events{
	.version  = PW_VERSION_METADATA_EVENTS,
	.property = on_metadata_property,
};

void on_global(void*, uint32_t id, uint32_t, const char* type, uint32_t, const spa_dict* props) {
	if(!props)
		return;
	if(strcmp(type, PW_TYPE_INTERFACE_Node) == 0) {
		const char* cls{spa_dict_lookup(props, PW_KEY_MEDIA_CLASS)};
		const char* name{spa_dict_lookup(props, PW_KEY_NODE_NAME)};
		if(!cls || !name || strcmp(cls, "Audio/Sink") != 0)
			return;
		sinks.push_back({id, name});
		if(default_sink == name)
			rebind_node();
	} else if(strcmp(type, PW_TYPE_INTERFACE_Metadata) == 0 && !metadata) {
		const char* name{spa_dict_lookup(props, PW_KEY_METADATA_NAME)};
		if(!name || strcmp(name, "default") != 0)
			return;
		metadata = static_cast<pw_metadata*>(
			pw_registry_bind(registry, id, PW_TYPE_INTERFACE_Metadata, PW_VERSION_METADATA, 0));
		if(!metadata)
			return;
		metadata_id = id;
		pw_metadata_add_listener(metadata, &metadata_hook, &k_metadata_events, nullptr);
	}
}

void on_global_remove(void*, uint32_t id) {
	erase_if(sinks, [id](const Sink& s) { return s.id_ == id; });
	if(id == node_id)
		rebind_node();
	if(id == metadata_id) {
		spa_hook_remove(&metadata_hook);
		pw_proxy_destroy(reinterpret_cast<pw_proxy*>(metadata));
		metadata    = nullptr;
		metadata_id = SPA_ID_INVALID;
	}
}

const pw_registry_events k_registry_events{
	.version       = PW_VERSION_REGISTRY_EVENTS,
	.global        = on_global,
	.global_remove = on_global_remove,
};

void on_core_error(void*, uint32_t id, int, int res, const char* message) {
	Log::logger(Log::LogLevel::Warning, "SysInfo: PipeWire error id={} res={} message=\"{}\"", id, res, message);
	if(id == PW_ID_CORE && res == -EPIPE)
		is_lost = true;
}

const pw_core_events k_core_events{[] {
	pw_core_events e{};
	e.version = PW_VERSION_CORE_EVENTS;
	e.error   = on_core_error;
	return e;
}()};

void disconnect() {
	unbind_node();
	if(metadata) {
		spa_hook_remove(&metadata_hook);
		pw_proxy_destroy(reinterpret_cast<pw_proxy*>(metadata));
		metadata = nullptr;
	}
	if(registry) {
		spa_hook_remove(&registry_hook);
		pw_proxy_destroy(reinterpret_cast<pw_proxy*>(registry));
		registry = nullptr;
	}
	if(core) {
		spa_hook_remove(&core_hook);
		pw_core_disconnect(core);
		core = nullptr;
	}
	metadata_id = SPA_ID_INVALID;
	sinks.clear();
	default_sink.clear();
	set_volume(nullopt);
}

bool connect() {
	core = pw_context_connect(context, nullptr, 0);
	if(!core)
		return false;
	pw_core_add_listener(core, &core_hook, &k_core_events, nullptr);
	registry = pw_core_get_registry(core, PW_VERSION_REGISTRY, 0);
	pw_registry_add_listener(registry, &registry_hook, &k_registry_events, nullptr);
	return true;
}

void schedule_reconnect();

void try_reconnect() {
	if(!context || core)
		return;
	if(connect())
		Log::logger(Log::LogLevel::Debug, "SysInfo: PipeWire reconnected");
	else
		schedule_reconnect();
}

void schedule_reconnect() {
	Timer::push(2s, try_reconnect, 0.f);
}

}

namespace SysInfo::Pipewire {

void init() {
	pw_init(nullptr, nullptr);
	loop = pw_loop_new(nullptr);
	if(!loop)
		return;
	// Only the protocol and metadata marshalling are needed; the rest of client.conf
	// (rt, client-node, adapter, ...) would just cost memory.
	context = pw_context_new(loop,
		pw_properties_new("module.rt",
			"false",
			"module.client-node",
			"false",
			"module.client-device",
			"false",
			"module.adapter",
			"false",
			"module.session-manager",
			"false",
			nullptr),
		0);
	if(!context) {
		shutdown();
		return;
	}
	pw_loop_enter(loop);
	if(!Detail::watch(pw_loop_get_fd(loop), Detail::Tag::Pipewire, EPOLLIN)) {
		shutdown();
		return;
	}
	if(!connect()) {
		Log::logger(Log::LogLevel::Warning, "SysInfo: PipeWire unavailable, retrying");
		schedule_reconnect();
	}
}

void shutdown() {
	disconnect();
	if(context)
		pw_context_destroy(context);
	if(loop) {
		Detail::unwatch(pw_loop_get_fd(loop));
		pw_loop_leave(loop);
		pw_loop_destroy(loop);
	}
	context = nullptr;
	loop    = nullptr;
	current.reset();
	pw_deinit();
}

bool dispatch() {
	if(!loop)
		return false;
	has_pending_change = false;
	pw_loop_iterate(loop, 0);
	// Proxies cannot be torn down from inside their own callbacks.
	if(is_lost) {
		is_lost = false;
		disconnect();
		schedule_reconnect();
	}
	return has_pending_change;
}

optional<Volume> get_volume() {
	return current;
}

}
