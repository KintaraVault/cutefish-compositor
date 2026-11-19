// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shell/menubar/media_spectrum.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <complex>
#include <fstream>
#include <map>
#include <numbers>
#include <utility>

#include <sys/epoll.h>
#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>

#include "base/event_loop.h"
#include "base/timer.h"
#include "core/main_loop.h"

namespace cutefish::shell {
namespace {

using Clock = std::chrono::steady_clock;
constexpr uint32_t kRate = 48000;
constexpr size_t kFrames = 2048;

uint32_t number(const spa_dict *props, const char *key)
{
    uint32_t value = 0;
    if (const char *str = props ? spa_dict_lookup(props, key) : nullptr) {
        const std::string_view text(str);
        const auto end = text.data() + text.size();
        const auto result = std::from_chars(text.data(), end, value);
        if (result.ec != std::errc() || result.ptr != end)
            return 0;
    }
    return value;
}

// Web browsers can own their MPRIS name in the main process but emit audio
// from a child. Do not fall back to any stream that isn't from this process tree.
bool belongsTo(uint32_t pid, uint32_t owner)
{
    for (int depth = 0; pid > 1 && depth < 16; ++depth) {
        if (pid == owner)
            return true;
        std::ifstream proc("/proc/" + std::to_string(pid) + "/stat");
        std::string stat;
        if (!std::getline(proc, stat))
            break;
        const auto end = stat.rfind(')');
        if (end == std::string::npos || end + 4 >= stat.size())
            break;
        const auto result = std::from_chars(stat.data() + end + 4, stat.data() + stat.size(), pid);
        if (result.ec != std::errc())
            break;
    }
    return false;
}

// FFT on a Hann-windowed stereo block. Pack L + iR; summing the powers at
// symmetric bins equals the power of both channels, even for opposite phases.
MediaBands analyze(std::array<std::complex<double>, kFrames> &bins)
{
    static const auto hann = [] {
        std::array<double, kFrames> window;
        for (size_t i = 0; i < kFrames; ++i)
            window[i] = .5 - .5 * std::cos(2 * std::numbers::pi * i / (kFrames - 1));
        return window;
    }();
    for (size_t i = 0; i < kFrames; ++i)
        bins[i] *= hann[i];
    for (size_t i = 1, j = 0; i < kFrames; ++i) {
        size_t bit = kFrames >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(bins[i], bins[j]);
    }
    for (size_t size = 2; size <= kFrames; size *= 2) {
        const auto step = std::polar(1., -2 * std::numbers::pi / size);
        for (size_t i = 0; i < kFrames; i += size) {
            std::complex<double> weight(1);
            for (size_t j = 0; j < size / 2; ++j) {
                const auto even = bins[i + j];
                const auto odd = bins[i + j + size / 2] * weight;
                bins[i + j] = even + odd;
                bins[i + j + size / 2] = even - odd;
                weight *= step;
            }
        }
    }
    constexpr std::array edges {45, 140, 330, 800, 2000, 5000, 14000};
    MediaBands result {};
    for (size_t i = 0; i < result.size(); ++i) {
        double power = 0;
        for (int j = (edges[i] * kFrames + kRate - 1) / kRate;
             j < int((edges[i + 1] * kFrames + kRate - 1) / kRate); ++j) {
            power += std::norm(bins[j]) + std::norm(bins[kFrames - j]);
        }
        const double db = 10 * std::log10(std::max(1e-12, power * 8 / (kFrames * kFrames)));
        result[i] = std::clamp((db + 53) / 43, 0., 1.);
    }
    return result;
}

} // namespace

struct MediaSpectrum::Tap {
    struct Client {
        Tap *tap;
        uint32_t pid;
        pw_client *proxy = nullptr;
        spa_hook hook {};
        ~Client() {
            if (proxy) {
                spa_hook_remove(&hook);
                pw_proxy_destroy(reinterpret_cast<pw_proxy *>(proxy));
            }
        }
    };
    struct Node { uint32_t client; std::string serial; };

    Tap() : retry(core::mainLoop())
    {
        pw_init(nullptr, nullptr);
        retry.setSingleShot(true);
        retry.onTimeout = [this] { if (pid) connect(); };
    }

    ~Tap()
    {
        retry.stop();
        disconnect();
        pw_deinit();
    }

    void connect()
    {
        if (core)
            return;
        loop = pw_loop_new(nullptr);
        if (loop) {
            pw_loop_enter(loop);
            context = pw_context_new(loop, nullptr, 0);
        }
        if (context)
            core = pw_context_connect(context, pw_properties_new(PW_KEY_APP_NAME, "Cutefish Media", nullptr), 0);
        if (core)
            registry = pw_core_get_registry(core, PW_VERSION_REGISTRY, 0);
        if (!registry) {
            disconnect();
            retry.start(std::chrono::seconds(2));
            return;
        }
        static const pw_core_events coreEvents = [] {
            pw_core_events e {};
            e.version = PW_VERSION_CORE_EVENTS;
            e.error = [](void *data, uint32_t id, int, int result, const char *) {
                if (id == PW_ID_CORE && (result == -EPIPE || result == -ECONNRESET))
                    static_cast<Tap *>(data)->failed = true;
            };
            return e;
        }();
        pw_core_add_listener(core, &coreHook, &coreEvents, this);
        static const pw_registry_events events {
            .version = PW_VERSION_REGISTRY_EVENTS,
            .global = [](void *data, uint32_t id, uint32_t, const char *type, uint32_t version, const spa_dict *props) {
                static_cast<Tap *>(data)->add(id, type, version, props);
            },
            .global_remove = [](void *data, uint32_t id) {
                auto &tap = *static_cast<Tap *>(data);
                const auto removed = tap.nodes.erase(id) + tap.clients.erase(id);
                tap.dirty |= removed != 0;
            },
        };
        pw_registry_add_listener(registry, &registryHook, &events, this);
        watch = core::mainLoop()->watch(pw_loop_get_fd(loop), EPOLLIN, [this](uint32_t) {
            const int result = pw_loop_iterate(loop, 0);
            if (failed || (result < 0 && result != -EINTR)) {
                disconnect();
                retry.start(std::chrono::seconds(2));
            } else if (dirty) {
                dirty = false;
                retarget();
            }
        });
    }

    void disconnect()
    {
        if (watch)
            core::mainLoop()->unwatch(std::exchange(watch, 0));
        resetStream();
        nodes.clear();
        clients.clear();
        if (registry) {
            spa_hook_remove(&registryHook);
            pw_proxy_destroy(reinterpret_cast<pw_proxy *>(registry));
            registry = nullptr;
            spa_hook_remove(&coreHook);
        }
        if (core)
            pw_core_disconnect(std::exchange(core, nullptr));
        if (context)
            pw_context_destroy(std::exchange(context, nullptr));
        if (loop) {
            pw_loop_leave(loop);
            pw_loop_destroy(std::exchange(loop, nullptr));
        }
        failed = false;
    }

    void add(uint32_t id, std::string_view type, uint32_t version, const spa_dict *props)
    {
        if (!props)
            return;
        if (type == PW_TYPE_INTERFACE_Client) {
            auto client = std::make_unique<Client>();
            client->tap = this;
            client->pid = number(props, PW_KEY_SEC_PID);
            // pipewire-pulse owns the remote socket. The original PID is in
            // the client's full properties, not necessarily in its globals.
            const char *api = spa_dict_lookup(props, PW_KEY_CLIENT_API);
            if (api && std::string_view(api) == "pipewire-pulse") {
                client->pid = 0;
                client->proxy = static_cast<pw_client *>(pw_registry_bind(registry, id, PW_TYPE_INTERFACE_Client,
                                                                         std::min(version, uint32_t(PW_VERSION_CLIENT)), 0));
                static const pw_client_events events {
                    .version = PW_VERSION_CLIENT_EVENTS,
                    .info = [](void *data, const pw_client_info *info) {
                        auto &client = *static_cast<Client *>(data);
                        if (info->change_mask & PW_CLIENT_CHANGE_MASK_PROPS) {
                            client.pid = number(info->props, PW_KEY_APP_PROCESS_ID);
                            client.tap->dirty = true;
                        }
                    },
                };
                if (client->proxy)
                    pw_client_add_listener(client->proxy, &client->hook, &events, client.get());
            }
            clients[id] = std::move(client);
            dirty = true;
        } else if (type == PW_TYPE_INTERFACE_Node) {
            const auto *media = spa_dict_lookup(props, PW_KEY_MEDIA_CLASS);
            const auto *serial = spa_dict_lookup(props, PW_KEY_OBJECT_SERIAL);
            if (media && serial && std::string_view(media) == "Stream/Output/Audio") {
                nodes[id] = {number(props, PW_KEY_CLIENT_ID), serial};
                dirty = true;
            }
        }
    }

    void resetStream()
    {
        if (stream) {
            spa_hook_remove(&streamHook);
            pw_stream_destroy(std::exchange(stream, nullptr));
        }
        serial.clear();
        filled = 0;
        sampleTime = {};
    }

    void retarget()
    {
        const Node *selected = nullptr;
        if (pid) {
            for (const auto &[id, node] : nodes) {
                const auto client = clients.find(node.client);
                if (client == clients.end() || !belongsTo(client->second->pid, pid))
                    continue;
                if (node.serial == serial || client->second->pid == pid) {
                    selected = &node;
                    break;
                }
                selected = &node;
            }
        }
        if (selected && selected->serial == serial)
            return;
        resetStream();
        if (!selected)
            return;
        serial = selected->serial;
        stream = pw_stream_new(core, "Media spectrum",
                pw_properties_new(PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY, "Capture",
                                  PW_KEY_NODE_NAME, "cutefish.internal.media-spectrum", PW_KEY_TARGET_OBJECT, serial.c_str(),
                                  PW_KEY_STREAM_MONITOR, "true", PW_KEY_NODE_PASSIVE, "true",
                                  "node.dont-fallback", "true", "node.dont-move", "true", PW_KEY_NODE_DONT_RECONNECT, "true", nullptr));
        if (!stream)
            return;
        static const pw_stream_events events = [] {
            pw_stream_events e {};
            e.version = PW_VERSION_STREAM_EVENTS;
            e.process = [](void *data) { static_cast<Tap *>(data)->process(); };
            return e;
        }();
        pw_stream_add_listener(stream, &streamHook, &events, this);
        std::array<uint8_t, 512> storage;
        spa_pod_builder builder = SPA_POD_BUILDER_INIT(storage.data(), storage.size());
        spa_audio_info_raw info {};
        info.format = SPA_AUDIO_FORMAT_F32;
        info.rate = kRate;
        info.channels = 2;
        info.position[0] = SPA_AUDIO_CHANNEL_FL;
        info.position[1] = SPA_AUDIO_CHANNEL_FR;
        const spa_pod *format = spa_format_audio_raw_build(&builder, SPA_PARAM_EnumFormat, &info);
        if (pw_stream_connect(stream, PW_DIRECTION_INPUT, PW_ID_ANY,
                              pw_stream_flags(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS), &format, 1) < 0)
            resetStream();
    }

    void process()
    {
        while (auto *buffer = pw_stream_dequeue_buffer(stream)) {
            const auto *b = buffer->buffer;
            if (b->n_datas) {
                const auto &d = b->datas[0];
                constexpr size_t stride = 2 * sizeof(float);
                if (d.data && d.chunk && d.chunk->offset < d.maxsize) {
                    const auto *pcm = static_cast<const float *>(d.data) + d.chunk->offset / sizeof(float);
                    const size_t frames = std::min(d.chunk->size, d.maxsize - d.chunk->offset) / stride;
                    const bool silent = d.chunk->flags & (SPA_CHUNK_FLAG_EMPTY | SPA_CHUNK_FLAG_CORRUPTED);
                    for (size_t i = 0; i < frames; ++i) {
                        samples[filled++] = silent ? std::complex<double>() : std::complex<double>(pcm[2*i], pcm[2*i+1]);
                        if (filled == kFrames) {
                            bands = analyze(samples);
                            sampleTime = Clock::now();
                            filled = 0;
                        }
                    }
                }
            }
            pw_stream_queue_buffer(stream, buffer);
        }
    }

    uint32_t pid = 0;
    pw_loop *loop = nullptr;
    pw_context *context = nullptr;
    pw_core *core = nullptr;
    pw_registry *registry = nullptr;
    pw_stream *stream = nullptr;
    spa_hook coreHook {}, registryHook {}, streamHook {};
    int watch = 0;
    base::Timer retry;
    bool dirty = false;
    bool failed = false;
    std::map<uint32_t, std::unique_ptr<Client>> clients;
    std::map<uint32_t, Node> nodes;
    std::string serial;
    size_t filled = 0;
    std::array<std::complex<double>, kFrames> samples;
    MediaBands bands {};
    Clock::time_point sampleTime;
};

MediaSpectrum::MediaSpectrum() = default;
MediaSpectrum::~MediaSpectrum() = default;

void MediaSpectrum::setPlayer(uint32_t pid)
{
    if (!m_tap && pid)
        m_tap = std::make_unique<Tap>();
    if (!m_tap || m_tap->pid == pid)
        return;
    m_tap->pid = pid;
    if (pid)
        m_tap->connect();
    m_tap->retarget();
}

MediaBands MediaSpectrum::levels() const
{
    return m_tap && Clock::now() - m_tap->sampleTime < std::chrono::milliseconds(250) ? m_tap->bands : MediaBands {};
}

} // namespace cutefish::shell
