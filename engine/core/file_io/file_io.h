#pragma once

#include <string>
#include <vector>
#include <filesystem>

namespace gws::file_io {

// ====================
// File I/O System
// Cross-platform abstraction for reading/writing files
// ====================

using Path = std::filesystem::path;

// ====================
// File Buffer
// Simple wrapper around file data
// ====================

struct FileBuffer {
    std::vector<uint8_t> data;
    
    FileBuffer() = default;
    explicit FileBuffer(size_t capacity) : data(capacity) {}
    
    // Convert buffer to string view (for text files)
    std::string_view AsString() const {
        return std::string_view(reinterpret_cast<const char*>(data.data()), data.size());
    }
    
    // Get data pointer
    const uint8_t* GetData() const { return data.data(); }
    uint8_t* GetData() { return data.data(); }
    
    // Get size in bytes
    size_t GetSize() const { return data.size(); }
    
    // Check if buffer is empty
    bool IsEmpty() const { return data.empty(); }
    
    // Clear buffer
    void Clear() { data.clear(); }
};

// ====================
// File Operations
// ====================

class FileSystem {
public:
    // Read entire file into buffer
    // Returns empty buffer if file cannot be read
    static FileBuffer ReadFile(const Path& path);
    
    // Write buffer to file
    // Creates file if it doesn't exist, overwrites if it does
    // Returns true on success
    static bool WriteFile(const Path& path, const FileBuffer& buffer);
    
    // Write string to file
    static bool WriteFile(const Path& path, std::string_view content);
    
    // Check if file exists
    static bool Exists(const Path& path);
    
    // Delete file
    // Returns true on success
    static bool Delete(const Path& path);
    
    // Create directory (recursive)
    // Returns true on success
    static bool CreateDirectory(const Path& path);
    
    // Get absolute path
    static Path GetAbsolutePath(const Path& path);
    
    // Get file size in bytes
    // Returns 0 if file doesn't exist
    static size_t GetFileSize(const Path& path);
    
private:
    FileSystem() = delete;  // Static-only class
};

// ====================
// Path Utilities
// ====================

namespace path {
    // Get the directory containing the executable
    Path GetExecutableDirectory();
    
    // Get the asset directory relative to executable
    Path GetAssetDirectory();
    
    // Normalize path separators
    Path Normalize(const Path& path);
    
    // Join path components
    Path Join(const Path& base, const Path& relative);
}

}  // namespace gws::file_io
