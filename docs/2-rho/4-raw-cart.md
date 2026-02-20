# RawCart Specification

The `Rho` protocol strictly leverages an internal physical serialization mechanism called the `RawCart`.
When a `RailwayStation` decides to route data over the actual physical wire (such as passing the buffer down to a Linux UDP socket or an Arduino UART pin), it compiles the business logic into this hyper-optimized byte structure.

---

## The Physical Byte Layout

Unlike JSON payloads or bloated WebSocket frames, a `RawCart` utilizes a dense variable-length architecture, often minimizing unnecessary overhead to just a few bytes.

The highest-level conceptual structure on the wire is:

`[HeaderByte: 1 Byte] [Nonce: VarLong] [HMAC: 8 Bytes] [Ciphertext]`

_Note: If Security is disabled, the `Nonce` and `HMAC` are entirely omitted, making the packet instantly smaller._

---

### 1. The HeaderByte

The absolute first byte of any `RawCart` defines the structural mapping for the rest of the stream.

- **Bit 0 (`isSecure`):** If set `1`, the packet includes a cryptographically secure `Nonce` and `HMAC` signature. The `Ciphertext` section is verified using `aeadOpen`.
- **Bit 1 (`hasMeta`):** If set `1`, the station has dynamically wrapped a `Map<u64, String>` metadata block into the inner payload.
- **Bit 2 (`anycast`):** If set `1`, the packet bypasses standard broadcast `.rail = 0` listeners and only targets identical `N#` point-to-point stations.

### 2. The Cryptographic Blocks

If `Bit 0 (isSecure)` is true:

- **Nonce (VarLong):** A strictly incrementing counter tracked by the station. Encoded using `Xi::String::pushVarLong()` to compress small counters (e.g. `150`) into a single byte rather than a rigid 8-byte uint64.
- **HMAC (8 Bytes):** The resulting ChaCha20-Poly1305 authentication tag. `Rho` explicitly truncates the standard 16-byte Poly1305 MAC down to 8 bytes. While cryptographically less dense, an 8-byte signature provides a $1$ in $1.8 \times 10^{19}$ probability of a collision, preventing tampering while vastly accelerating extreme LoRa bandwidth constraints. The signature validates `poly1305(HeaderByte + Nonce + Rail + Data + Meta)`.

### 3. The `Ciphertext` Structure

Whether physically encrypted via ChaCha20, or just passed as plaintext, the final blob of bytes holds the functional components.

The `Ciphertext` decompresses into:

`[Rail: VarLong] { InnerPayload }`

- **Rail (VarLong):** The routing ID `0-N` utilized by the Hub station to find which child `RailwayStation` this packet targets.

The `{ InnerPayload }` then decompresses again perfectly:

`[Data Length: VarLong] [Data Bytes] + [Optional Meta Block]`

- If `Bit 1 (hasMeta)` is `1` on the `HeaderByte`, the back-half of the `InnerPayload` contains a serialized `Xi::Map` byte stream. The exact encoding specifies a `[size: VarLong]` followed sequentially by `[key: VarLong][value: String]` iterations.

---

## The Parsing Pipeline

1. **Hardware Ingestion:** Bytes arrive from the OS (Unix `/dev/ttyUSB0` or `recvfrom()`).
2. **Deserialization Phase:** The raw buffer is passed to `Xi::RailwayStation::deserializeCart(bytes)`. This mechanically splits the `Header`, `Nonce`, `HMAC`, and unparsed `Ciphertext` off the front stack organically.
3. **Graph Hooking:** The structure is fed into the absolute root's `.pushRaw()`.
4. **Decryption Phase:** The root `RailwayStation` looks at `Header Bit 0`. If `true`, it authenticates the `HMAC` against the `Ciphertext` bytes.
5. **Topology Routing:** It peeks at the `Rail: VarLong` hidden within the Ciphertext, and instantly loops over its immediate `.parentStations` array.
   - If a child station mathematically matches the `Rail` based on the `Anycast` flag rules, the data is pushed down perfectly as a new logical `Xi::String`.
