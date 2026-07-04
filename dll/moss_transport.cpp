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

#include "dll/moss_transport.h"
#include "dll/dll.h"

#include <fstream>
#include <filesystem>
#include <cstring>

#if defined(STEAM_WIN32)
// windows.h already pulled in via common_includes.h
#else
#include <dlfcn.h>
#endif

// ---- globals for the moss contextless callbacks --------------------------------
// moss message/event callbacks carry no user pointer, and the keystore callbacks
// are registered globally (not per-handle). The emulator only ever runs a single
// Networking instance, hence a single moss node, so a single active pointer and a
// single identity path are sufficient.
static MossTransport *g_moss_active = nullptr;
static std::mutex g_moss_keystore_mutex;
static std::string g_moss_identity_path;

// ================================================================================
// dynamic library loading helpers
// ================================================================================
static void *moss_dlopen(const char *name)
{
#if defined(STEAM_WIN32)
    return (void *)LoadLibraryA(name);
#else
    return dlopen(name, RTLD_NOW | RTLD_LOCAL);
#endif
}

static void moss_dlclose(void *handle)
{
    if (!handle) return;
#if defined(STEAM_WIN32)
    FreeLibrary((HMODULE)handle);
#else
    dlclose(handle);
#endif
}

static void *moss_dlsym(void *handle, const char *symbol)
{
#if defined(STEAM_WIN32)
    return (void *)GetProcAddress((HMODULE)handle, symbol);
#else
    return dlsym(handle, symbol);
#endif
}

// resolve a symbol into a function pointer, recording failure
template <typename T>
static bool resolve(void *handle, const char *symbol, T &out, bool &ok)
{
    out = reinterpret_cast<T>(moss_dlsym(handle, symbol));
    if (!out) {
        PRINT_DEBUG("moss: missing symbol '%s'", symbol);
        ok = false;
    }
    return out != nullptr;
}

// ================================================================================
// MossTransport
// ================================================================================
MossTransport::MossTransport() {}

MossTransport::~MossTransport()
{
    shutdown();
}

bool MossTransport::load_library()
{
#if defined(STEAM_WIN32)
    const char *candidates[] = { "moss.dll", "moss64.dll" };
#elif defined(__APPLE__)
    const char *candidates[] = { "libmoss.dylib", "./libmoss.dylib" };
#else
    const char *candidates[] = { "libmoss.so", "./libmoss.so" };
#endif
    for (const char *name : candidates) {
        lib_handle = moss_dlopen(name);
        if (lib_handle) {
            PRINT_DEBUG("moss: loaded '%s'", name);
            return true;
        }
    }
    PRINT_DEBUG("moss: shared library not found, P2P transport disabled");
    return false;
}

bool MossTransport::init(const std::string &mesh_id,
                         const std::string &channel_,
                         const std::vector<uint8_t> &psk,
                         const std::vector<std::string> &trackers,
                         const std::vector<std::string> &static_peers,
                         const std::string &identity_path,
                         bool high_throughput,
                         int listen_port)
{
    if (enabled) return true;
    if (g_moss_active) {
        // another instance already owns the global callbacks; refuse to avoid cross-talk
        PRINT_DEBUG("moss: an active transport already exists, skipping second init");
        return false;
    }

    if (!load_library()) return false;

    bool ok = true;
    resolve(lib_handle, "Moss_Init", p_Init, ok);
    resolve(lib_handle, "Moss_Start", p_Start, ok);
    resolve(lib_handle, "Moss_Stop", p_Stop, ok);
    resolve(lib_handle, "Moss_Subscribe", p_Subscribe, ok);
    resolve(lib_handle, "Moss_Connect", p_Connect, ok);
    resolve(lib_handle, "Moss_Publish", p_Publish, ok);
    resolve(lib_handle, "Moss_SetCallback", p_SetCallback, ok);
    resolve(lib_handle, "Moss_SetEventCallback", p_SetEventCallback, ok);
    resolve(lib_handle, "Moss_SetKeyStore", p_SetKeyStore, ok);
    resolve(lib_handle, "Moss_GetMeshInfo", p_GetMeshInfo, ok);
    resolve(lib_handle, "Moss_GetPublicKey", p_GetPublicKey, ok);
    resolve(lib_handle, "Moss_Free", p_Free, ok);
    if (!ok) {
        PRINT_DEBUG("moss: failed to resolve required symbols");
        moss_dlclose(lib_handle);
        lib_handle = nullptr;
        return false;
    }

    channel = channel_;

    // become the active instance + register identity persistence
    {
        std::lock_guard<std::mutex> lk(g_moss_keystore_mutex);
        g_moss_identity_path = identity_path;
    }
    g_moss_active = this;
    p_SetKeyStore(&MossTransport::keystore_load_trampoline, &MossTransport::keystore_save_trampoline);

    // build config JSON
    nlohmann::json cfg = nlohmann::json::object();
    if (!trackers.empty()) {
        cfg["trackers"] = trackers; // omit => moss default tracker set
    }
    if (!static_peers.empty()) {
        cfg["static_peers"] = static_peers;
    }
    // A FIXED listen port is important for NAT traversal: with a random port each
    // run, the node's external address changes every session, trackers accumulate
    // stale dead-port entries for our stable identity, and UPnP can't keep a
    // consistent mapping — peers then connect to dead ports and flap. A fixed port
    // keeps the external mapping stable so hole-punching / UPnP actually hold.
    int preferred_port = (listen_port > 0 && listen_port < 65536) ? listen_port : 41666;
    // Re-announce to trackers more often than the 120s default so two players who
    // start a minute apart still discover each other quickly, and so a dropped
    // candidate is retried sooner.
    cfg["announce_interval_sec"] = 30;
    // Enable automatic router port-mapping. Without this, two peers behind home
    // NATs frequently fail to hole-punch (sessions don't form, or only one
    // direction works, so the gossipsub mesh never grafts and app messages never
    // flow). UPnP / NAT-PMP / PCP open the listen port on cooperating routers and
    // make direct connectivity dramatically more reliable.
    cfg["nat"] = {
        {"upnp_enabled", true},
        {"natpmp_enabled", true},
        {"pcp_enabled", true},
        {"hole_punch_attempts", 5},
        {"port_prediction_enabled", true},
    };
    if (high_throughput) {
        cfg["transport"] = { {"high_throughput", true} };
    }

    uint8_t *psk_ptr = nullptr;
    uint8_t psk_buf[32];
    if (psk.size() == 32) {
        memcpy(psk_buf, psk.data(), 32);
        psk_ptr = psk_buf;
    }

    // Try the fixed port first (stable NAT mapping). moss binds BOTH tcp4 and udp4
    // on this port and does not retry a fixed port, so if either is unavailable
    // (already held by another instance / a lingering process, or blocked) Start
    // fails. Fall back to a couple of specific alternate ports so moss still comes
    // up instead of silently dropping to LAN-only.
    //
    // We deliberately do NOT fall back to port 0 (auto): under Proton/Wine the Go
    // runtime cannot bind sockets at all (IOCP is unsupported), so EVERY bind
    // fails, and port 0 makes moss retry 64 times (~10s) before giving up — hanging
    // game startup for Wine users who can't use moss anyway. Specific ports fail
    // fast, so the drop to LAN-only is immediate there.
    int try_ports[3] = {
        preferred_port,
        preferred_port < 65535 ? preferred_port + 1 : 41667,
        preferred_port < 65534 ? preferred_port + 2 : 41668,
    };
    bool started = false;
    for (int pi = 0; pi < 3 && !started; ++pi) {
        cfg["listen_port"] = try_ports[pi];
        std::string cfg_str = cfg.dump();
        if (pi == 0) PRINT_DEBUG("[MOSS-DIAG] moss config: %s", cfg_str.c_str());

        std::string mesh_id_mut = mesh_id;
        node = p_Init(&mesh_id_mut[0], psk_ptr, &cfg_str[0]);
        if (node < 0) {
            PRINT_DEBUG("moss: Moss_Init failed with code %lld (port %d)", (long long)node, try_ports[pi]);
            continue;
        }

        p_SetCallback(node, &MossTransport::msg_trampoline);
        p_SetEventCallback(node, &MossTransport::event_trampoline);

        int32_t start_rc = p_Start(node);
        if (start_rc == 0) {
            started = true;
            if (pi != 0) {
                PRINT_DEBUG("[MOSS-DIAG] fixed port %d unavailable — moss started on port %d "
                            "(NAT mapping less stable; free %d or forward it for best results)",
                            preferred_port, try_ports[pi], preferred_port);
            }
            break;
        }
        PRINT_DEBUG("moss: Moss_Start failed with code %d on port %d%s",
            start_rc, try_ports[pi], pi < 2 ? " — trying next port" : " — giving up, LAN only");
        p_Stop(node);
        node = -1;
    }
    if (!started) {
        g_moss_active = nullptr;
        moss_dlclose(lib_handle);
        lib_handle = nullptr;
        return false;
    }

    p_Subscribe(node, &channel[0]);

    // dial explicit static peers as well (Connect is optional / best-effort)
    for (auto peer : static_peers) {
        p_Connect(node, &peer[0]);
    }

    // cache our public key
    uint8_t *pk = p_GetPublicKey(node);
    if (pk) {
        memcpy(public_key.data(), pk, 32);
        p_Free(pk);
    }

    enabled = true;
    PRINT_DEBUG("moss: transport started, mesh='%s' channel='%s'", mesh_id.c_str(), channel.c_str());
    return true;
}

void MossTransport::shutdown()
{
    if (!enabled && !lib_handle) {
        if (g_moss_active == this) g_moss_active = nullptr;
        return;
    }
    if (node >= 0 && p_Stop) {
        p_Stop(node);
        node = -1;
    }
    if (g_moss_active == this) g_moss_active = nullptr;
    if (lib_handle) {
        moss_dlclose(lib_handle);
        lib_handle = nullptr;
    }
    enabled = false;
}

bool MossTransport::publish(const uint8_t *data, uint32_t len)
{
    if (!enabled || !p_Publish || node < 0) return false;
    int32_t rc = p_Publish(node, &channel[0], const_cast<uint8_t *>(data), len);
    // rc 0 = flooded to >=1 topic peer; -6 = MOSS_ERR_NO_PEERS (nobody grafted on
    // this topic yet) — both are non-fatal (a peer may simply not be connected yet).
    return rc == 0 || rc == -6;
}

void MossTransport::poll_messages(std::vector<MossInbound> &out)
{
    std::lock_guard<std::mutex> lk(inbox_mutex);
    while (!inbox.empty()) {
        out.push_back(std::move(inbox.front()));
        inbox.pop_front();
    }
}

void MossTransport::poll_events(std::vector<MossEvent> &out)
{
    std::lock_guard<std::mutex> lk(inbox_mutex);
    while (!events.empty()) {
        out.push_back(std::move(events.front()));
        events.pop_front();
    }
}

int MossTransport::peer_count() const
{
    if (!enabled || !p_GetMeshInfo || node < 0) return 0;
    char *info = p_GetMeshInfo(node);
    if (!info) return 0;
    int count = 0;
    try {
        auto j = nlohmann::json::parse(info);
        if (j.contains("peer_count")) count = j["peer_count"].get<int>();
    } catch (...) {}
    p_Free(info);
    return count;
}

std::string MossTransport::nat_type() const
{
    if (!enabled || !p_GetMeshInfo || node < 0) return "";
    char *info = p_GetMeshInfo(node);
    if (!info) return "";
    std::string nat;
    try {
        auto j = nlohmann::json::parse(info);
        if (j.contains("nat_type")) nat = j["nat_type"].get<std::string>();
    } catch (...) {}
    p_Free(info);
    return nat;
}

// ---- callback handling --------------------------------------------------------
void MossTransport::on_message(const uint8_t *sender_id, const uint8_t *data, uint32_t len)
{
    MossInbound m;
    if (sender_id) memcpy(m.sender.data(), sender_id, 32);
    if (data && len) m.data.assign(data, data + len);
    std::lock_guard<std::mutex> lk(inbox_mutex);
    // soft cap to avoid unbounded growth if Run() stalls
    if (inbox.size() < 8192) inbox.push_back(std::move(m));
}

void MossTransport::on_event(int32_t event_type, const char *detail_json)
{
    MossEvent e;
    e.type = event_type;
    if (detail_json) e.detail_json = detail_json;
    std::lock_guard<std::mutex> lk(inbox_mutex);
    if (events.size() < 4096) events.push_back(std::move(e));
}

// ---- static trampolines -------------------------------------------------------
void MossTransport::msg_trampoline(const char * /*channel*/, const uint8_t *sender_id, const uint8_t *data, uint32_t len)
{
    MossTransport *self = g_moss_active;
    if (self) self->on_message(sender_id, data, len);
}

void MossTransport::event_trampoline(int32_t event_type, const char *detail_json)
{
    MossTransport *self = g_moss_active;
    if (self) self->on_event(event_type, detail_json);
}

uint32_t MossTransport::keystore_load_trampoline(uint8_t *buffer, uint32_t capacity)
{
    std::lock_guard<std::mutex> lk(g_moss_keystore_mutex);
    if (g_moss_identity_path.empty()) return 0;

    std::ifstream f(std::filesystem::u8path(g_moss_identity_path), std::ios::binary | std::ios::ate);
    if (!f.is_open()) return 0;
    std::streamsize size = f.tellg();
    if (size <= 0) return 0;

    // probe call: capacity 0 / null buffer => return required size
    if (buffer == nullptr || capacity == 0) {
        return (uint32_t)size;
    }
    if ((uint32_t)size > capacity) return 0;
    f.seekg(0, std::ios::beg);
    if (!f.read((char *)buffer, size)) return 0;
    return (uint32_t)size;
}

void MossTransport::keystore_save_trampoline(const uint8_t *data, uint32_t len)
{
    std::lock_guard<std::mutex> lk(g_moss_keystore_mutex);
    if (g_moss_identity_path.empty() || !data || !len) return;
    std::ofstream f(std::filesystem::u8path(g_moss_identity_path), std::ios::binary | std::ios::trunc);
    if (f.is_open()) {
        f.write((const char *)data, len);
    }
}
