# The X25519 Handshake

While `Xi::Tunnel` handles fragmentation, ARQ (Automatic Repeat reQuest), and application callbacks, its true architectural marvel is the **X25519 Ephemeral Key Exchange**.

This protocol deep-dive explains how two disconnected machines—whether an ESP32 and a Linux server, or two LoRa radios—securely establish an AES-level ChaCha20 symmetric key over a public airwave without relying on Certificate Authorities (CAs) or TLS.

---

## 🔒 The TLS 1.3 Problem

Standard Transport Layer Security (TLS) is incredibly heavy. When an IoT device connects to a WebSocket over `wss://`:

1. The server blasts highly structured X.509 Certificates down to the client.
2. The client must parse thousands of bytes of ASN.1 encoded structures, attempting to verify signatures against root CAs using massive RSA calculations.
3. This consumes hundreds of kilobytes of RAM, completely halting the ESP32's CPU, and often triggering watchdog timer crashes.

**Rho completely bypasses this via a pure P2P Forward Privacy model over UDP.**

---

## ⚡ The Ephemeral Exchange (Step-by-Step)

The `Tunnel` module abstracts the complex Monocypher C-bindings into a frictionless single-RTT step process using Type 20 packets.

### Step 1: The Switch Request (Public Key Broadcast)

Let's assume **Node A** (Client) wants to securely talk to **Node B** (Server). They currently "discovered" each other via unencrypted UDP `probe()` and `announce()` packets.

Node A decides to lock the connection.

1. **Math Generation:**
   Node A executes `Xi::generateKeyPair()` inside `.initEphemeral()`. This relies strictly on the `Xi::Crypto` hardware randomness pool binding (e.g., `esp_random()` or `/dev/urandom`). It calculates a 32-byte Secret Key, and mathematically derives a 32-byte Public Key.
2. **Buffer Packaging:**
   Node A bundles this Public Key string via `.generateSwitchRequest()` into a special Type 20 packet and sends it across the wire.
   _(Because Elliptic-Curve math is one-way, even if a hacker catches the SwitchRequest packet in the air, they cannot mathematically reverse the Public Key to discover the Secret Key)._

### Step 2: The Peer Responds

Node B (the Server) receives the `SwitchRequest` in `.parsePacket(type == 20)`. This fires the `tunnel.onSwitchRequest()` callback.

1. **Mirror Generation:** Node B immediately fires `.initEphemeral()` locally (if it hasn't already), generating its own unique 32-byte Secret Key and 32-byte Public Key.
2. **The Magic Calculation:** Node B executes `.enableSecurityX()`.
   Internally, this triggers `Xi::sharedKey(MySecretKey, TheirPublicKey)`.
   This is an ECDH (Elliptic Curve Diffie-Hellman) multiplication. The result is a mathematically unified, 32-byte "Shared Secret".
   It then passes this shared secret through the rigorous `Xi::kdf` expander:
   `Xi::String newKey = Xi::kdf(shared, "RhoPufferV1", 32);`
3. **The Return Volley:** Node B packages its newly generated Public Key into a return `SwitchRequest` packet, and blasts it back to Node A over UDP. Node B's encryption door is locked.

### Step 3: Connection Locked!

Node A receives the return `SwitchRequest` containing Node B's Public Key.

1. Node A also executes `tunnel.enableSecurityX()`.
2. Because of the brilliance of ECDH math, Node A arrives at the _exact same 32-byte Shared Secret_ that Node B generated in Step 2.
3. The KDF evaluates the identical master key material.

The `isSecure` flag on both `Tunnel` instances definitively flips to `true`.

Both machines now possess an identical, cryptographically secure AES-level key without ever transmitting it over the wire. From this exact microsecond forward, all data built by `.flush()` is instantly routed through the AEAD ChaCha20-Poly1305 cipher, rendering the UDP tunnel completely dark to the outside world.

---

## 🛡️ Perfect Forward Secrecy

Because `Rho` generates brand new, randomized Ephemeral keys every single time `.initEphemeral()` is called, it inherently guarantees **Perfect Forward Secrecy (PFS)**.

Even if a hostile entity records 10 hours of chaotic encrypted UDP traffic between your ESP32 and Linux server, and then later physically steals the ESP32 hardware to extract its memory, they _still_ cannot decrypt the recorded traffic!

The keys existed purely in volatile RAM during the session and are permanently destroyed the moment `tunnel.onDestroy()` fired or the device rebooted.
