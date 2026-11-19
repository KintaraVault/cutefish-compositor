// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shell/menubar/mpris.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "base/strings.h"

namespace cutefish::shell {

namespace dbus = base::dbus;

static constexpr const char *kPrefix = "org.mpris.MediaPlayer2.";
static constexpr const char *kPath = "/org/mpris/MediaPlayer2";
static constexpr const char *kRootInterface = "org.mpris.MediaPlayer2";
static constexpr const char *kPlayerInterface = "org.mpris.MediaPlayer2.Player";

MprisPlayer::MprisPlayer(dbus::Connection *bus, std::string service)
    : m_bus(bus)
    , m_service(std::move(service))
    , m_root(bus, m_service, kPath, kRootInterface)
    , m_player(bus, m_service, kPath, kPlayerInterface)
    , m_sender(bus, m_service)
{
    m_positionPoll.onTimeout = [this] { refreshPosition(); };
    if (m_bus) {
        const dbus::Proxy daemon(m_bus, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus");
        m_calls.track([&](std::function<void()> finished) {
            return daemon.callAsync("GetConnectionUnixProcessID", [this, finished](const dbus::Reply &reply) {
                m_processId = reply.value<uint32_t>().value_or(0);
                changed();
                finished();
            }, m_service);
        });
        dbus::MatchRule rule;
        rule.sender = m_service;
        rule.path = kPath;
        rule.interface = "org.freedesktop.DBus.Properties";
        rule.member = "PropertiesChanged";
        m_matches.push_back(m_bus->subscribe(rule, [this](const dbus::Message &message) {
            std::string interface;
            dbus::VariantMap changed;
            std::vector<std::string> invalidated;
            if (!m_sender.accepts(message) || !message.read(interface, changed, invalidated))
                return;
            if (interface != kRootInterface && interface != kPlayerInterface)
                return;
            apply(interface, changed);
            if (!invalidated.empty())
                load(interface);
        }));
        rule.interface = kPlayerInterface;
        rule.member = "Seeked";
        m_matches.push_back(m_bus->subscribe(rule, [this](const dbus::Message &message) {
            int64_t position;
            if (m_sender.accepts(message) && message.read(position) && !isStaleSeekPosition(position / 1e6)) {
                const bool ended = std::exchange(m_atEnd, false);
                m_nearEndPosition.reset();
                samplePosition(std::max(int64_t(0), position) / 1e6);
                if (ended)
                    changed();
            }
        }));
    }
    load(kRootInterface);
    load(kPlayerInterface);
}

MprisPlayer::~MprisPlayer() = default;

void MprisPlayer::load(const std::string &interface)
{
    const dbus::Proxy &proxy = interface == kRootInterface ? m_root : m_player;
    m_calls.track([&](std::function<void()> finished) {
        return proxy.getAllAsync([this, interface, finished](const dbus::Reply &reply) {
            if (interface == kRootInterface)
                m_rootReady = true;
            else
                m_playerReady = true;
            apply(interface, reply.value<dbus::VariantMap>().value_or(dbus::VariantMap()));
            finished();
        });
    });
}

void MprisPlayer::apply(const std::string &interface, const dbus::VariantMap &properties)
{
    const bool root = interface == kRootInterface;
    dbus::VariantMap &stored = root ? m_rootProperties : m_playerProperties;
    const PlaybackStatus status = playbackStatus();
    const auto oldTrack = trackId();
    const auto oldTitle = title();
    const auto oldDuration = duration();
    const auto oldPosition = position();
    for (const auto &[name, value] : properties) {
        stored[name] = value;
        if (!root && name == "Metadata")
            m_metadata = value.value<dbus::VariantMap>();
    }
    if (!root) {
        const auto currentStatus = playbackStatus();
        const bool trackChanged = oldTrack != trackId() || oldDuration != duration()
                || (!hasTrackPath() && oldTitle != title());
        if (trackChanged || currentStatus == PlaybackStatus::Stopped) {
            m_seekRequest.reset();
            m_seekGuardUntil = {};
        }
        if (trackChanged || currentStatus == PlaybackStatus::Stopped
                || (currentStatus == PlaybackStatus::Playing && status != currentStatus)) {
            m_atEnd = false;
            m_nearEndPosition.reset();
        }
        if (const auto pos = properties.find("Position"); pos != properties.end()) {
            const auto micros = pos->second.get<int64_t>();
            if (!isStaleSeekPosition(micros.value_or(0) / 1e6))
                samplePosition(micros && *micros >= 0 ? std::optional<double>(*micros / 1e6) : std::nullopt);
        } else if (currentStatus == PlaybackStatus::Stopped || trackChanged) {
            samplePosition(std::nullopt);
        } else if (status != currentStatus || properties.contains("Rate")) {
            samplePosition(oldPosition);
        }
        if (currentStatus == PlaybackStatus::Playing) {
            if (!m_positionPoll.isActive() && m_bus)
                m_positionPoll.start(std::chrono::milliseconds(750));
        } else {
            m_positionPoll.stop();
        }
        if (currentStatus != PlaybackStatus::Stopped && (trackChanged || status != currentStatus))
            refreshPosition();
    }
    changed();
    if (playbackStatus() != status)
        playbackStatusChanged();
}

static std::string stringOf(const dbus::VariantMap &properties, const char *name)
{
    const auto it = properties.find(name);
    return it == properties.end() ? std::string() : it->second.value<std::string>();
}

static bool flagOf(const dbus::VariantMap &properties, const char *name, bool fallback)
{
    const auto it = properties.find(name);
    return it == properties.end() ? fallback : it->second.value<bool>(fallback);
}

std::string MprisPlayer::identity() const
{
    return stringOf(m_rootProperties, "Identity");
}

MprisPlayer::PlaybackStatus MprisPlayer::playbackStatus() const
{
    const std::string status = stringOf(m_playerProperties, "PlaybackStatus");
    if (status == "Playing")
        return PlaybackStatus::Playing;
    if (status == "Paused")
        return PlaybackStatus::Paused;
    return PlaybackStatus::Stopped;
}

bool MprisPlayer::playerFlag(const char *name, bool fallback) const
{
    return flagOf(m_playerProperties, name, fallback);
}

bool MprisPlayer::canControl() const
{
    return playerFlag("CanControl");
}

bool MprisPlayer::canGoNext() const
{
    return playerFlag("CanGoNext");
}

bool MprisPlayer::canGoPrevious() const
{
    return playerFlag("CanGoPrevious");
}

bool MprisPlayer::canPlay() const
{
    return playerFlag("CanPlay");
}

bool MprisPlayer::canPause() const
{
    return playerFlag("CanPause");
}

std::string MprisPlayer::title() const
{
    return stringOf(m_metadata, "xesam:title");
}

std::string MprisPlayer::artist() const
{
    const auto it = m_metadata.find("xesam:artist");
    if (it == m_metadata.end())
        return {};
    // A list by the spec; some players send one string.
    if (auto artists = it->second.get<std::vector<std::string>>())
        return base::strings::join(*artists, ", ");
    return it->second.value<std::string>();
}

std::string MprisPlayer::artUrl() const
{
    return stringOf(m_metadata, "mpris:artUrl");
}

dbus::ObjectPath MprisPlayer::trackId() const
{
    const auto it = m_metadata.find("mpris:trackid");
    if (it == m_metadata.end())
        return {};
    if (const auto path = it->second.get<dbus::ObjectPath>())
        return *path;
    return {it->second.value<std::string>()};
}

int64_t MprisPlayer::durationMicros() const
{
    const auto it = m_metadata.find("mpris:length");
    if (it == m_metadata.end())
        return 0;
    if (const auto length = it->second.get<int64_t>())
        return std::max(int64_t(0), *length);
    // Spotify sends an unsigned duration, although MPRIS specifies int64.
    const auto length = it->second.value<uint64_t>();
    return length <= uint64_t(std::numeric_limits<int64_t>::max()) ? int64_t(length) : 0;
}

double MprisPlayer::duration() const
{
    return durationMicros() / 1e6;
}

void MprisPlayer::samplePosition(std::optional<double> seconds)
{
    ++m_positionRevision;
    m_sampleTime = std::chrono::steady_clock::now();
    m_position = seconds;
}

std::optional<double> MprisPlayer::position() const
{
    if (!m_position || playbackStatus() == PlaybackStatus::Stopped)
        return std::nullopt;
    double elapsed = *m_position;
    if (playbackStatus() == PlaybackStatus::Playing && !m_atEnd) {
        const auto it = m_playerProperties.find("Rate");
        const double rate = it == m_playerProperties.end() ? 1 : it->second.value<double>(1);
        if (std::isfinite(rate))
            elapsed += rate * std::chrono::duration<double>(std::chrono::steady_clock::now() - m_sampleTime).count();
    }
    return duration() > 0 ? std::clamp(elapsed, 0., duration()) : std::max(0., elapsed);
}

void MprisPlayer::observePosition(double seconds)
{
    if (isStaleSeekPosition(seconds))
        return;
    const bool wasAtEnd = m_atEnd;
    const bool wasSeekable = canSetPosition();
    const double length = duration();
    const bool atLimit = length > 0 && seconds >= length;
    const bool nearEnd = length > 0 && seconds >= length - std::min(.25, length / 10);
    // A player normally signals a track change or Stopped. Some leave the
    // old metadata/status at the end; confirm with real samples, not merely
    // the projected clock reaching the duration while buffering.
    if (playbackStatus() == PlaybackStatus::Playing) {
        m_atEnd = nearEnd && m_nearEndPosition && std::abs(seconds - *m_nearEndPosition) < .015;
        m_nearEndPosition = nearEnd ? std::optional<double>(seconds) : std::nullopt;
    } else {
        m_atEnd = atLimit;
        m_nearEndPosition.reset();
    }
    samplePosition(m_atEnd ? length : seconds);
    if (wasAtEnd != m_atEnd || wasSeekable != canSetPosition())
        changed();
}

bool MprisPlayer::isStaleSeekPosition(double seconds) const
{
    // Some players acknowledge SetPosition before Position reflects the seek.
    // Keep a brief window for the player to catch up, but still reconcile if
    // the seek was silently ignored or the player landed somewhere else.
    if (m_seekRequest)
        return true;
    if (std::chrono::steady_clock::now() >= m_seekGuardUntil)
        return false;
    const auto expected = position();
    return expected && std::abs(seconds - *expected) > .35;
}

void MprisPlayer::refreshPosition()
{
    if (!m_bus || m_seekRequest || playbackStatus() == PlaybackStatus::Stopped)
        return;
    if (m_readingPosition) {
        m_readPositionAgain = true;
        return;
    }
    const auto revision = m_positionRevision;
    m_readingPosition = true;
    m_calls.track([&](std::function<void()> finished) {
        return m_player.getPropertyAsync("Position", [this, revision, finished](const dbus::Reply &reply) {
            m_readingPosition = false;
            if (!m_seekRequest && revision == m_positionRevision) {
                const auto value = reply.value<dbus::Variant>();
                const auto micros = value ? value->get<int64_t>() : std::nullopt;
                if (micros && *micros >= 0)
                    observePosition(*micros / 1e6);
            }
            if (std::exchange(m_readPositionAgain, false))
                refreshPosition();
            finished();
        });
    });
}

bool MprisPlayer::hasTrackPath() const
{
    const auto path = trackId().path;
    return path.size() > 1 && path.front() == '/' && path.back() != '/'
            && path.find("//") == std::string::npos && path != "/org/mpris/MediaPlayer2/TrackList/NoTrack"
            && path.find_first_not_of("/ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_") == std::string::npos;
}

bool MprisPlayer::canSetPosition() const
{
    return canControl() && playerFlag("CanSeek") && duration() > 0 && playbackStatus() != PlaybackStatus::Stopped
            && (hasTrackPath() || position().has_value());
}

void MprisPlayer::setPosition(double seconds)
{
    if (!canSetPosition() || !std::isfinite(seconds))
        return;
    const int64_t length = durationMicros();
    const int64_t micros = std::llround(std::clamp(static_cast<long double>(seconds) * 1e6L,
                                                  0.L, static_cast<long double>(length - 1)));
    const auto before = position();
    const bool ended = std::exchange(m_atEnd, false);
    m_nearEndPosition.reset();
    const auto request = ++m_seekSerial;
    const bool allowFallback = !m_seekRequest;
    m_seekRequest = request;
    m_seekGuardUntil = {};
    samplePosition(micros / 1e6);
    const bool absolute = hasTrackPath();
    sendSeek(request, micros, absolute && !allowFallback ? std::nullopt : before, absolute);
    if (ended)
        changed();
}

void MprisPlayer::sendSeek(uint64_t request, int64_t micros, std::optional<double> before, bool absolute)
{
    m_calls.track([&](std::function<void()> finished) {
        auto done = [this, request, micros, before, absolute, finished](const dbus::Reply &reply) {
            // Signals from an earlier seek may still be queued. Keep the
            // newest target until its method finishes, then read Position.
            if (m_seekRequest == request) {
                if (reply.isError() && absolute && before) {
                    sendSeek(request, micros, before, false);
                } else {
                    m_seekRequest.reset();
                    if (reply.isError())
                        samplePosition(before);
                    else
                        m_seekGuardUntil = std::chrono::steady_clock::now() + std::chrono::milliseconds(1100);
                    refreshPosition();
                }
            }
            finished();
        };
        if (absolute)
            return m_player.callAsync("SetPosition", std::move(done), trackId(), micros);
        const int64_t offset = micros - int64_t(std::llround(before.value_or(0) * 1e6));
        return m_player.callAsync("Seek", std::move(done), offset);
    });
}

void MprisPlayer::callPlayer(const char *method)
{
    if (canControl())
        m_player.send(method);
}

void MprisPlayer::play()
{
    callPlayer("Play");
}

void MprisPlayer::pause()
{
    callPlayer("Pause");
}

void MprisPlayer::playPause()
{
    callPlayer("PlayPause");
}

void MprisPlayer::stop()
{
    callPlayer("Stop");
}

void MprisPlayer::next()
{
    callPlayer("Next");
}

void MprisPlayer::previous()
{
    callPlayer("Previous");
}

static bool playing(const MprisPlayer &player)
{
    return player.playbackStatus() == MprisPlayer::PlaybackStatus::Playing;
}

MprisManager::MprisManager(dbus::Connection *bus)
    : m_bus(bus)
{
    if (!m_bus)
        return;

    dbus::MatchRule rule;
    rule.sender = "org.freedesktop.DBus";
    rule.path = "/org/freedesktop/DBus";
    rule.interface = "org.freedesktop.DBus";
    rule.member = "NameOwnerChanged";
    m_nameOwnerChanged = m_bus->subscribe(rule, [this](const dbus::Message &message) {
        std::string name, oldOwner, newOwner;
        if (!message.read(name, oldOwner, newOwner) || !name.starts_with(kPrefix))
            return;
        if (oldOwner.empty())
            add(name);
        else if (newOwner.empty())
            remove(name);
        else {
            remove(name);
            add(name);
        }
    });

    const dbus::Proxy daemon(m_bus, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus");
    m_calls.track([&](std::function<void()> finished) {
        return daemon.callAsync("ListNames", [this, finished](const dbus::Reply &reply) {
            for (const std::string &name : reply.value<std::vector<std::string>>().value_or(std::vector<std::string>())) {
                if (name.starts_with(kPrefix))
                    add(name);
            }
            finished();
        });
    });
}

MprisManager::~MprisManager() = default;

std::string MprisManager::currentService() const
{
    return m_current ? m_current->service() : std::string();
}

std::vector<std::string> MprisManager::availableServices() const
{
    std::vector<const Entry *> others;
    for (const Entry &entry : m_entries) {
        if (entry.player != m_current)
            others.push_back(&entry);
    }
    std::sort(others.begin(), others.end(), [](const Entry *a, const Entry *b) { return a->active > b->active; });

    std::vector<std::string> services;
    if (present(m_current))
        services.push_back(m_current->service());
    for (const Entry *entry : others)
        services.push_back(entry->player->service());
    return services;
}

MprisManager::Entry *MprisManager::entry(const std::string &service)
{
    const auto found = std::find_if(m_entries.begin(), m_entries.end(),
                                    [&service](const Entry &entry) { return entry.player->service() == service; });
    return found == m_entries.end() ? nullptr : &*found;
}

bool MprisManager::present(const std::shared_ptr<MprisPlayer> &player) const
{
    return player && std::any_of(m_entries.begin(), m_entries.end(),
                                 [&player](const Entry &entry) { return entry.player == player; });
}

void MprisManager::add(const std::string &service)
{
    if (Entry *known = entry(service)) {
        known->active = ++m_clock;
        return;
    }

    Entry added;
    added.player = std::make_shared<MprisPlayer>(m_bus, service);
    added.active = ++m_clock;
    added.statusChanged = added.player->playbackStatusChanged.connect([this, service] { statusChanged(service); });
    m_entries.push_back(std::move(added));

    // Nothing playing is interrupted by showing the newcomer.
    if (!m_current || !playing(*m_current))
        show(m_entries.back().player);
}

void MprisManager::remove(const std::string &service)
{
    std::erase_if(m_entries, [&service](const Entry &entry) { return entry.player->service() == service; });
    if (m_current && m_current->service() == service)
        show(choose());
}

void MprisManager::statusChanged(const std::string &service)
{
    Entry *changed = entry(service);
    if (!changed)
        return;

    if (playing(*changed->player))
        changed->active = ++m_clock;
    show(choose());
}

std::shared_ptr<MprisPlayer> MprisManager::choose() const
{
    if (present(m_current) && playing(*m_current))
        return m_current;

    const Entry *latestPlaying = nullptr;
    const Entry *latest = nullptr;
    for (const Entry &entry : m_entries) {
        if (playing(*entry.player) && (!latestPlaying || entry.active > latestPlaying->active))
            latestPlaying = &entry;
        if (!latest || entry.active > latest->active)
            latest = &entry;
    }

    if (latestPlaying)
        return latestPlaying->player;
    if (present(m_current))
        return m_current;
    return latest ? latest->player : nullptr;
}

void MprisManager::show(std::shared_ptr<MprisPlayer> player)
{
    if (player == m_current)
        return;

    m_current = std::move(player);
    m_currentSubscription = m_current ? m_current->changed.connect([this] { currentPlayerChanged(); })
                                      : base::Subscription();
    currentChanged();
}

} // namespace cutefish::shell
