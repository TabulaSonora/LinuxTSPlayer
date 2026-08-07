#pragma once

#include "host/ts_player.hpp"

#include <memory>

namespace ts::host {

/// Live MIDI in, over the ALSA sequencer.
///
/// The counterpart of the Apple front end's `MIDIInput`, and a good deal shorter: CoreMIDI hands
/// over packed UMP words that have to be walked by message type to know their length, whereas the
/// sequencer delivers one decoded event at a time. All that is left is mapping event kinds back to
/// the status bytes the engine speaks.
///
/// Everything arrives on port 0 and goes through `Player::send_channel`, which appends to the
/// non-blocking inbox -- a MIDI source must never wait on a block render.
class MidiInput {
public:
    explicit MidiInput(Player& player);
    ~MidiInput();

    MidiInput(const MidiInput&) = delete;
    MidiInput& operator=(const MidiInput&) = delete;
    MidiInput(MidiInput&&) = delete;
    MidiInput& operator=(MidiInput&&) = delete;

    /// Creates the client and its writable port, and starts listening.
    ///
    /// Does not throw when the sequencer is unavailable -- a machine with no `snd-seq` module is a
    /// perfectly good music player, just not one you can play into. Returns whether it came up.
    bool start();

    void stop() noexcept;

    [[nodiscard]] bool running() const noexcept;

    /// Subscribes to every readable port on the system, the equivalent of the Apple build's
    /// `connectAllSources`. Also applied to ports that appear later, so a keyboard plugged in mid
    /// session is picked up.
    ///
    /// Off by default: it is convenient on a desktop with one keyboard and actively wrong on a
    /// machine running a sequencer, where it would echo everything back into the synth.
    void set_auto_connect(bool enabled);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ts::host
