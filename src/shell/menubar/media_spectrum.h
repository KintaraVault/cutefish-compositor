// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CUTEFISH_SHELL_MENUBAR_MEDIA_SPECTRUM_H_
#define CUTEFISH_SHELL_MENUBAR_MEDIA_SPECTRUM_H_

#include <array>
#include <cstdint>
#include <memory>

namespace cutefish::shell {

using MediaBands = std::array<double, 6>;

// A passive tap of the active player's PipeWire playback node, never of a
// microphone or of the whole output mix. Unmatched players have no levels.
class MediaSpectrum
{
public:
    MediaSpectrum();
    ~MediaSpectrum();
    void setPlayer(uint32_t pid);
    MediaBands levels() const;

private:
    struct Tap;
    std::unique_ptr<Tap> m_tap;
};

} // namespace cutefish::shell

#endif // CUTEFISH_SHELL_MENUBAR_MEDIA_SPECTRUM_H_
