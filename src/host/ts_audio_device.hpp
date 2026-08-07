#pragma once

#include "host/ts_player.hpp"

#include <memory>
#include <string>

namespace ts::host {

/// The output device, and nothing else.
///
/// The counterpart of the Apple front end's `AudioOutput`: it owns a device whose callback copies
/// out of the player's ring and does nothing else -- no allocation, no lock, no engine code. All the
/// interesting behaviour is upstream in `Player`; this is the twenty lines that hand the ring to a
/// backend.
///
/// The device is opened at the engine's own 32 kHz rather than the hardware's rate, so miniaudio
/// performs the one resample on the way out and the engine never has to be told what the device is
/// doing. (The Apple build connects its source node at 32 kHz for exactly the same reason.)
class AudioDevice {
public:
    /// Does not open anything; call `start`.
    explicit AudioDevice(Player& player);
    ~AudioDevice();

    AudioDevice(const AudioDevice&) = delete;
    AudioDevice& operator=(const AudioDevice&) = delete;
    AudioDevice(AudioDevice&&) = delete;
    AudioDevice& operator=(AudioDevice&&) = delete;

    /// Opens the default output device and starts it. Throws `std::runtime_error` if the backend
    /// will not start; the caller is expected to surface that rather than play in silence.
    ///
    /// Idempotent -- starting a running device does nothing.
    void start();

    void stop() noexcept;

    /// Closes and reopens the device, which is how a route change is handled: the old device is
    /// gone and its period is not necessarily the new one's.
    void restart();

    [[nodiscard]] bool running() const noexcept;

    /// The backend miniaudio settled on (PipeWire, PulseAudio, ALSA, JACK), for the about dialog
    /// and for bug reports where the backend is usually the first question.
    [[nodiscard]] std::string backend_name() const;

    /// What the hardware is actually running at, which is not the engine's rate.
    [[nodiscard]] int device_rate() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ts::host
