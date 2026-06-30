# moss P2P runtime (vendored)

This folder holds the prebuilt [moss](https://github.com/redstone-md/moss) P2P core
that the emulator loads **dynamically at runtime** to provide internet-wide
multiplayer (lobbies, friends, matchmaking, networking sockets) on top of a
NAT-traversing, tracker-bootstrapped, Noise-encrypted mesh.

The emulator never links against moss at build time. At startup the `Networking`
layer calls `LoadLibrary("moss.dll")` / `dlopen("libmoss.so")`. If the library is
absent or fails to load, the emulator silently falls back to the legacy LAN
UDP/TCP broadcast transport — moss is purely additive.

## Files
- `moss.dll` — Windows x86-64 shared library
- `moss.h`   — generated C FFI header (reference only; the emulator inlines the
  needed typedefs in `dll/dll/moss_transport.h`)

## Placement at runtime
`moss.dll` must be discoverable by the game process. The default Windows search
order looks next to the **game executable**, so place `moss.dll` in the same
folder where you drop `steam_api(64).dll`.

## Architecture note
The vendored `moss.dll` is **64-bit**. For 32-bit games build/obtain a 32-bit
moss (`GOARCH=386`) and ship that instead; otherwise 32-bit titles transparently
fall back to LAN-only networking.

## Rebuilding moss
```bash
# Windows x64
CGO_ENABLED=1 GOOS=windows GOARCH=amd64 go build -buildmode=c-shared -o moss.dll ./cmd/moss-ffi
# Linux x64
CGO_ENABLED=1 GOOS=linux   GOARCH=amd64 go build -buildmode=c-shared -o libmoss.so ./cmd/moss-ffi
```
