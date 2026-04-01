#pragma once

#include <string>
#include <vector>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <future>

namespace gws::file_io {

// ====================
// File I/O System
// Cross-platform abstraction for reading/writing files
// Supports both synchronous and asynchronous operations
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
// File Operation Results
// ====================

enum class FileResult {
    Success,              // Operation completed successfully
    FileNotFound,         // File does not exist
    PermissionDenied,     // Access denied
    IOError,              // Generic I/O error
    InvalidPath,          // Path is invalid
    DirectoryError,       // Directory operation failed
    Unknown               // Unknown error
};

struct FileOperationStatus {
    FileResult result = FileResult::Unknown;
    std::string error_message;
    
    bool IsSuccess() const { return result == FileResult::Success; }
};

// ====================
// Virtual Filesystem Interface
// Allows pluggable filesystem implementations
// ====================

class IFileSystem {
public:
    virtual ~IFileSystem() = default;
    
    // Synchronous operations
    virtual FileBuffer ReadFile(const Path& path) = 0;
    virtual FileOperationStatus WriteFile(const Path& path, const FileBuffer& buffer) = 0;
    virtual FileOperationStatus WriteFile(const Path& path, std::string_view content) = 0;
    virtual bool Exists(const Path& path) = 0;
    virtual FileOperationStatus Delete(const Path& path) = 0;
    virtual FileOperationStatus CreateDirectory(const Path& path) = 0;
    virtual Path GetAbsolutePath(const Path& path) = 0;
    virtual size_t GetFileSize(const Path& path) = 0;
    
    // Additional utilities
    virtual bool IsDirectory(const Path& path) = 0;
    virtual bool IsFile(const Path& path) = 0;
};

// ====================
// Standard Filesystem Implementation
// Default implementation using std::filesystem
// ====================

class StandardFileSystem : public IFileSystem {
public:
    FileBuffer ReadFile(const Path& path) override;
    FileOperationStatus WriteFile(const Path& path, const FileBuffer& buffer) override;
    FileOperationStatus WriteFile(const Path& path, std::string_view content) override;
    bool Exists(const Path& path) override;
    FileOperationStatus Delete(const Path& path) override;
    FileOperationStatus CreateDirectory(const Path& path) override;
    Path GetAbsolutePath(const Path& path) override;
    size_t GetFileSize(const Path& path) override;
    bool IsDirectory(const Path& path) override;
    bool IsFile(const Path& path) override;
};

// ====================
// Async File Operations
// High-level async API for non-blocking file I/O
// ====================

class AsyncFileLoader {
public:
    explicit AsyncFileLoader(std::shared_ptr<IFileSystem> filesystem);
    
    // Load file asynchronously using std::future
    std::future<FileBuffer> ReadFileAsync(const Path& path);
    std::future<FileOperationStatus> WriteFileAsync(const Path& path, const FileBuffer& buffer);
    std::future<FileOperationStatus> WriteFileAsync(const Path& path, std::string_view content);
    
    // Callback-based API for more control
    using FileLoadCallback = std::function<void(const FileBuffer&, const FileOperationStatus&)>;
    using FileOpCallback = std::function<void(const FileOperationStatus&)>;
    
    void ReadFileAsync(const Path& path, FileLoadCallback callback);
    void WriteFileAsync(const Path& path, const FileBuffer& buffer, FileOpCallback callback);
    void WriteFileAsync(const Path& path, std::string_view content, FileOpCallback callback);
    
    // Set the filesystem backend
    void SetFileSystem(std::shared_ptr<IFileSystem> filesystem);
    std::shared_ptr<IFileSystem> GetFileSystem() const;
    
private:
    std::shared_ptr<IFileSystem> m_filesystem;
};

// ====================
// Global File Operations (using default filesystem)
// ====================

class FileSystem {
public:
    // Read entire file into buffer
    // Returns empty buffer if file cannot be read
    static FileBuffer ReadFile(const Path& path);
    
    // Write buffer to file
    // Creates file if it doesn't exist, overwrites if it does
    // Returns operation status
    static FileOperationStatus WriteFile(const Path& path, const FileBuffer& buffer);
    
    // Write string to file
    static FileOperationStatus WriteFile(const Path& path, std::string_view content);
    
    // Check if file exists
    static bool Exists(const Path& path);
    
    // Delete file
    static FileOperationStatus Delete(const Path& path);
    
    // Create directory (recursive)
    static FileOperationStatus CreateDirectory(const Path& path);
    
    // Get absolute path
    static Path GetAbsolutePath(const Path& path);
    
    // Get file size in bytes
    // Returns 0 if file doesn't exist
    static size_t GetFileSize(const Path& path);
    
    // Check if path is a directory
    static bool IsDirectory(const Path& path);
    
    // Check if path is a file
    static bool IsFile(const Path& path);
    
    // Set the global filesystem implementation
    static void SetInstance(std::shared_ptr<IFileSystem> filesystem);
    
    // Get the global filesystem implementation
    static std::shared_ptr<IFileSystem> GetInstance();
    
private:
    FileSystem() = delete;  // Static-only class
    static std::shared_ptr<IFileSystem> s_instance;
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
    
    // Get file extension
    std::string GetExtension(const Path& path);
    
    // Get filename without extension
    Path GetStemPath(const Path& path);
    
    // Get parent directory
    Path GetParent(const Path& path);
}

}  // namespace gws::file_io
