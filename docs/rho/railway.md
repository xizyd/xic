# Railway & Station Philosophy

If `Xi::Tunnel` guarantees that a message securely travels from Point A to Point B, `Xi::RailwayStation` guarantees that the message knows **where** Point B actually is in a chaotic, multi-client ecosystem.

The `Railway` module is a high-performance multiplexing and cryptography engine. It enables you to instantly route encrypted traffic across thousands of concurrent clients without allocating thread-pools or managing complex hashtables.

---

## 🚉 The "Station" Philosophy

In standard networking (like Node.js WebSockets or Boost.Asio), a Server manages a massive array or dictionary of active client socket objects. When the server wants to broadcast a message, it writes a `for` loop that iterates over every socket and calls `.send()`. This tightly couples the business logic directly to the hardware socket layer.

`Rho::Railway` flips this paradigm on its head by treating data routing like physical train stations.

Every entity—whether it is the Main Server, a background service, or an individual remote Client—is represented by a single `Xi::RailwayStation` object.

### The Topological Graph

You explicitly link stations together in memory using `.addStation()`.

1. **The Central Hub:** The Server bootup sequence creates a primary `allDrain` Hub Station.
2. **The Clients:** When a brand new UDP connection is established, the Server spins up a lightweight child `RailwayStation` for that specific user, and "plugs" it directly into the Hub Station using `hub.addStation(client)`.
3. **The Ripple Effect:** When the Server's business logic calls `hub.push(message)`, the Hub Station doesn't actually talk to the internet. It simply takes the message and instantly duplicates it, pushing it down into every single attached child `RailwayStation`.
4. **The Exit Nodes:** The child stations encrypt the message using their unique user keys, and output the raw encrypted `RawCart` bytes through the `.onOutboxRawCartListener()` hardware callback out to the OS Network socket.

This creates a completely stateless, extremely fast routing graph where business logic lives at the absolute top of the tree, entirely decoupled from the actual networking hardware or UDP multiplexing structure.

---

## 📖 Cartography: The `RawCart` Protocol

When `Railway` transmits data across the physical airwaves, it packages it into a `Xi::RawCart`. A Cart is a densely packed binary frame designed to be cryptographically validated before it is even parsed.

### The Physical Byte Structure

The spec defines building a Cart securely over the wire as:
`[Header: 1 Byte] [Nonce: VarLong] [HMAC: 8 Bytes] [Rail: VarLong] { Encrypted Payload: [Data] [Meta] }`

1. **Header Byte:** Identifies if this is a raw unencrypted ping, or a secured user payload.
2. **Nonce (VarLong):** An incrementing cryptographic sequence integer to absolutely prevent replay attacks.
3. **HMAC (Truncated 8 Bytes):** The station calculates a cryptographic hash signature of the entire packet using its secret Key. If an attacker modifies even a single bit of the packet mid-flight over LoRa or WiFi, the HMAC mismatch instantly rejects the packet with zero exceptions thrown.
4. **Rail ID (VarLong):** The specific routing integer.

### The Parsing Mechanism

When bytes fly in from the OS socket, the developer throws them straight into the Hub Station via `.pushRaw()`.

The Hub parses the `Rail ID` out of the packet header. It then instantly scans its attached child stations to see if any child possesses that specific `Rail ID`.
If it finds a match, it hands the raw bytes to the child. The child verifies the HMAC, decrypts the payload, and fires its friendly `.onCart()` callback precisely for that user's business logic.

---

## 💻 Example: Building a Chat Server Graph

```cpp
#include "Xi/Rho/Railway.hpp"
#include <iostream>

Xi::RailwayStation serverHub;

// An incoming new client connects to our game
Xi::RailwayStation player1;
player1.rail = 1005; // Their unique routing ID
player1.name = "Player 1";

// We attach the player to the master graph
serverHub.addStation(player1);

// ... Later ...

void broadcastWin() {
  // We push "You Win!" to the Server Hub.
  // The Hub instantly trickles this down to player1, encrypting it
  // and triggering their outbox to the UDP socket seamlessly.
  serverHub.push("Match Finished: You Win!");
}
```
