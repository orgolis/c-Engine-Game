#pragma once

// ====================
// Runtime PakFile — memory-mapped bundle loader  (Master Plan Stage 2.2 step 5)
// ====================
//
// Closes the cook->runtime loop: `tools/assetcook` writes a `.pak`; at runtime
// PakFile memory-maps it and hands the bytes to PakReader, which returns
// ZERO-COPY spans into the mapping (the OS pages data in on demand — no parse,
// no heap copy). RAII unmaps on destruction.
//
//   PakFile pak;
//   if (pak.open("cooked/Koltuk.pak")) {
//       size_t n; const uint8_t* mesh = pak.get(scene_mesh_id("Koltuk", 0), n);
//       CookedMeshView v; v.open(mesh, n);
//   }

#include "pak.h"

#include <cstdint>
#include <string>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#else
#  include <fcntl.h>
#  include <sys/mman.h>
#  include <sys/stat.h>
#  include <unistd.h>
#endif

namespace schizo::assets {

// Generic read-only memory-mapped file (RAII). The zero-copy substrate for
// streaming: map any cooked file (e.g. a `.vt` virtual texture) and hand its
// bytes to a view — the OS pages data in on demand, so a streamer can touch one
// tile without faulting in the whole file. RAII unmaps on destruction.
class MappedFile {
public:
    MappedFile() = default;
    ~MappedFile() { close(); }
    MappedFile(const MappedFile&)            = delete;
    MappedFile& operator=(const MappedFile&) = delete;

    bool open(const std::string& path) {
        close();
#if defined(_WIN32)
        file_ = ::CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file_ == INVALID_HANDLE_VALUE) return false;
        LARGE_INTEGER li{};
        if (!::GetFileSizeEx(file_, &li) || li.QuadPart == 0) { close(); return false; }
        size_ = static_cast<size_t>(li.QuadPart);
        mapping_ = ::CreateFileMappingA(file_, nullptr, PAGE_READONLY, 0, 0, nullptr);
        if (!mapping_) { close(); return false; }
        data_ = static_cast<const uint8_t*>(::MapViewOfFile(mapping_, FILE_MAP_READ, 0, 0, 0));
        if (!data_) { close(); return false; }
#else
        fd_ = ::open(path.c_str(), O_RDONLY);
        if (fd_ < 0) return false;
        struct stat st {};
        if (::fstat(fd_, &st) != 0 || st.st_size == 0) { close(); return false; }
        size_ = static_cast<size_t>(st.st_size);
        void* p = ::mmap(nullptr, size_, PROT_READ, MAP_PRIVATE, fd_, 0);
        if (p == MAP_FAILED) { close(); return false; }
        data_ = static_cast<const uint8_t*>(p);
#endif
        return true;
    }
    void close() {
#if defined(_WIN32)
        if (data_)    { ::UnmapViewOfFile(data_); data_ = nullptr; }
        if (mapping_) { ::CloseHandle(mapping_);  mapping_ = nullptr; }
        if (file_ != INVALID_HANDLE_VALUE) { ::CloseHandle(file_); file_ = INVALID_HANDLE_VALUE; }
#else
        if (data_) { ::munmap(const_cast<uint8_t*>(data_), size_); data_ = nullptr; }
        if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
#endif
        size_ = 0;
    }
    bool           is_open() const { return data_ != nullptr; }
    const uint8_t* data()    const { return data_; }
    size_t         size()    const { return size_; }

private:
    const uint8_t* data_ = nullptr;
    size_t         size_ = 0;
#if defined(_WIN32)
    HANDLE file_    = INVALID_HANDLE_VALUE;
    HANDLE mapping_ = nullptr;
#else
    int fd_ = -1;
#endif
};

class PakFile {
public:
    PakFile() = default;
    ~PakFile() { close(); }
    PakFile(const PakFile&)            = delete;
    PakFile& operator=(const PakFile&) = delete;

    // Memory-map `path` and open a PakReader over it. Returns false (and leaves
    // the object closed) on any failure.
    bool open(const std::string& path) {
        close();
#if defined(_WIN32)
        file_ = ::CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file_ == INVALID_HANDLE_VALUE) return false;
        LARGE_INTEGER li{};
        if (!::GetFileSizeEx(file_, &li) || li.QuadPart == 0) { close(); return false; }
        size_ = static_cast<size_t>(li.QuadPart);
        mapping_ = ::CreateFileMappingA(file_, nullptr, PAGE_READONLY, 0, 0, nullptr);
        if (!mapping_) { close(); return false; }
        data_ = static_cast<const uint8_t*>(
            ::MapViewOfFile(mapping_, FILE_MAP_READ, 0, 0, 0));
        if (!data_) { close(); return false; }
#else
        fd_ = ::open(path.c_str(), O_RDONLY);
        if (fd_ < 0) return false;
        struct stat st {};
        if (::fstat(fd_, &st) != 0 || st.st_size == 0) { close(); return false; }
        size_ = static_cast<size_t>(st.st_size);
        void* p = ::mmap(nullptr, size_, PROT_READ, MAP_PRIVATE, fd_, 0);
        if (p == MAP_FAILED) { close(); return false; }
        data_ = static_cast<const uint8_t*>(p);
#endif
        if (!reader_.open(data_, size_)) { close(); return false; }
        return true;
    }

    void close() {
#if defined(_WIN32)
        if (data_)    { ::UnmapViewOfFile(data_); data_ = nullptr; }
        if (mapping_) { ::CloseHandle(mapping_);  mapping_ = nullptr; }
        if (file_ != INVALID_HANDLE_VALUE) { ::CloseHandle(file_); file_ = INVALID_HANDLE_VALUE; }
#else
        if (data_) { ::munmap(const_cast<uint8_t*>(data_), size_); data_ = nullptr; }
        if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
#endif
        size_   = 0;
        reader_ = PakReader{};
    }

    bool             is_open() const { return data_ != nullptr; }
    size_t           size()    const { return size_; }
    const PakReader& reader()  const { return reader_; }

    // Convenience forwards to the underlying reader (zero-copy).
    bool           has(AssetId id) const { return reader_.has(id); }
    const uint8_t* get(AssetId id, size_t& out_size) const { return reader_.get(id, out_size); }
    bool           verify(AssetId id) const { return reader_.verify(id); }

private:
    const uint8_t* data_ = nullptr;
    size_t         size_ = 0;
    PakReader      reader_;
#if defined(_WIN32)
    HANDLE file_    = INVALID_HANDLE_VALUE;
    HANDLE mapping_ = nullptr;
#else
    int fd_ = -1;
#endif
};

}  // namespace schizo::assets
