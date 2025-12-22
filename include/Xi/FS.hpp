// src/Xi/FS.hpp

#ifndef XI_FS_HPP
#define XI_FS_HPP 1

#include "String.hpp"
#include "Array.hpp"

// Platform specific includes
#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
    #include <io.h>
    #include <direct.h>
    #include <sys/types.h>
    #include <sys/stat.h>
    
    // Windows constants
    #ifndef S_ISDIR
    #define S_ISDIR(mode)  (((mode) & S_IFMT) == S_IFDIR)
    #endif
    #ifndef S_ISREG
    #define S_ISREG(mode)  (((mode) & S_IFMT) == S_IFREG)
    #endif
    #define F_OK 0
    #define W_OK 2
    #define R_OK 4
#else
    #include <sys/types.h>
    #include <sys/stat.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <dirent.h>
    #include <stdio.h>
#endif

namespace Xi {

enum class FSType {
    UNKNOWN = 0,
    FILE = 1,
    FOLDER = 2,
    SYMLINK = 3
};

struct FSStat {
    bool exists;
    bool read;
    bool write;
    bool execute;
    FSType type;
    u64 size;
    String symlinkTarget;

    FSStat() : exists(false), read(false), write(false), execute(false), 
               type(FSType::UNKNOWN), size(0) {}
};

class FS {
private:
    // Helper: Ensure we use the correct separator for the underlying OS calls
    static String toNativePath(const String& path) {
#if defined(_WIN32)
        // Copy and replace / with \ for Windows API
        String res = path;
        // Access raw data to replace in place
        // Xi::String doesn't expose mutable iterator easily, assume data() is mutable 
        // via const_cast logic inside String usually, or rebuild. 
        // Safe rebuild:
        return res.replace("/", "\\");
#else
        return path;
#endif
    }

#if defined(_WIN32)
    // Helper to read Windows Symlink/Reparse Point
    static String getWindowsReparseTarget(const char* path) {
        HANDLE hFile = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                                   OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
        if (hFile == INVALID_HANDLE_VALUE) return "";

        // Minimal buffer for REPARSE_DATA_BUFFER
        char buffer[16 * 1024];
        DWORD bytesReturned;
        String result = "";

        if (DeviceIoControl(hFile, FSCTL_GET_REPARSE_POINT, NULL, 0, buffer, sizeof(buffer), &bytesReturned, NULL)) {
            // Parse manual structure (typedefs omitted for brevity, assuming offsets)
            // REPARSE_DATA_BUFFER structure: ReparseTag(4) + ReparseDataLength(2) + Reserved(2)
            u32 tag = *((u32*)buffer);
            // IO_REPARSE_TAG_SYMLINK = 0xA000000C
            if (tag == 0xA000000C) {
                // SymbolicLinkReparseBuffer: SubstituteNameOffset(0), SubstituteNameLength(2), ...
                // Located at buffer + 8
                u8* data = (u8*)buffer + 8;
                u16 subOffset = *((u16*)data);
                u16 subLength = *((u16*)(data + 2));
                // PathBuffer follows headers (headers are 12 bytes for Symlink)
                wchar_t* wPath = (wchar_t*)(data + 12 + subOffset);
                
                // Convert WCHAR to UTF8 String
                int len = WideCharToMultiByte(CP_UTF8, 0, wPath, subLength / 2, NULL, 0, NULL, NULL);
                if (len > 0) {
                    ArrayFragment<u8>* f = ArrayFragment<u8>::create(len + 1);
                    WideCharToMultiByte(CP_UTF8, 0, wPath, subLength / 2, (char*)f->data, len, NULL, NULL);
                    f->length = len;
                    f->data[len] = 0; // Null term
                    // Construct string from buffer manually or via intermediate
                    result = String(f->data, len);
                    f->release();
                    
                    // Remove standard Windows prefix \??\ if present
                    if (result.find("\\??\\") == 0) result = result.begin(4, result.len());
                }
            }
        }
        CloseHandle(hFile);
        return result;
    }
#endif

public:
    static FSStat stat(const String& path) {
        FSStat s;
        if (path.len() == 0) return s;
        String native = toNativePath(path);
        const char* p = native;

#if defined(_WIN32)
        // Check attributes first for Reparse Point (Symlink)
        DWORD attrs = GetFileAttributesA(p);
        if (attrs != INVALID_FILE_ATTRIBUTES) {
            s.exists = true;
            if (attrs & FILE_ATTRIBUTE_REPARSE_POINT) {
                s.type = FSType::SYMLINK;
                s.symlinkTarget = getWindowsReparseTarget(p);
            } else if (attrs & FILE_ATTRIBUTE_DIRECTORY) {
                s.type = FSType::FOLDER;
            } else {
                s.type = FSType::FILE;
            }
            
            // Size via _stat64
            struct _stat64 st;
            if (_stat64(p, &st) == 0) {
                s.size = (u64)st.st_size;
            }
            
            // Access
            s.read = (_access(p, R_OK) == 0);
            s.write = (_access(p, W_OK) == 0);
            s.execute = s.read; // Approximate on Windows
        }
#else
        struct stat st;
        // Use lstat to not follow symlinks
        if (lstat(p, &st) == 0) {
            s.exists = true;
            s.size = (u64)st.st_size;
            
            if (S_ISLNK(st.st_mode)) {
                s.type = FSType::SYMLINK;
                char buf[4096];
                usz len = readlink(p, buf, sizeof(buf)-1);
                if (len != -1) {
                    s.symlinkTarget = String((u8*)buf, (usz)len);
                }
            } else if (S_ISDIR(st.st_mode)) {
                s.type = FSType::FOLDER;
            } else {
                s.type = FSType::FILE;
            }

            s.read = (access(p, R_OK) == 0);
            s.write = (access(p, W_OK) == 0);
            s.execute = (access(p, X_OK) == 0);
        }
#endif
        return s;
    }

    static String read(const String& path, u64 offset = 0, u64 length = 0) {
        String native = toNativePath(path);
        FILE* f = fopen(native, "rb");
        if (!f) return String();

        fseek(f, 0, SEEK_END);
        long fileSize = ftell(f);
        if (fileSize < 0) { fclose(f); return String(); }
        
        if (offset >= (u64)fileSize) { fclose(f); return String(); }

        u64 readLen = (length == 0) ? ((u64)fileSize - offset) : length;
        if (offset + readLen > (u64)fileSize) readLen = (u64)fileSize - offset;

        fseek(f, (long)offset, SEEK_SET);

        // Pre-allocate String
        // Trick: Xi::String doesn't have explicit resize, use repeated push or custom alloc logic
        // We will alloc a buffer and construct String
        u8* buf = new u8[readLen];
        usz actual = fread(buf, 1, readLen, f);
        fclose(f);

        String res(buf, actual);
        delete[] buf;
        return res;
    }

    static bool write(const String& path, const String& content, u64 offset) {
        String native = toNativePath(path);
        FILE* f = null;

        if (offset == 0 && content.len() > 0) {
            // Just overwrite or create
            // If offset is 0, do we want to truncate?
            // "Overwrite all the file" implies truncate.
            // "At specific offset, leave other things intact" implies r+b.
            
            // To differentiate overload behavior passed via params:
            // The prompt says: write(path, str) -> overwrite all.
            // write(path, str, offset) -> specific offset.
            // Since we merged into one C++ function with default param, we need to detect intent?
            // Actually, if user calls write(p, s, 0), it could mean "start at 0, keep rest".
            // But usually default param means standard overwrite.
            // Let's check existence.
        }

        bool modeOverwrite = false;

        // Try to open for update to preserve content
        f = fopen(native, "r+b");
        if (!f) {
            // File doesn't exist, create it
            f = fopen(native, "wb");
            modeOverwrite = true;
        }

        if (!f) return false;

        if (modeOverwrite && offset > 0) {
            // Created new file but want to write at offset. Fill with zeros up to offset.
            // Inefficient but safe.
            u8 z = 0;
            for(u64 i=0; i<offset; ++i) fwrite(&z, 1, 1, f);
        } else if (!modeOverwrite) {
             // File existed, seek to offset
             fseek(f, (long)offset, SEEK_SET);
        }

        if (offset == 0 && !modeOverwrite) {
            // Re-open in truncate mode
            fclose(f);
            f = fopen(native, "wb");
            if (!f) return false;
        }

        fwrite(content.data(), 1, content.len(), f);
        fclose(f);
        return true;
    }
    
    // Explicit Overload for "Overwrite all" logic to be safe
    static bool write(const String& path, const String& content) {
        String native = toNativePath(path);
        FILE* f = fopen(native, "wb");
        if(!f) return false;
        fwrite(content.data(), 1, content.len(), f);
        fclose(f);
        return true;
    }

    static bool unlink(const String& path) {
        String native = toNativePath(path);
#if defined(_WIN32)
        return _unlink(native) == 0;
#else
        return ::unlink(native) == 0;
#endif
    }

    static bool mkdir(const String& path) {
        String native = toNativePath(path);
#if defined(_WIN32)
        return _mkdir(native) == 0;
#else
        return ::mkdir(native, 0755) == 0;
#endif
    }

    static bool rmdir(const String& path, bool recursive) {
        String native = toNativePath(path);
        if (!recursive) {
#if defined(_WIN32)
            return _rmdir(native) == 0;
#else
            return ::rmdir(native) == 0;
#endif
        }

        // Recursive delete
#if defined(_WIN32)
        String searchPath = native + "\\*";
        WIN32_FIND_DATAA fd;
        HANDLE hFind = FindFirstFileA(searchPath, &fd);
        if (hFind == INVALID_HANDLE_VALUE) return false;

        do {
            if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
            
            String subPath = native + "\\" + fd.cFileName;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                rmdir(subPath, true);
            } else {
                _unlink(subPath);
            }
        } while (FindNextFileA(hFind, &fd));
        FindClose(hFind);
        return _rmdir(native) == 0;
#else
        DIR* d = opendir(native);
        if (!d) return false;
        struct dirent* p;
        while ((p = readdir(d))) {
            if (strcmp(p->d_name, ".") == 0 || strcmp(p->d_name, "..") == 0) continue;
            String subPath = native + "/" + p->d_name;
            
            struct stat st;
            if (lstat(subPath, &st) == 0) {
                if (S_ISDIR(st.st_mode)) {
                    rmdir(subPath, true);
                } else {
                    ::unlink(subPath);
                }
            }
        }
        closedir(d);
        return ::rmdir(native) == 0;
#endif
    }
};

} // namespace Xi

#endif // XI_FS_HPP