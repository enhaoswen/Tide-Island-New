#include "backend/egl_vendor.hpp"
#include "utils/log.hpp"

#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <wayland-client.h>
#include <wayland-egl.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <fcntl.h>
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

using Clock = std::chrono::steady_clock;
constexpr auto probe_timeout = std::chrono::milliseconds(1200);
constexpr auto selection_timeout = std::chrono::seconds(3);
constexpr const char* vendor_files_env = "__EGL_VENDOR_LIBRARY_FILENAMES";

struct Identity {
    std::array<char, 256> egl_vendor{};
    std::array<char, 256> gl_vendor{};
    std::array<char, 256> renderer{};
    std::array<char, 256> version{};

    bool operator==(const Identity&) const = default;
};

bool copy_string(std::array<char, 256>& destination, const char* source) {
    if (!source || std::strlen(source) >= destination.size()) {
        return false;
    }
    std::strcpy(destination.data(), source);
    return true;
}

void registry_global(void* data, wl_registry* registry, uint32_t name,
                     const char* interface, uint32_t version) {
    auto& compositor = *static_cast<wl_compositor**>(data);
    if (!compositor && std::string_view(interface) == wl_compositor_interface.name) {
        compositor = static_cast<wl_compositor*>(
            wl_registry_bind(registry, name, &wl_compositor_interface, std::min(version, 1U)));
    }
}

void registry_remove(void*, wl_registry*, uint32_t) {}

std::optional<Identity> query_renderer() {
    auto* const display = wl_display_connect(nullptr);
    if (!display) {
        return std::nullopt;
    }
    auto* const registry = wl_display_get_registry(display);
    wl_compositor* compositor{};
    constexpr wl_registry_listener listener{registry_global, registry_remove};
    if (!registry || wl_registry_add_listener(registry, &listener, &compositor) < 0 ||
        wl_display_roundtrip(display) < 0 || !compositor) {
        return std::nullopt;
    }
    auto* const surface = wl_compositor_create_surface(compositor);
    if (!surface) {
        return std::nullopt;
    }
    auto* const window = wl_egl_window_create(surface, 1, 1);
    if (!window) {
        return std::nullopt;
    }

    const auto egl_display = eglGetDisplay(reinterpret_cast<EGLNativeDisplayType>(display));
    if (egl_display == EGL_NO_DISPLAY || !eglInitialize(egl_display, nullptr, nullptr) ||
        !eglBindAPI(EGL_OPENGL_ES_API)) {
        return std::nullopt;
    }
    // Match the real backend's window config and context, including alpha.
    constexpr EGLint config_attributes[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_NONE,
    };
    EGLConfig config{};
    EGLint count{};
    if (!eglChooseConfig(egl_display, config_attributes, &config, 1, &count) || count == 0) {
        return std::nullopt;
    }
    constexpr EGLint context_attributes[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    const auto context = eglCreateContext(egl_display, config, EGL_NO_CONTEXT, context_attributes);
    if (context == EGL_NO_CONTEXT) {
        return std::nullopt;
    }
    const auto egl_surface = eglCreateWindowSurface(
        egl_display, config, reinterpret_cast<EGLNativeWindowType>(window), nullptr);
    if (egl_surface == EGL_NO_SURFACE ||
        !eglMakeCurrent(egl_display, egl_surface, egl_surface, context)) {
        return std::nullopt;
    }

    Identity result{};
    if (!copy_string(result.egl_vendor, eglQueryString(egl_display, EGL_VENDOR)) ||
        !copy_string(result.gl_vendor, reinterpret_cast<const char*>(glGetString(GL_VENDOR))) ||
        !copy_string(result.renderer, reinterpret_cast<const char*>(glGetString(GL_RENDERER))) ||
        !copy_string(result.version, reinterpret_cast<const char*>(glGetString(GL_VERSION)))) {
        return std::nullopt;
    }
    // This runs only in a disposable child. _exit closes its connection and
    // releases all driver resources without running potentially slow destructors.
    return result;
}

std::optional<Identity> probe(const std::string& vendor_file, Clock::time_point deadline) {
    if (Clock::now() >= deadline) {
        return std::nullopt;
    }
    int descriptors[2]{};
    if (pipe2(descriptors, O_CLOEXEC | O_NONBLOCK) != 0) {
        return std::nullopt;
    }
    const auto child = fork();
    if (child == 0) {
        close(descriptors[0]);
        // A failed candidate should not pollute the application's own log.
        const auto null_fd = open("/dev/null", O_WRONLY | O_CLOEXEC);
        if (null_fd >= 0) {
            dup2(null_fd, STDERR_FILENO);
            dup2(null_fd, STDOUT_FILENO);
            close(null_fd);
        }
        if (!vendor_file.empty() && setenv(vendor_files_env, vendor_file.c_str(), 1) != 0) {
            _exit(1);
        }
        const auto identity = query_renderer();
        if (!identity) {
            _exit(1);
        }
        ssize_t written{};
        do {
            written = write(descriptors[1], &*identity, sizeof(Identity));
        } while (written < 0 && errno == EINTR);
        _exit(written == sizeof(Identity) ? 0 : 1);
    }
    close(descriptors[1]);
    if (child < 0) {
        close(descriptors[0]);
        return std::nullopt;
    }

    Identity result{};
    size_t received{};
    while (received < sizeof(result)) {
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now());
        if (remaining.count() <= 0) {
            break;
        }
        pollfd descriptor{descriptors[0], POLLIN, 0};
        const auto ready = poll(&descriptor, 1, static_cast<int>(remaining.count()));
        if (ready < 0 && errno == EINTR) {
            continue;
        }
        if (ready <= 0) {
            break;
        }
        const auto count = read(descriptors[0], reinterpret_cast<char*>(&result) + received,
                                sizeof(result) - received);
        if (count < 0 && (errno == EINTR || errno == EAGAIN)) {
            continue;
        }
        if (count <= 0) {
            break;
        }
        received += static_cast<size_t>(count);
    }
    close(descriptors[0]);

    int status{};
    pid_t reaped{};
    do {
        reaped = waitpid(child, &status, WNOHANG);
        if (reaped == 0 && Clock::now() < deadline) {
            poll(nullptr, 0, 1);
        }
    } while ((reaped == 0 && Clock::now() < deadline) || (reaped < 0 && errno == EINTR));
    if (reaped == 0) {
        kill(child, SIGKILL);
        do {
            reaped = waitpid(child, &status, 0);
        } while (reaped < 0 && errno == EINTR);
        return std::nullopt;
    }
    if (reaped != child || !WIFEXITED(status) || WEXITSTATUS(status) != 0 || received != sizeof(result)) {
        return std::nullopt;
    }
    return result;
}

std::vector<std::string> vendor_files() {
    std::vector<std::string> result{};
    // GLVND's standard discovery directories. Custom discovery settings are
    // respected by skipping automatic selection altogether.
    for (const auto* directory : {"/etc/glvnd/egl_vendor.d", "/usr/share/glvnd/egl_vendor.d"}) {
        std::error_code error{};
        std::vector<std::string> files{};
        std::filesystem::directory_iterator entry(directory, error), end;
        while (!error && entry != end) {
            if (entry->is_regular_file(error) && entry->path().extension() == ".json") {
                auto path = entry->path().string();
                if (path.find(':') == std::string::npos) {
                    files.push_back(std::move(path));
                }
            }
            entry.increment(error);
        }
        std::sort(files.begin(), files.end());
        result.insert(result.end(), files.begin(), files.end());
    }
    return result;
}

} // namespace

void EglVendor::select_for_wayland() {
    const auto* auto_select = std::getenv("TIDE_EGL_AUTO_VENDOR");
    if ((auto_select && std::string_view(auto_select) == "0") ||
        std::getenv(vendor_files_env) || std::getenv("__EGL_VENDOR_LIBRARY_DIRS") ||
        !std::getenv("WAYLAND_DISPLAY") || getuid() != geteuid() || getgid() != getegid()) {
        return;
    }
    const auto files = vendor_files();
    if (files.size() < 2) {
        return;
    }

    // Probing EGL in the parent would retain every enumerated vendor and its
    // compiler dependencies. Fork before threads/EGL exist, keeping those
    // allocations in short-lived children instead.
    const auto deadline = Clock::now() + selection_timeout;
    const auto original = probe({}, std::min(deadline, Clock::now() + probe_timeout));
    if (!original) {
        return;
    }
    for (const auto& file : files) {
        const auto candidate = probe(file, std::min(deadline, Clock::now() + probe_timeout));
        if (candidate && *candidate == *original) {
            if (setenv(vendor_files_env, file.c_str(), 0) == 0) {
                Log::logger(Log::Debug, "Selected EGL vendor file=\"{}\", renderer=\"{}\"",
                            file, original->renderer.data());
            }
            return;
        }
        if (Clock::now() >= deadline) {
            break;
        }
    }
    // An unavailable or unrecognized setup retains EGL's normal selection.
}
