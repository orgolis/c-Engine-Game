#include "file_io.h"
#include <fstream>
#include <thread>

namespace gws::file_io {

// ====================
// StandardFileSystem Implementation
// ====================

FileBuffer StandardFileSystem::ReadFile(const Path& path) {
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

FileOperationStatus StandardFileSystem::WriteFile(const Path& path, const FileBuffer& buffer) {
    try {
        // Create parent directory if it doesn't exist
        Path parent = path.parent_path();
        if (!parent.empty() && !std::filesystem::exists(parent)) {
            std::filesystem::create_directories(parent);
        }
        
        // Write file
        std::ofstream file(path, std::ios::binary);
        if (!file.is_open()) {
            return FileOperationStatus{
                FileResult::IOError,
                "Failed to open file for writing: " + path.string()
            };
        }
        
        if (buffer.GetSize() > 0) {
            file.write(reinterpret_cast<const char*>(buffer.GetData()), buffer.GetSize());
        }
        
        if (file.good()) {
            return FileOperationStatus{FileResult::Success, ""};
        } else {
            return FileOperationStatus{
                FileResult::IOError,
                "Failed to write file: " + path.string()
            };
        }
    } catch (const std::filesystem::filesystem_error& e) {
        return FileOperationStatus{
            FileResult::IOError,
            "Filesystem error: " + std::string(e.what())
        };
    }
}

FileOperationStatus StandardFileSystem::WriteFile(const Path& path, std::string_view content) {
    try {
        // Create parent directory if it doesn't exist
        Path parent = path.parent_path();
        if (!parent.empty() && !std::filesystem::exists(parent)) {
            std::filesystem::create_directories(parent);
        }
        
        // Write file
        std::ofstream file(path, std::ios::binary);
        if (!file.is_open()) {
            return FileOperationStatus{
                FileResult::IOError,
                "Failed to open file for writing: " + path.string()
            };
        }
        
        file.write(content.data(), content.size());
        
        if (file.good()) {
            return FileOperationStatus{FileResult::Success, ""};
        } else {
            return FileOperationStatus{
                FileResult::IOError,
                "Failed to write file: " + path.string()
            };
        }
    } catch (const std::filesystem::filesystem_error& e) {
        return FileOperationStatus{
            FileResult::IOError,
            "Filesystem error: " + std::string(e.what())
        };
    }
}

bool StandardFileSystem::Exists(const Path& path) {
    try {
        return std::filesystem::exists(path);
    } catch (const std::filesystem::filesystem_error&) {
        return false;
    }
}

FileOperationStatus StandardFileSystem::Delete(const Path& path) {
    try {
        if (std::filesystem::remove(path) != 0) {
            return FileOperationStatus{FileResult::Success, ""};
        } else {
            return FileOperationStatus{
                FileResult::FileNotFound,
                "File does not exist: " + path.string()
            };
        }
    } catch (const std::filesystem::filesystem_error& e) {
        return FileOperationStatus{
            FileResult::IOError,
            "Filesystem error: " + std::string(e.what())
        };
    }
}

FileOperationStatus StandardFileSystem::CreateDirectory(const Path& path) {
    try {
        if (std::filesystem::create_directories(path)) {
            return FileOperationStatus{FileResult::Success, ""};
        } else {
            return FileOperationStatus{FileResult::Success, "Directory already exists"};
        }
    } catch (const std::filesystem::filesystem_error& e) {
        return FileOperationStatus{
            FileResult::DirectoryError,
            "Failed to create directory: " + std::string(e.what())
        };
    }
}

Path StandardFileSystem::GetAbsolutePath(const Path& path) {
    try {
        return std::filesystem::absolute(path);
    } catch (const std::filesystem::filesystem_error&) {
        return path;
    }
}

size_t StandardFileSystem::GetFileSize(const Path& path) {
    try {
        if (!std::filesystem::exists(path)) {
            return 0;
        }
        return std::filesystem::file_size(path);
    } catch (const std::filesystem::filesystem_error&) {
        return 0;
    }
}

bool StandardFileSystem::IsDirectory(const Path& path) {
    try {
        return std::filesystem::is_directory(path);
    } catch (const std::filesystem::filesystem_error&) {
        return false;
    }
}

bool StandardFileSystem::IsFile(const Path& path) {
    try {
        return std::filesystem::is_regular_file(path);
    } catch (const std::filesystem::filesystem_error&) {
        return false;
    }
}

// ====================
// AsyncFileLoader Implementation
// ====================

AsyncFileLoader::AsyncFileLoader(std::shared_ptr<IFileSystem> filesystem)
    : m_filesystem(filesystem) {
    if (!m_filesystem) {
        m_filesystem = std::make_shared<StandardFileSystem>();
    }
}

std::future<FileBuffer> AsyncFileLoader::ReadFileAsync(const Path& path) {
    return std::async(std::launch::async, [this, path]() {
        return m_filesystem->ReadFile(path);
    });
}

std::future<FileOperationStatus> AsyncFileLoader::WriteFileAsync(const Path& path, const FileBuffer& buffer) {
    return std::async(std::launch::async, [this, path, buffer_copy = buffer]() {
        return m_filesystem->WriteFile(path, buffer_copy);
    });
}

std::future<FileOperationStatus> AsyncFileLoader::WriteFileAsync(const Path& path, std::string_view content) {
    std::string content_str(content);  // Copy string to avoid dangling reference
    return std::async(std::launch::async, [this, path, content_str]() {
        return m_filesystem->WriteFile(path, content_str);
    });
}

void AsyncFileLoader::ReadFileAsync(const Path& path, FileLoadCallback callback) {
    std::thread([this, path, callback]() {
        FileBuffer buffer = m_filesystem->ReadFile(path);
        FileOperationStatus status{
            buffer.IsEmpty() ? FileResult::FileNotFound : FileResult::Success,
            buffer.IsEmpty() ? "File not found or empty: " + path.string() : ""
        };
        callback(buffer, status);
    }).detach();
}

void AsyncFileLoader::WriteFileAsync(const Path& path, const FileBuffer& buffer, FileOpCallback callback) {
    std::thread([this, path, buffer_copy = buffer, callback]() {
        FileOperationStatus status = m_filesystem->WriteFile(path, buffer_copy);
        callback(status);
    }).detach();
}

void AsyncFileLoader::WriteFileAsync(const Path& path, std::string_view content, FileOpCallback callback) {
    std::string content_str(content);
    std::thread([this, path, content_str, callback]() {
        FileOperationStatus status = m_filesystem->WriteFile(path, content_str);
        callback(status);
    }).detach();
}

void AsyncFileLoader::SetFileSystem(std::shared_ptr<IFileSystem> filesystem) {
    if (filesystem) {
        m_filesystem = filesystem;
    }
}

std::shared_ptr<IFileSystem> AsyncFileLoader::GetFileSystem() const {
    return m_filesystem;
}

// ====================
// Global FileSystem Implementation
// ====================

std::shared_ptr<IFileSystem> FileSystem::s_instance = nullptr;

FileBuffer FileSystem::ReadFile(const Path& path) {
    if (!s_instance) {
        s_instance = std::make_shared<StandardFileSystem>();
    }
    return s_instance->ReadFile(path);
}

FileOperationStatus FileSystem::WriteFile(const Path& path, const FileBuffer& buffer) {
    if (!s_instance) {
        s_instance = std::make_shared<StandardFileSystem>();
    }
    return s_instance->WriteFile(path, buffer);
}

FileOperationStatus FileSystem::WriteFile(const Path& path, std::string_view content) {
    if (!s_instance) {
        s_instance = std::make_shared<StandardFileSystem>();
    }
    return s_instance->WriteFile(path, content);
}

bool FileSystem::Exists(const Path& path) {
    if (!s_instance) {
        s_instance = std::make_shared<StandardFileSystem>();
    }
    return s_instance->Exists(path);
}

FileOperationStatus FileSystem::Delete(const Path& path) {
    if (!s_instance) {
        s_instance = std::make_shared<StandardFileSystem>();
    }
    return s_instance->Delete(path);
}

FileOperationStatus FileSystem::CreateDirectory(const Path& path) {
    if (!s_instance) {
        s_instance = std::make_shared<StandardFileSystem>();
    }
    return s_instance->CreateDirectory(path);
}

Path FileSystem::GetAbsolutePath(const Path& path) {
    if (!s_instance) {
        s_instance = std::make_shared<StandardFileSystem>();
    }
    return s_instance->GetAbsolutePath(path);
}

size_t FileSystem::GetFileSize(const Path& path) {
    if (!s_instance) {
        s_instance = std::make_shared<StandardFileSystem>();
    }
    return s_instance->GetFileSize(path);
}

bool FileSystem::IsDirectory(const Path& path) {
    if (!s_instance) {
        s_instance = std::make_shared<StandardFileSystem>();
    }
    return s_instance->IsDirectory(path);
}

bool FileSystem::IsFile(const Path& path) {
    if (!s_instance) {
        s_instance = std::make_shared<StandardFileSystem>();
    }
    return s_instance->IsFile(path);
}

void FileSystem::SetInstance(std::shared_ptr<IFileSystem> filesystem) {
    s_instance = filesystem;
}

std::shared_ptr<IFileSystem> FileSystem::GetInstance() {
    if (!s_instance) {
        s_instance = std::make_shared<StandardFileSystem>();
    }
    return s_instance;
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

std::string GetExtension(const Path& path) {
    auto ext = path.extension().string();
    // Remove leading dot if present
    if (!ext.empty() && ext[0] == '.') {
        return ext.substr(1);
    }
    return ext;
}

Path GetStemPath(const Path& path) {
    return path.stem();
}

Path GetParent(const Path& path) {
    return path.parent_path();
}

}  // namespace path

}  // namespace gws::file_io
