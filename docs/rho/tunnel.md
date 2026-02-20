# Tunnel

`Xi::Tunnel` is a peer-to-peer session controller. It manages the exact lifecycle of a connection between two isolated computers.

Its primary job is taking raw, unstructured `Xi::String` bytes pulled perfectly off a network socket (like UDP) and parsing them through a highly resilient state machine that guarantees security, order, and integrity.

---

## ⚡ Speed & The AAA Gaming Advantage

Why is `Tunnel` faster than traditional websockets or `TCP` streams?

1. **Zero-Allocation Deserialization:**
   When a 1,400-byte UDP packet arrives, `Tunnel::parse()` does not construct a massive `std::vector` or heavy Object tree to represent it. It uses `Xi::String::peekVarLong()` explicitly on the underlying memory buffer to read the packet headers `[Type][ID][Channel]`.
2. **True Unreliable Fast-Path (Bypassing HOL):**
   If you send a player movement packet via TCP and it drops, the entire game engine connection stalls (Head-of-Line Blocking). By default, `Tunnel` marks data as `important = true` (Reliable). However, game engines can flip a packet flag to `important = false` or `bypassHOL = true`.
   If a `bypassHOL` packet arrives slightly out of order, `Tunnel` immediately dispatches it to the application logic rather than holding it in a buffer waiting for the missing sequence to arrive, achieving absolute minimal latency jitter for High-Tickrate scenarios like First-Person Shooters.
3. **Static Mutexing/Threading:**
   There are no background polling threads. A `Tunnel` object only computes memory exactly when you tell it to via `tunnel.update()` or `tunnel.parse(bytes)`. This makes it 100% deterministic inside tightly constrained environments like an ESP32 `loop()` or a Unity/Unreal Engine `Update()` frame.

---

## 📖 Deep Dive: The Protocol State Machine

A newly created `Xi::Tunnel` is simply a blank slate. It must organically discover its peer and negotiate a shared symmetric key before standard transmission begins.

### 1. Probing & Announcing (Discovery)

To connect two systems without hardcoding their IP addresses, `Tunnel` uses an unencrypted discovery mechanism.

- `tunnel.probe(metadata)`: Used by a **Client**. It broadcasts a generic ping into the ether (usually over a Multi-Cast or Broadcast UDP address) asking "Is anyone listening?". The `metadata` can contain the client's hostname or desired game lobby.
- `tunnel.onProbe()` / `tunnel.announce(server_metadata)`: Used by a **Server**. When the server hears a Probe, it fires `.announce()` back directly to the Client.
- **The Handshake:** Once the Client receives the Announce, it "locks on" to the Server's address and they proceed to encrypt the connection.

### 2. The Exchange (Security)

TLS 1.3 takes ~10,000 lines of OpenSSL C-code and multiple round trips to establish a secure connection. `Tunnel` achieves the exact same Forward-Secrecy using a pristine **X25519 Elliptic-Curve Diffie-Hellman (ECDH)** single-round-trip architecture.

When either side wants to go private, it generates a Switch Request.

- `tunnel.generateSwitchRequest(peerPublicKey)`
  It creates a mathematically irreversible 32-byte public key string and bundles it. The peer receives it via `.onSwitchRequest()`.
  Once both sides have exchanged `SwitchRequests`, they call:
- `tunnel.enableSecurityX()`
  This triggers the `Crypto` module to multiply their Private Key against the peer's Public Key, resulting in an identical shared symmetric 32-byte hash simultaneously on both computers over a public airwave. The `isSecure` boolean flips to `true`.

### 3. Application Data & Fragmentation

Once running, developers push standard logical payloads into the Tunnel.

- `tunnel.push(payload, channel_id)`
  If the user attempts to `push` a massive 5-Megabyte map update or JPEG image across the `Tunnel`, attempting to send it over UDP in one chunk will instantly crash the OS-level socket MTU limits (Maximum Transmission Unit).

Instead of failing, the `Tunnel` transparently slices the 5MB payload into tiny, independent 1400-byte `Fragment` packets, numbering them sequentially, and streaming them across the network.

- _Note: When the `.build()` command fires to pull the packet bytes out of the Tunnel for the network socket, it handles the framing automatically._

When the remote peer receives the fragments piece by piece, it holds them in an internalized `Map` buffer. Only when the final fractional piece arrives does the `Tunnel` perfectly reconstruct the final 5MB `Xi::String` payload and fire the standard `.onPacket()` callback to the application as a unified whole.

---

## 📖 Lifecycle Callbacks

The C++ Developer interfaces with the `Tunnel` entirely asynchronously using lambda closures (powered by the zero-allocation `Xi::Func`).

- `tunnel.onPacket([](Xi::Packet p) { ... })`
  Fired when a fully reconstructed application-level payload successfully arrives.
- `tunnel.onDisconnect([](Xi::Map reason) { ... })`
  Fired manually, or automatically if `tunnel.update()` realizes the `.lastSeen` heartbeat counter has exceeded the timeout threshold.
- `tunnel.onDestroy([]() { ... })`
  A hard-cleanup callback used to instantly destroy routing logic if the peer is permanently lost or kicked.
