# Path (URL \u0026 URI)

`Xi::Path` is a robust, allocation-lightweight URL and URI parser.

When dealing with HTTP routing algorithms on a Web Server, or managing REST-like endpoints on an ESP32 IoT device, standard string splitting and URL Decoding rapidly degrades performance. `Xi::Path` instantly destructs complex raw URL strings into easily digestible, highly optimized data components.

---

## Architectural Overview: Smart Resolution

URL parsing is notoriously complex in C++. `Xi::Path` abstracts the painful edge cases logic of relative resolution, Query Strings (`?key=value`), and Hexadecimal URL Decodes (`%20`).

It is designed to intelligently fuse path segments together. For example, if a client requests `relativeTo(parent)`:

- `Xi::Path("/api/v1").relativeTo(Xi::Path("http://server.com"))` seamlessly outputs `http://server.com/api/v1`.

### Core Features

1. **Zero-Exception Parsing:** A malformed URL (`http:///broken//path?&&`) will not crash the binary. The parser naturally ignores sequential extraneous slashes and truncates violently malformed queries safely into empty `Xi::String` fields.
2. **Built-in Query Maps:** The query strings (`?id=42&sort=desc`) are automatically transformed into a high-performance `Xi::Map<String, String>` dictionary during instantiation for extremely fast `O(1)` Robin-Hood lookups in your application logic.
3. **Array Segments:** The path (`/api/users/4`) is automatically cut into a contiguous `Xi::Array<String>` (`["api", "users", "4"]`).

---

## 📖 Complete API Reference

### 1. Construction \u0026 Resolution

The constructor dynamically detects whether you are passing a full absolute URI (`https://...`) or merely a relative local route (`/css/style.css`).

- `Path()`
  Constructs an empty path.
- `Path(const String &path)`
  The core parser. Automatically strips out the `protocol`, `hostname`, `port`, `segments`, and the `queryMap`. Runs URL Decodes (e.g., converting `%20` to ` `) on all extracted values seamlessly.
- `Path(const Array<String> &paths)`
  Constructs a complete hierarchy organically from a raw array of folder strings.
- `String relativeTo(const Path &parent)`
  A powerful routing abstraction. By feeding it a `parent` base, it calculates the exact string required to reach the target URL.

### 2. URI Components (Properties)

These methods return the extracted components directly as `Xi::String` objects natively for use.

- `String protocol()`: e.g., `"https"` or `"ws"`
- `String hostname()`: e.g., `"api.example.com"` or `"192.168.1.5"`
- `String port()`: e.g., `"8080"` (Defaults to empty if absent)
- `String host()`: Fuses `hostname:` and `port` automatically.

### 3. Path Segments \u0026 Queries

- `String basename()`
  Returns the very last segment in the path array (e.g., `index.html`).
- `Array<String> getSegments()`
  Returns the physical folder hierarchy list. (e.g., `["users", "list"]`)
- `Map<String, String> getQuery()`
  Returns the pre-computed dictionary of query parameters.
- `String getQuery(const String &key)`
  Direct accessor. Returns the decoded value associated with the `key` (e.g., `?search=hello` -> `path.getQuery("search")` returns `"hello"`), or an empty String if absent.

### 4. Serialization \u0026 Formatting

If you organically built a local `Xi::Path` in memory by editing its internal map or pushing string segments, you can forcefully export it out to a raw string representation for the HTTP Socket.

- `String toString(bool forwardSlash, bool protocolAndHost, bool withQuery)`
  Dynamically synthesizes the Path layout into a singular raw bytes string.
  - `forwardSlash = true`: Wraps the output with an absolute leading `/` or trailing `/`.
  - `protocolAndHost`: If `true`, injects `ws://host:port` to the very front if available.
  - `withQuery`: Assesses the `_queryMap` and forcefully regenerates the `?a=1&b=2` suffix string onto the back.

---

## 💻 Example: Handling an IoT REST Request

```cpp
#include "Xi/Path.hpp"
#include "Xi/Log.hpp"

// Pretend an incoming HTTP GET request arrived over the socket.
void handleGetRequest(Xi::String rawRequestLine) {
    // Input: "GET /device/lights/dimmer?value=50&mode=fade HTTP/1.1"

    // We isolate just the URL portion and parse it!
    Xi::Path route = Xi::Path("/device/lights/dimmer?value=50&mode=fade");

    // 1. Array-based Routing check
    Xi::Array<Xi::String> segments = route.getSegments();

    if(segments.size() > 0 && segments[0] == "device") {

        // Output: "dimmer"
        Xi::println(route.basename());

        // 2. Query Parameter HashMap Lookup (O(1))
        Xi::String brightness = route.getQuery("value");
        Xi::String mode = route.getQuery("mode");

        // 3. Output to log
        Xi::print("Setting brightness to: ");
        Xi::println(brightness);
    }
}
```
