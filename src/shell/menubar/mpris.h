// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_MENUBAR_MPRIS_H_
#define CUTEFISH_SHELL_MENUBAR_MPRIS_H_

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/dbus.h"
#include "base/signal.h"
#include "base/timer.h"
#include "shell/common/pending_calls.h"
#include "shell/common/sender_filter.h"

namespace cutefish::shell {

// One media player on the session bus, org.mpris.MediaPlayer2.<name>: its
// MediaPlayer2 and MediaPlayer2.Player properties, read once and kept from
// PropertiesChanged, and the calls the control centre's media controls make.
class MprisPlayer
{
public:
    enum class PlaybackStatus {
        Stopped,
        Playing,
        Paused,
    };

    MprisPlayer(base::dbus::Connection *bus, std::string service);
    ~MprisPlayer();

    MprisPlayer(const MprisPlayer &) = delete;
    MprisPlayer &operator=(const MprisPlayer &) = delete;

    const std::string &service() const { return m_service; }
    // Both interfaces answered once.
    bool isReady() const { return m_rootReady && m_playerReady; }

    std::string identity() const;
    uint32_t processId() const { return m_processId; }

    PlaybackStatus playbackStatus() const;
    bool canControl() const;
    bool canGoNext() const;
    bool canGoPrevious() const;
    bool canPlay() const;
    bool canPause() const;
    // xesam:title, xesam:artist joined by ", ", mpris:artUrl.
    std::string title() const;
    std::string artist() const;
    std::string artUrl() const;
    std::optional<double> position() const; // Seconds, projected from a monotonic sample.
    double duration() const;
    base::dbus::ObjectPath trackId() const;
    bool canSetPosition() const;
    bool atEnd() const { return m_atEnd; }
    void refreshPosition();
    void setPosition(double seconds);

    // Methods of a player that cannot be controlled are not sent.
    void play();
    void pause();
    void playPause();
    void stop();
    void next();
    void previous();

    // Any property changed.
    base::Signal<> changed;
    base::Signal<> playbackStatusChanged;

private:
    void load(const std::string &interface);
    void apply(const std::string &interface, const base::dbus::VariantMap &properties);
    bool playerFlag(const char *name, bool fallback = false) const;
    void callPlayer(const char *method);
    void samplePosition(std::optional<double> seconds);
    void observePosition(double seconds);
    bool isStaleSeekPosition(double seconds) const;
    int64_t durationMicros() const;
    bool hasTrackPath() const;
    void sendSeek(uint64_t request, int64_t micros, std::optional<double> before, bool absolute);

    base::dbus::Connection *m_bus;
    std::string m_service;
    base::dbus::Proxy m_root;
    base::dbus::Proxy m_player;
    SenderFilter m_sender;
    std::vector<base::dbus::Match> m_matches;
    PendingCalls m_calls;
    base::dbus::VariantMap m_rootProperties;
    base::dbus::VariantMap m_playerProperties;
    base::dbus::VariantMap m_metadata;
    bool m_rootReady = false;
    bool m_playerReady = false;
    uint32_t m_processId = 0;
    base::Timer m_positionPoll;
    std::optional<double> m_position;
    std::optional<double> m_nearEndPosition;
    bool m_atEnd = false;
    std::chrono::steady_clock::time_point m_sampleTime;
    uint64_t m_positionRevision = 0;
    uint64_t m_seekSerial = 0;
    std::optional<uint64_t> m_seekRequest;
    std::chrono::steady_clock::time_point m_seekGuardUntil;
    bool m_readingPosition = false;
    bool m_readPositionAgain = false;
};

// The media players on the bus and the one the control centre shows. A
// playing player stays shown; when it stops, the one that most recently
// started playing takes over. With none playing, a player that appears is
// shown, and when the shown one leaves, the most recently active one is.
class MprisManager
{
public:
    explicit MprisManager(base::dbus::Connection *bus);
    ~MprisManager();

    MprisManager(const MprisManager &) = delete;
    MprisManager &operator=(const MprisManager &) = delete;

    MprisPlayer *current() const { return m_current.get(); }
    std::string currentService() const;
    // The shown player first, then the most recently active.
    std::vector<std::string> availableServices() const;

    base::Signal<> currentChanged;
    // Something of the shown player changed.
    base::Signal<> currentPlayerChanged;

private:
    struct Entry
    {
        std::shared_ptr<MprisPlayer> player;
        // Raised when the player appears or starts playing.
        uint64_t active = 0;
        base::Subscription statusChanged;
    };

    void add(const std::string &service);
    void remove(const std::string &service);
    void statusChanged(const std::string &service);
    Entry *entry(const std::string &service);
    bool present(const std::shared_ptr<MprisPlayer> &player) const;
    std::shared_ptr<MprisPlayer> choose() const;
    void show(std::shared_ptr<MprisPlayer> player);

    base::dbus::Connection *m_bus;
    std::vector<Entry> m_entries;
    std::shared_ptr<MprisPlayer> m_current;
    uint64_t m_clock = 0;
    base::Subscription m_currentSubscription;
    base::dbus::Match m_nameOwnerChanged;
    PendingCalls m_calls;
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_MENUBAR_MPRIS_H_
