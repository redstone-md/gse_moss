/* Copyright (C) 2024 Goldberg Emulator (moss integration)
   This file is part of the Goldberg Emulator

   The Goldberg Emulator is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 3 of the License, or (at your option) any later version.

   The Goldberg Emulator is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with the Goldberg Emulator; if not, see
   <http://www.gnu.org/licenses/>.  */

// Thin C++ wrapper around the moss P2P core (moss.dll / libmoss.so), loaded
// dynamically at runtime. Exposes a pub/sub mesh transport that the Networking
// layer uses as an internet-wide alternative/complement to LAN broadcast.

#ifndef MOSS_TRANSPORT_INCLUDE
#define MOSS_TRANSPORT_INCLUDE

#include <cstdint>
#include <array>
#include <string>
#include <vector>
#include <deque>
#include <mutex>

// ---- moss FFI surface (mirrors moss.h, kept inline so we don't depend on the
//      generated cgo header at build time) -----------------------------------
typedef int64_t MossHandle;

typedef void (*MossMessageCallback)(const char *channel,
                                    const uint8_t *sender_id,
                                    const uint8_t *data, uint32_t len);
typedef void (*MossEventCallback)(int32_t event_type, const char *detail_json);
typedef uint32_t (*MossKeyStoreLoadCallback)(uint8_t *buffer, uint32_t capacity);
typedef void (*MossKeyStoreSaveCallback)(const uint8_t *data, uint32_t len);

// moss event ids (see moss docs/API.md)
enum {
    MOSS_EVENT_PEER_JOINED = 1,
    MOSS_EVENT_PEER_LEFT = 2,
    MOSS_EVENT_SUPERNODE_PROMOTED = 3,
    MOSS_EVENT_SUPERNODE_REVOKED = 4,
    MOSS_EVENT_TRACKER_ANNOUNCE = 5,
    MOSS_EVENT_TRACKER_FAILURE = 6,
    MOSS_EVENT_RELAY_MIGRATED = 7,
};

// a message received from the mesh, marshalled off the moss callback thread
struct MossInbound {
    std::array<uint8_t, 32> sender{};
    std::vector<uint8_t> data{};
};

struct MossEvent {
    int32_t type = 0;
    std::string detail_json{};
};

class MossTransport {
public:
    MossTransport();
    ~MossTransport();

    // Loads moss.dll/.so, creates+starts a node and subscribes to `channel`.
    //  mesh_id        : rendezvous id (controls the tracker infohash)
    //  channel        : pub/sub topic used for all traffic
    //  psk            : optional 32-byte pre-shared key (empty => open mesh)
    //  trackers       : optional tracker override (empty => moss defaults)
    //  static_peers   : optional host:port peers to dial directly
    //  identity_path  : file used to persist the node identity across runs
    // Returns true on success. On failure the object is left disabled and all
    // operations become no-ops, so the caller can transparently fall back to LAN.
    bool init(const std::string &mesh_id,
              const std::string &channel,
              const std::vector<uint8_t> &psk,
              const std::vector<std::string> &trackers,
              const std::vector<std::string> &static_peers,
              const std::string &identity_path,
              bool high_throughput,
              int listen_port);

    void shutdown();

    bool is_enabled() const { return enabled; }

    // Publish a raw payload (already-serialized Common_Message) to the mesh.
    bool publish(const uint8_t *data, uint32_t len);
    bool publish(const std::vector<uint8_t> &data) { return publish(data.data(), (uint32_t)data.size()); }

    // Drain queued inbound messages / events (called from Networking::Run()).
    void poll_messages(std::vector<MossInbound> &out);
    void poll_events(std::vector<MossEvent> &out);

    const std::array<uint8_t, 32> &own_public_key() const { return public_key; }
    int peer_count() const;
    std::string nat_type() const; // "" if unknown/unavailable

private:
    // called by the static C trampolines
    void on_message(const uint8_t *sender_id, const uint8_t *data, uint32_t len);
    void on_event(int32_t event_type, const char *detail_json);

    static void msg_trampoline(const char *channel, const uint8_t *sender_id, const uint8_t *data, uint32_t len);
    static void event_trampoline(int32_t event_type, const char *detail_json);
    static uint32_t keystore_load_trampoline(uint8_t *buffer, uint32_t capacity);
    static void keystore_save_trampoline(const uint8_t *data, uint32_t len);

    bool load_library();

    bool enabled = false;
    void *lib_handle = nullptr;       // HMODULE / void* from dlopen
    MossHandle node = -1;
    std::string channel{};
    std::array<uint8_t, 32> public_key{};

    std::mutex inbox_mutex;
    std::deque<MossInbound> inbox;
    std::deque<MossEvent> events;

    // resolved moss entry points
    MossHandle (*p_Init)(char *, uint8_t *, char *) = nullptr;
    int32_t (*p_Start)(MossHandle) = nullptr;
    int32_t (*p_Stop)(MossHandle) = nullptr;
    int32_t (*p_Subscribe)(MossHandle, char *) = nullptr;
    int32_t (*p_Connect)(MossHandle, char *) = nullptr;
    int32_t (*p_Publish)(MossHandle, char *, uint8_t *, uint32_t) = nullptr;
    int32_t (*p_SetCallback)(MossHandle, MossMessageCallback) = nullptr;
    int32_t (*p_SetEventCallback)(MossHandle, MossEventCallback) = nullptr;
    int32_t (*p_SetKeyStore)(MossKeyStoreLoadCallback, MossKeyStoreSaveCallback) = nullptr;
    char *(*p_GetMeshInfo)(MossHandle) = nullptr;
    uint8_t *(*p_GetPublicKey)(MossHandle) = nullptr;
    // Optional (newer moss.dll): returns the human-readable OS reason behind a
    // coarse failure code — chiefly the bind error behind a failed Moss_Start.
    // Resolved best-effort; may stay null against an older dll.
    char *(*p_LastError)(MossHandle) = nullptr;
    void (*p_Free)(void *) = nullptr;
};

#endif // MOSS_TRANSPORT_INCLUDE
