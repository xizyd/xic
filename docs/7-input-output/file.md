# File System (WIP)

> [!WARNING]
> The `Xi::FS` abstraction layer is currently a **Work In Progress**. It wraps fundamental OS-level operations (like `fread()`, `<sys/stat.h>`, `<windows.h> Reparse Points`) but does not yet natively interface with ESP32 SPIFFS or LittleFS directly.

`Xi::FS` provides a completely cross-platform suite of tools wrapped around the `Xi::String` allocator mechanism for basic disk access.

---

## 📖 API Reference

### 1. Synchronous File Reading

- `Xi::String Xi::FS::read(const Xi::String& path, u64 offset = 0, u64 length = 0)`
  Reads a binary block from disk. If `length` is `0`, it dynamically pulls the entire file into a single `Xi::String` buffer up to the available system heap.

### 2. Synchronous File Writing

- `bool Xi::FS::write(const Xi::String& path, const Xi::String& content)`
  Overwrites or creates a file perfectly with the requested binary sequence. Returns `true` on success.
- `bool Xi::FS::write(const Xi::String& path, const Xi::String& content, u64 offset)`
  Performs an inline edit of an existing file buffer precisely at the requested `offset`.

### 3. File System Metadata

- `Xi::FSStat Xi::FS::stat(const Xi::String& path)`
  Returns a synchronized struct mapping:
  - `exists` (Boolean)
  - `type` (`FSType::UNKNOWN`, `FSType::FILE`, `FSType::FOLDER`, `FSType::SYMLINK`)
  - `size` (u64 Bytes)
  - `symlinkTarget` (Dynamically parses POSIX and modern Windows Reparse Pointers)
  - `read`, `write`, `execute` permission flags.

### 4. Direct Operations

- `bool Xi::FS::unlink(const Xi::String& path)` (Deletes a file object)
- `bool Xi::FS::mkdir(const Xi::String& path)` (Creates a single directory)
- `bool Xi::FS::rmdir(const Xi::String& path, bool recursive)` (Recursively obliterates a folder and all children if `recursive = true`).
