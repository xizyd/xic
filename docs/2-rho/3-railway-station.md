# Railway Station

`Xi::RailwayStation` provides the topological routing structure for the Rho protocol.
It implements a decentralized, "onion-styled" message router that enables complex one-to-one, one-to-many, and many-to-one network graphs over completely lossy or broadcast physical layers (like UDP, LoRa, or a shared UART bus).

---

## 🚉 The Graph Topology

Instead of a centralized `Server` class managing a gigantic dictionary of `Client` sockets, `Railway` represents every single entity as a discrete `Xi::RailwayStation`.

You explicitly link these stations together in memory using `.addStation(otherStation)`, creating a directional parent-child hierarchy representing your specific networking model (whether it be a Star Topology, a Web Mesh, or a simple Point-To-Point link).

```cpp
#include "Xi/Rho/Railway.hpp"

// The absolute physical hardware root.
// Anything pushed out of this station hits the physical radio/network socket.
Xi::RailwayStation physicalRoot;

// A virtual station representing an internal App service.
Xi::RailwayStation gameEngine;

// We link them together!
// Now, anything arriving at `physicalRoot` filters down into `gameEngine`.
// And anything `gameEngine` pushes, trickles upward into `physicalRoot`.
physicalRoot.addStation(gameEngine);
```

---

## 🛤️ Rail Filters: Broadcast, Multicast, Anycast

A `RawCart` traveling across a Railway must know who it belongs to. This is done using a fundamental grouping ID called a `rail` (effectively a dynamic port number).

Each `RailwayStation` configures its behavior based on three crucial variables:

1. `st.rail` (A `VarLong` `0-N`)
2. `st.anycast` (Boolean)
3. `st.allDrain` (Boolean)

### 1. Broadcast Mode (`rail = 0`)

Intended for physical Root Hubs, Listeners, or Debuggers.
If `st.rail = 0` and `st.anycast = false`, the station **receives everything**. Any `RawCart` arriving that is not explicitly marked as Anycast will immediately trigger this station's `.onCart()` / `.onRawCart()` listeners.

- _Note: If `st.allDrain = true` (default), it will literally intercept **everything** regardless of Anycast flags. Excellent for Gateway bridging nodes._

### 2. Multicast Mode (`rail = N`)

Intended for open group channels, group chats, or unencrypted pairing discovery.
If a station binds to `st.rail = 1337`, it will exclusively receive Carts also specifically destined for `1337` (with Anycast `false`).

### 3. Anycast Mode (`rail = N`, `anycast = true`, Notated as `N#`)

Intended for 1-to-1 secure application data.
Once two specific peers agree to establish a Point-to-Point connection, they both negotiate an arbitrary Rail ID (via `st.enrail()`), and flip their exact `st.anycast = true`.

- **CRITICAL:** Carts sent with the `Anycast` bit perfectly bypass all standard `Broadcast (0)` stations along the mesh network (unless those stations are explicitly running as unrestricted `allDrain` Gateways). This prevents massive packet spam inside discovery queues.

---

## 🔒 Onion Security & Metadata Diffing

`RailwayStation` is completely independent of the actual encrypted payload, but it manages the Cryptographic Envelope natively.

A single station can be switched to `isSecure = true` by passing it an agreed-upon 32-byte ChaCha20 key.

- When a secure station `.push(data)`, it completely encrypts the interior payload AND calculates an ultra-fast `Poly1305` HMAC signature over the packet `Header`, `Nonce`, inner `Data`, and inner `Meta`.
- It dynamically wraps this into a `RawCart` format, appending the unencrypted (but HMAC signed) `Nonce` to the frame, ensuring replay-attack immunity.

### Metadata Aggregation

Stations natively possess a dynamic Map named `st.meta`. This allows nodes to attach key/value pairing data transparently along the route.

- If a client pushes data from a deeply nested station `A -> B -> C`, `B` can append its own diagnostic data (like signal strength `RSSI`) into the `.meta` dictionary without decrypting `A`'s underlying application payload.
- As an optimization, a station continually tracks `.theirMeta` so that it mathematically **only sends metadata deltas**, preventing bloat over the wire on static values.

---

## 💻 Listeners & Traffic Hooks

Connecting business logic dynamically occurs via non-allocating `Xi::Func` lambdas.

```cpp
// 1. App-Level Payload Listener
// Triggers only when a Cart successfully passes all rail filtering AND cryptographic checks.
st.onCart([](Xi::String data, u64 originalRail, Xi::RailwayStation* origin){
    Xi::println("Received Payload: " + data);
});

// 2. Hardware-Level Outbox Listener
// Used on the absolute Root station.
// Triggers when a Cart is fully constructed, encrypted, and structurally
// ready to be blasted across the physical `WiFiUDP` or `Serial` port.
st.onOutboxRawCartListener([](u8 header, u64 nonce, Xi::String hmac, Xi::String cipherText, Xi::RailwayStation* origin){

    // We physically serialize these raw buffers out over UDP natively
    Xi::String wireFrame = Xi::RailwayStation::serializeCart(header, nonce, hmac, cipherText);
    udp.send(wireFrame);
});
```
