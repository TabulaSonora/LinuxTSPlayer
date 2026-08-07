#include "host/ts_midi_input.hpp"

#include <alsa/asoundlib.h>
#include <poll.h>
#include <unistd.h>

#include <atomic>
#include <thread>
#include <vector>

namespace ts::host {

namespace {

/// Turns one sequencer event back into the three bytes the engine speaks.
///
/// Returns false for everything that is not a channel voice message. SysEx is deliberately not
/// forwarded: `Player` has no live SysEx path, and a GS reset arriving from a keyboard mid-song
/// would undo the loaded file's own setup.
bool to_channel_message(const snd_seq_event_t& event, int& status, int& data1, int& data2)
{
    switch (event.type) {
    case SND_SEQ_EVENT_NOTEON:
        status = 0x90 | (event.data.note.channel & 0x0F);
        data1 = event.data.note.note;
        data2 = event.data.note.velocity;
        return true;

    case SND_SEQ_EVENT_NOTEOFF:
        status = 0x80 | (event.data.note.channel & 0x0F);
        data1 = event.data.note.note;
        data2 = event.data.note.off_velocity;
        return true;

    case SND_SEQ_EVENT_KEYPRESS:
        status = 0xA0 | (event.data.note.channel & 0x0F);
        data1 = event.data.note.note;
        data2 = event.data.note.velocity;
        return true;

    case SND_SEQ_EVENT_CONTROLLER:
        status = 0xB0 | (event.data.control.channel & 0x0F);
        data1 = event.data.control.param & 0x7F;
        data2 = event.data.control.value & 0x7F;
        return true;

    case SND_SEQ_EVENT_PGMCHANGE:
        status = 0xC0 | (event.data.control.channel & 0x0F);
        data1 = event.data.control.value & 0x7F;
        data2 = 0;
        return true;

    case SND_SEQ_EVENT_CHANPRESS:
        status = 0xD0 | (event.data.control.channel & 0x0F);
        data1 = event.data.control.value & 0x7F;
        data2 = 0;
        return true;

    case SND_SEQ_EVENT_PITCHBEND: {
        // ALSA centres bend on zero and the wire format centres it on 8192.
        const int bend = event.data.control.value + 8192;
        status = 0xE0 | (event.data.control.channel & 0x0F);
        data1 = bend & 0x7F;
        data2 = (bend >> 7) & 0x7F;
        return true;
    }

    default:
        return false;
    }
}

} // namespace

struct MidiInput::Impl {
    Player* player = nullptr;
    snd_seq_t* seq = nullptr;
    int port = -1;

    /// Written to in order to wake the listener out of poll() at shutdown. A signal would do, but a
    /// pipe needs no handler and cannot be delivered to the wrong thread.
    int wake[2] = {-1, -1};

    std::atomic<bool> quit{false};
    std::atomic<bool> auto_connect{false};
    std::thread listener;

    void listen();
    void connect_all_sources();
};

void MidiInput::Impl::connect_all_sources()
{
    snd_seq_client_info_t* client_info = nullptr;
    snd_seq_port_info_t* port_info = nullptr;
    snd_seq_client_info_alloca(&client_info);
    snd_seq_port_info_alloca(&port_info);

    const int self = snd_seq_client_id(seq);

    snd_seq_client_info_set_client(client_info, -1);
    while (snd_seq_query_next_client(seq, client_info) >= 0) {
        const int client = snd_seq_client_info_get_client(client_info);

        // Skip ourselves and the System client, which announces port changes rather than notes.
        if (client == self || client == SND_SEQ_CLIENT_SYSTEM) {
            continue;
        }

        snd_seq_port_info_set_client(port_info, client);
        snd_seq_port_info_set_port(port_info, -1);
        while (snd_seq_query_next_port(seq, port_info) >= 0) {
            const unsigned int needed = SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ;
            if ((snd_seq_port_info_get_capability(port_info) & needed) != needed) {
                continue;
            }
            // Already-subscribed ports fail harmlessly, so there is nothing to track.
            snd_seq_connect_from(seq, port, client, snd_seq_port_info_get_port(port_info));
        }
    }
}

void MidiInput::Impl::listen()
{
    const int seq_fds = snd_seq_poll_descriptors_count(seq, POLLIN);
    std::vector<pollfd> fds(static_cast<std::size_t>(seq_fds) + 1);
    snd_seq_poll_descriptors(seq, fds.data(), static_cast<unsigned int>(seq_fds), POLLIN);

    fds.back().fd = wake[0];
    fds.back().events = POLLIN;

    while (!quit.load(std::memory_order_relaxed)) {
        if (poll(fds.data(), static_cast<nfds_t>(fds.size()), -1) < 0) {
            continue;
        }
        if (quit.load(std::memory_order_relaxed)) {
            break;
        }

        snd_seq_event_t* event = nullptr;
        while (snd_seq_event_input(seq, &event) >= 0 && event != nullptr) {
            // The System client announces a new port by way of this event; re-running the sweep is
            // how a keyboard plugged in mid-session gets picked up. The Apple build does the same
            // thing on CoreMIDI's msgSetupChanged.
            if (event->type == SND_SEQ_EVENT_PORT_START
                && auto_connect.load(std::memory_order_relaxed)) {
                connect_all_sources();
            }

            int status = 0;
            int data1 = 0;
            int data2 = 0;
            if (to_channel_message(*event, status, data1, data2)) {
                player->send_channel(0, status, data1, data2);
            }

            if (snd_seq_event_input_pending(seq, 0) <= 0) {
                break;
            }
        }
    }
}

MidiInput::MidiInput(Player& player) : impl_(std::make_unique<Impl>())
{
    impl_->player = &player;
}

MidiInput::~MidiInput()
{
    stop();
}

bool MidiInput::start()
{
    if (impl_->seq != nullptr) {
        return true;
    }

    if (snd_seq_open(&impl_->seq, "default", SND_SEQ_OPEN_INPUT, 0) < 0) {
        impl_->seq = nullptr;
        return false;
    }

    snd_seq_set_client_name(impl_->seq, "Tabula Sonora");

    impl_->port = snd_seq_create_simple_port(
        impl_->seq, "Tabula Sonora",
        SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE,
        SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_SYNTHESIZER
            | SND_SEQ_PORT_TYPE_APPLICATION);

    if (impl_->port < 0) {
        snd_seq_close(impl_->seq);
        impl_->seq = nullptr;
        return false;
    }

    if (pipe(impl_->wake) != 0) {
        snd_seq_close(impl_->seq);
        impl_->seq = nullptr;
        return false;
    }

    if (impl_->auto_connect.load(std::memory_order_relaxed)) {
        impl_->connect_all_sources();
    }

    impl_->quit.store(false, std::memory_order_relaxed);
    impl_->listener = std::thread{[this] { impl_->listen(); }};
    return true;
}

void MidiInput::stop() noexcept
{
    if (impl_->seq == nullptr) {
        return;
    }

    impl_->quit.store(true, std::memory_order_relaxed);

    if (impl_->wake[1] >= 0) {
        const char byte = 0;
        [[maybe_unused]] const auto written = write(impl_->wake[1], &byte, 1);
    }

    if (impl_->listener.joinable()) {
        impl_->listener.join();
    }

    for (int& fd : impl_->wake) {
        if (fd >= 0) {
            close(fd);
            fd = -1;
        }
    }

    snd_seq_close(impl_->seq);
    impl_->seq = nullptr;
    impl_->port = -1;
}

bool MidiInput::running() const noexcept
{
    return impl_->seq != nullptr;
}

void MidiInput::set_auto_connect(bool enabled)
{
    impl_->auto_connect.store(enabled, std::memory_order_relaxed);
    if (enabled && impl_->seq != nullptr) {
        impl_->connect_all_sources();
    }
}

} // namespace ts::host
