# P2P multiplayer over moss

This fork can run the emulator's networking over the [moss](https://github.com/redstone-md/moss)
P2P mesh **in addition to** the classic LAN broadcast. That lets people on
different networks (different homes, cities, ISPs) find each other and play
together over the internet — something the plain LAN broadcast can't do — using
tracker-based rendezvous, NAT hole-punching / relaying and an encrypted mesh.

moss is loaded **at runtime** from `moss.dll` (Windows) / `libmoss.so` (Linux). If
the library is missing or fails to start, the emulator silently falls back to
LAN-only networking, so nothing breaks when it isn't present.

> This is a networking transport for the emulator's own lobby/friends/matchmaking
> layer. It does not connect to, impersonate, or interact with Valve's Steam
> network in any way.

---

## Quick start (play with a friend over the internet)

1. Both players use the **same emulator build** and keep **`moss.dll` next to the
   game's `steam_api(64).dll`** (i.e. next to the game `.exe`).
2. Both players use the **same App ID** — copy the same `steam_appid.txt`, or set
   the App ID the same way you normally do. Lobbies are filtered by App ID.
3. In `steam_settings/configs.main.ini` make sure moss is enabled (it is by
   default):
   ```ini
   [main::moss]
   enable_moss=1
   ```
4. Launch the game on both machines and **wait ~30–60 seconds** — moss needs a
   moment to bootstrap through trackers and open a path through your routers.
5. One player creates/hosts a lobby; the other opens the in-game lobby browser or
   joins by code. That's it.

By default the emulator joins one shared `global` mesh (so relay-capable nodes
can help bridge tricky NATs) and talks only to peers using the **same App ID**.

---

## Configuration

All keys live under `[main::moss]` in `steam_settings/configs.main.ini`:

| Key | Default | Meaning |
|-----|---------|---------|
| `enable_moss` | `1` | Master switch. `0` = LAN-only, no moss. |
| `room_key` | *(empty)* | Optional shared secret. When set, you join a **private** mesh + channel with only people using the exact same key — independent of App ID. Leave empty to use the default global mesh scoped by App ID. |
| `listen_port` | `41666` | Fixed UDP port moss binds. Keep it **fixed** (a stable port is important for NAT traversal). Everyone can use the same value; different machines don't conflict. |

Optional files in the `steam_settings/` folder (see the `*.EXAMPLE` versions):

- **`moss_trackers.txt`** — one tracker URL per line (`udp://host:port/announce`
  or `http(s)://…`). Omit the file to use moss's built-in default trackers.
- **`moss_static_peers.txt`** — one `host:port` per line to dial directly. Use for
  zero-tracker LAN play or to connect straight to a friend's public / VPN address.
- **`moss_psk.txt`** — a single 64-char hex string (32 bytes) for a closed,
  pre-shared-key-encrypted mesh. Everyone must use the identical value.

---

## Not supported: Proton / Wine

The moss runtime (`moss.dll`) is a Go library and its networking relies on
Windows IOCP, which **Wine/Proton do not implement well enough** — under
Wine it cannot bind its sockets at all (`Moss_Start` fails), so **moss P2P does
not work when the Windows build is run through Proton/Wine**. The emulator
detects this and falls back to LAN-only networking automatically (fast, no hang).

If you're on Linux and want online play:

- Use the **native Linux emulator build + `libmoss.so`** with a native Linux game
  (then moss runs natively and works), **or**
- For a Windows game under Proton/Wine, play over the legacy LAN transport via a
  **VPN (ZeroTier) + `custom_broadcasts.txt`** (this path uses classic Winsock,
  which Wine supports fine) instead of moss.

## If it won't connect

Most home routers can be traversed automatically (moss uses UPnP / NAT-PMP / PCP
plus hole-punching). If a direct path can't be held, use one of these:

1. **Port-forward** UDP **41666** on **one** player's router to that PC's local IP.
   The other player then connects to them directly and stably. This is the most
   reliable fix and only one side needs to do it.
2. **Use a room key.** Set the same `room_key` on both machines to share a private
   mesh — handy if you also want isolation from strangers.
3. **Use a VPN** (ZeroTier / Radmin / Hamachi) and list each other's VPN address
   in `moss_static_peers.txt`. Then trackers/NAT traversal aren't needed at all.
4. **CGNAT** (carrier-grade NAT, common on mobile/some ISPs) cannot be
   port-forwarded — use a VPN or have the non-CGNAT side host with a forwarded
   port.

### Reading the logs (debug build)

A debug build writes `STEAM_LOG_*.log` with `[MOSS-DIAG]` lines. Useful ones:

- `init_moss mesh='…' channel='…'` — confirms the mesh/channel you joined.
- `nat_type=…` — `full_cone` / `restricted_cone` / `port_restricted_cone` are
  traversable; `symmetric_nat` / `cgnat` usually need a VPN or port-forward.
- `moss transport peers=N` — how many mesh peers you're connected to.
- `moss-backed app connections=N` — how many game peers you've discovered.

---

## Building `moss.dll` yourself

```bash
# Windows x64
CGO_ENABLED=1 GOOS=windows GOARCH=amd64 go build -buildmode=c-shared -o moss.dll ./cmd/moss-ffi
# Linux x64
CGO_ENABLED=1 GOOS=linux   GOARCH=amd64 go build -buildmode=c-shared -o libmoss.so ./cmd/moss-ffi
```

The prebuilt Windows x64 runtime is vendored at `third-party/moss/`. For 32-bit
games build a 32-bit moss (`GOARCH=386`); otherwise 32-bit titles fall back to
LAN-only.

---

## How it works (brief)

The emulator's `Networking` layer already routes everything (lobbies, friends,
matchmaking, networking sockets, stats) through a single message type. moss is
wired in as a second transport next to LAN broadcast:

- The node joins mesh `global` (or `gse-room-<key>`) and subscribes to channel
  `gse-app-<appid>` (or `gse-room-<key>`).
- Presence and all messages are published to that channel; receivers filter by
  the destination Steam ID, exactly like the LAN path.
- A peer heard over moss is served over moss (the mesh is more reliable across
  NATs than the legacy direct-IP path).

See `docs/`/source comments and `dll/moss_transport.*`, `dll/network.cpp` for
details.
