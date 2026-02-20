# Tunnel

`Xi::Tunnel` is a peer-to-peer session controller. It acts as the upper layer above `Xi::RailwayStation`, managing the exact lifecycle of a connection between two isolated computers.

While `RailwayStation` ensures that raw bytes map to the correct entity on a graph, `Tunnel` parses those bytes into a highly resilient state machine guaranteeing logical application-layer security, byte-order, and fragmentation integrity.

---

## ⚡ Speed & The AAA Gaming Advantage

Why is `Tunnel` faster than traditional websockets or `TCP` streams?

1. **Zero-Allocation Deserialization:**
   When a 1,400-byte UDP packet arrives, `Tunnel::parse()` does not construct a massive `std::vector` or heavy Object tree to represent it. It uses native substring parsing directly on the underlying `Xi::String` memory.
2. **True Unreliable Fast-Path (Bypassing HOL):**
   If you send a player movement packet via TCP and it drops, the entire game engine connection stalls (Head-of-Line Blocking). By default, `Tunnel` marks data as `important = true` (Reliable). However, game engines can flip a packet flag to `important = false` or `bypassHOL = true`.
   If a `bypassHOL` packet arrives, `Tunnel` immediately dispatches it to the application logic rather than holding it in a buffer waiting for the missing sequence to arrive, achieving absolute minimal latency jitter for High-Tickrate scenarios like First-Person Shooters.
3. **Selective Repeat ARQ:**
   Instead of archaic "Go-Back-N" algorithms, `Tunnel` uses an advanced Sliding Window bitmap (`receiveWindowMask`). When sending connection health heartbeats, it explicitly lists which packet IDs it _does_ have, and which micro-fragments are _missing_, allowing the peer to selectively re-transmit purely what plummeted over a bad LoRa connection.

---

## 📖 Deep Dive: The Protocol State Machine

A newly created `Xi::Tunnel` is simply a blank slate. It must organically discover its peer and negotiate a shared symmetric key before standard transmission begins.

### 1. Probing & Announcing (Discovery)

To connect two systems without hardcoding their IP addresses, `Tunnel` uses an unencrypted discovery mechanism via specific Channel 0 packet flags (Type 10/11).

- `tunnel.probe(metadata)`: Used by a **Client**. It broadcasts a generic ping into the ether (usually over `Railway` Multicast rail) asking "Is anyone listening?". The `metadata` can contain the client's hostname or desired game lobby.
- `tunnel.onProbe()` / `tunnel.announce(server_metadata)`: Used by a **Server**. When the server hears a Probe, it fires `.announce()` back directly to the Client.
- **The Handshake:** Once the Client receives the Announce via `.onAnnounce()`, it "locks on" to the Server's address and they proceed to encrypt the connection.

### 2. Application Data & Fragmentation

Once running, developers natively push and receive conceptual app-level packets.
The developer _never thinks about the maximum byte limits of the radio network_.

- `tunnel.push(payload, channel_id)`
  If the user attempts to `push` a massive 5-Megabyte map update or JPEG image across the `Tunnel`, attempting to send it over UDP in one chunk will instantly exceed the OS-level socket MTU limits (Maximum Transmission Unit).

When the `tunnel.flush()` command physically pulls the internal Outbox into wire-ready packets, it transparently limits sizes based on the provided parameter `tunnel.flush(32 /* BlockSize */, 1400 /* MTU */)`.
The `Tunnel` meticulously fragments the mega-string into `[Start]`, `[Middle]`, and `[End]` chunks natively. When the remote peer receives the fragments piece by piece, it holds them in an internalized Reassembly Buffer. Once complete, it fires `.onPacket()` as a unified whole.

---

## 📖 Lifecycle Integration

The C++ Developer interfaces with the `Tunnel` inside deterministic `update()` game loops, pulling cleanly constructed bytes out for transmission.

```cpp
Xi::Tunnel session;

// Frame Loop (60 FPS)
void loop() {
  // 1. Process Timeouts
  session.update();

  // 2. Generate exactly what bytes we need to push to the UDP Socket
  // This automatically extracts outbox payloads, handles ARQ Resends,
  // builds X25519 Handshake requests, and ensures payloads fit within 1400 bytes.
  while (true) {
     Xi::String outputChunk = session.flush(32, 1400);
     if (outputChunk.length() == 0) break;

     myHardwareSocket.send(outputChunk);
  }
}
```

### Callbacks

- `tunnel.onPacket([](Xi::Packet p) { ... })`
  Fired when a fully reconstructed application-level payload successfully arrives.
- `tunnel.onDisconnect([](Xi::Map<u64, String> reason) { ... })`
  Fired manually, or automatically if `tunnel.update()` realizes the `.lastSeen` heartbeat counter has exceeded the `disconnectTimeout` threshold (e.g. `120000ms`).
- `tunnel.onDestroy([]() { ... })`
  A hard-cleanup callback used to instantly destroy routing logic if the peer is permanently lost or kicked, signaling the `RailwayStation` to unhook from the `Hub`.
