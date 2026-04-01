#include "file_io.h"
#include <fstream>
#include <stdexcept>

namespace gws::file_io {

FileBuffer FileSystem::ReadFile(const Path& path) {
    try {
        // Check if file exists
        if (!std::filesystem::exists(path)) {
            return FileBuffer();
        }
        
        // Get file size
        size_t file_size = std::filesystem::file_size(path);
        
        // Read file
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) {
            return FileBuffer();
        }
        
        FileBuffer buffer(file_size);
        file.read(reinterpret_cast<char*>(buffer.GetData()), file_size);
        
        if (!file) {
            return FileBuffer();  // Read failed
        }
        
        return buffer;
    } catch (const std::filesystem::filesystem_error&) {
        return FileBuffer();
    }
}

bool FileSystem::WriteFile(const Path& path, const FileBuffer& buffer) {
    try {
        // Create parent directory if it doesn't exist
        Path parent = path.parent_path();
        if (!parent.empty() && !std::filesystem::exists(parent)) {
            std::filesystem::create_directories(parent);
        }
        
        // Write file
        std::ofstream file(path, std::ios::binary);
        if (!file.is_open()) {
            return false;
        }
        
        if (buffer.GetSize() > 0) {
            file.write(reinterpret_cast<const char*>(buffer.GetData()), buffer.GetSize());
        }
        
        return file.good();
    } catch (const std::filesystem::filesystem_error&) {
        return false;
    }
}

bool FileSystem::WriteFile(const Path& path, std::string_view content) {
    try {
        // Create parent directory if it doesn't exist
        Path parent = path.parent_path();
        if (!parent.empty() && !std::filesystem::exists(parent)) {
            std::filesystem::create_directories(parent);
        }
        
        // Write file
        std::ofstream file(path, std::ios::binary);
        if (!file.is_open()) {
            return false;
        }
        
        file.write(content.data(), content.size());
        return file.good();
    } catch (const std::filesystem::filesystem_error&) {
        return false;
    }
}

bool FileSystem::Exists(const Path& path) {
    try {
        return std::filesystem::exists(path);
    } catch (const std::filesystem::filesystem_error&) {
        return false;
    }
}

bool FileSystem::Delete(const Path& path) {
    try {
        return std::filesystem::remove(path) != 0;
    } catch (const std::filesystem::filesystem_error&) {
        return false;
    }
}

bool FileSystem::CreateDirectory(const Path& path) {
    try {
        return std::filesystem::create_directories(path);
    } catch (const std::filesystem::filesystem_error&) {
        return false;
    }
}

Path FileSystem::GetAbsolutePath(const Path& path) {
    try {
        return std::filesystem::absolute(path);
    } catch (const std::filesystem::filesystem_error&) {
        return path;
    }
}

size_t FileSystem::GetFileSize(const Path& path) {
    try {
        if (!std::filesystem::exists(path)) {
            return 0;
        }
        return std::filesystem::file_size(path);
    } catch (const std::filesystem::filesystem_error&) {
        return 0;
    }
}

// ====================
// Path Utilities
// ====================

namespace path {

Path GetExecutableDirectory() {
    try {
        return std::filesystem::current_path();
    } catch (const std::filesystem::filesystem_error&) {
        return Path();
    }
}

Path GetAssetDirectory() {
    return GetExecutableDirectory() / "assets";
}

Path Normalize(const Path& path) {
    // std::filesystem::path handles normalization automatically
    return path.lexically_normal();
}

Path Join(const Path& base, const Path& relative) {
    return base / relative;
}

}  // namespace path

}  // namespace gws::file_io
