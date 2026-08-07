#include "host/ts_audio_device.hpp"

#include <miniaudio.h>

#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>

namespace ts::host {

namespace {

/// The audio callback, and the only code in this project that runs under a hard deadline.
///
/// It records what the device asked for -- the render thread reads that to size its lead, since a
/// backend's period is not ours to choose and can change under us -- and then copies one span out
/// of the ring. `FrameRing::read` zero-pads any shortfall and counts it as an underrun, so there is
/// nothing to check and nothing to decide here.
///
/// Simpler than the Apple build's equivalent, which had to de-interleave into CoreAudio's two planar
/// buffers. The ring is interleaved and miniaudio wants interleaved, so the copy is direct.
void device_callback(ma_device* device, void* output, const void* input, ma_uint32 frame_count)
{
    (void)input;

    auto* handle = static_cast<Player::RingHandle*>(device->pUserData);
    handle->last_request.store(frame_count, std::memory_order_relaxed);

    handle->ring->read(std::span<float>{static_cast<float*>(output),
                                        static_cast<std::size_t>(frame_count) * 2});
}

} // namespace

struct AudioDevice::Impl {
    Player* player = nullptr;
    ma_context context{};
    ma_device device{};
    bool context_ready = false;
    bool device_ready = false;
};

AudioDevice::AudioDevice(Player& player) : impl_(std::make_unique<Impl>())
{
    impl_->player = &player;
}

AudioDevice::~AudioDevice()
{
    stop();
    if (impl_->device_ready) {
        ma_device_uninit(&impl_->device);
    }
    if (impl_->context_ready) {
        ma_context_uninit(&impl_->context);
    }
}

void AudioDevice::start()
{
    if (impl_->device_ready && ma_device_is_started(&impl_->device)) {
        return;
    }

    if (!impl_->context_ready) {
        // No backend list: miniaudio's own order already prefers PipeWire, then PulseAudio, then
        // ALSA, then JACK, which is the order a Linux desktop wants.
        if (ma_context_init(nullptr, 0, nullptr, &impl_->context) != MA_SUCCESS) {
            throw std::runtime_error{"No audio backend could be initialised"};
        }
        impl_->context_ready = true;
    }

    if (!impl_->device_ready) {
        ma_device_config config = ma_device_config_init(ma_device_type_playback);
        config.playback.format = ma_format_f32;
        config.playback.channels = 2;

        // The engine's rate, not the device's. miniaudio resamples on the way out.
        config.sampleRate = static_cast<ma_uint32>(Session::sample_rate);
        config.dataCallback = device_callback;
        config.pUserData = &impl_->player->handle();

        if (ma_device_init(&impl_->context, &config, &impl_->device) != MA_SUCCESS) {
            throw std::runtime_error{"No audio output device could be opened"};
        }
        impl_->device_ready = true;
    }

    if (ma_device_start(&impl_->device) != MA_SUCCESS) {
        throw std::runtime_error{"The audio output device would not start"};
    }
}

void AudioDevice::stop() noexcept
{
    if (impl_->device_ready && ma_device_is_started(&impl_->device)) {
        ma_device_stop(&impl_->device);
    }
}

void AudioDevice::restart()
{
    stop();
    if (impl_->device_ready) {
        ma_device_uninit(&impl_->device);
        impl_->device_ready = false;
    }
    start();
}

bool AudioDevice::running() const noexcept
{
    return impl_->device_ready && ma_device_is_started(&impl_->device) != 0u;
}

std::string AudioDevice::backend_name() const
{
    if (!impl_->context_ready) {
        return {};
    }
    return ma_get_backend_name(impl_->context.backend);
}

int AudioDevice::device_rate() const noexcept
{
    if (!impl_->device_ready) {
        return 0;
    }
    return static_cast<int>(impl_->device.sampleRate);
}

} // namespace ts::host
