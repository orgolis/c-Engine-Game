// ====================
// File I/O System Examples
// ====================

#pragma once

#include "file_io.h"
#include "asset_manager.h"
#include <iostream>

namespace gws::file_io::examples {

// ====================
// Example 1: Basic File Operations
// ====================
void BasicFileOperations() {
    using namespace gws::file_io;
    
    // Write a text file
    std::string content = "Hello, World!";
    FileOperationStatus write_status = FileSystem::WriteFile("output.txt", content);
    if (write_status.IsSuccess()) {
        std::cout << "File written successfully\n";
    }
    
    // Read the file back
    FileBuffer buffer = FileSystem::ReadFile("output.txt");
    if (!buffer.IsEmpty()) {
        std::cout << "File content: " << buffer.AsString() << "\n";
    }
    
    // Check if file exists
    if (FileSystem::Exists("output.txt")) {
        std::cout << "File exists\n";
    }
    
    // Get file size
    size_t size = FileSystem::GetFileSize("output.txt");
    std::cout << "File size: " << size << " bytes\n";
    
    // Delete the file
    FileOperationStatus delete_status = FileSystem::Delete("output.txt");
    if (delete_status.IsSuccess()) {
        std::cout << "File deleted\n";
    }
}

// ====================
// Example 2: Binary File Operations
// ====================
void BinaryFileOperations() {
    using namespace gws::file_io;
    
    // Create a binary buffer
    FileBuffer buffer(256);
    uint8_t* data = buffer.GetData();
    
    // Fill with some data
    for (size_t i = 0; i < 256; ++i) {
        data[i] = static_cast<uint8_t>(i % 256);
    }
    
    // Write binary data
    FileSystem::WriteFile("binary.dat", buffer);
    
    // Read binary data back
    FileBuffer read_buffer = FileSystem::ReadFile("binary.dat");
    std::cout << "Binary file size: " << read_buffer.GetSize() << " bytes\n";
}

// ====================
// Example 3: Path Utilities
// ====================
void PathUtilities() {
    using namespace gws::file_io;
    
    // Get common directories
    Path exe_dir = path::GetExecutableDirectory();
    Path asset_dir = path::GetAssetDirectory();
    
    // Join paths
    Path full_path = path::Join(asset_dir, "textures/wall.png");
    
    // Normalize path
    Path normalized = path::Normalize(full_path);
    
    // Get extension
    std::string ext = path::GetExtension(full_path);  // "png"
    
    // Get filename without extension
    Path stem = path::GetStemPath(full_path);  // "wall"
    
    // Get parent directory
    Path parent = path::GetParent(full_path);  // "assets/textures"
    
    std::cout << "Executable: " << exe_dir << "\n";
    std::cout << "Assets: " << asset_dir << "\n";
    std::cout << "Full path: " << full_path << "\n";
    std::cout << "Extension: " << ext << "\n";
}

// ====================
// Example 4: Directory Operations
// ====================
void DirectoryOperations() {
    using namespace gws::file_io;
    
    Path dir = "output/data/nested";
    
    // Create nested directory structure
    FileOperationStatus status = FileSystem::CreateDirectory(dir);
    if (status.IsSuccess()) {
        std::cout << "Directory created: " << dir << "\n";
    }
    
    // Check if path is directory
    if (FileSystem::IsDirectory(dir)) {
        std::cout << dir << " is a directory\n";
    }
    
    // Write a file in the new directory
    FileSystem::WriteFile(dir / "test.txt", "test content");
    
    // Check if it's a file
    if (FileSystem::IsFile(dir / "test.txt")) {
        std::cout << "File created in directory\n";
    }
}

// ====================
// Example 5: Asynchronous File Loading (std::future)
// ====================
void AsyncFileOperationsFuture() {
    using namespace gws::file_io;
    
    AsyncFileLoader loader;
    
    // Write some content first
    FileSystem::WriteFile("async_test.txt", "Async content");
    
    // Load file asynchronously using std::future
    std::future<FileBuffer> future = loader.ReadFileAsync("async_test.txt");
    
    // Do other work while file is loading...
    std::cout << "Loading file asynchronously...\n";
    
    // Wait for result with timeout (optional)
    if (future.valid()) {
        FileBuffer buffer = future.get();
        if (!buffer.IsEmpty()) {
            std::cout << "Async loaded: " << buffer.AsString() << "\n";
        }
    }
}

// ====================
// Example 6: Asynchronous File Loading (Callbacks)
// ====================
void AsyncFileOperationsCallbacks() {
    using namespace gws::file_io;
    
    AsyncFileLoader loader;
    
    // Load file with callback
    loader.ReadFileAsync("async_test.txt", 
        [](const FileBuffer& buffer, const FileOperationStatus& status) {
            if (status.IsSuccess() && !buffer.IsEmpty()) {
                std::cout << "Callback received: " << buffer.AsString() << "\n";
            }
        });
    
    // Continue with other work; callback will be called when ready
    std::cout << "Async load request queued\n";
}

// ====================
// Example 7: Custom Filesystem Implementation
// ====================
class MemoryFileSystem : public IFileSystem {
public:
    FileBuffer ReadFile(const Path& path) override {
        auto it = m_files.find(path.string());
        if (it != m_files.end()) {
            return it->second;
        }
        return FileBuffer();
    }
    
    FileOperationStatus WriteFile(const Path& path, const FileBuffer& buffer) override {
        m_files[path.string()] = buffer;
        return FileOperationStatus{FileResult::Success, ""};
    }
    
    FileOperationStatus WriteFile(const Path& path, std::string_view content) override {
        FileBuffer buffer(content.size());
        std::copy(content.begin(), content.end(), buffer.GetData());
        m_files[path.string()] = buffer;
        return FileOperationStatus{FileResult::Success, ""};
    }
    
    bool Exists(const Path& path) override {
        return m_files.find(path.string()) != m_files.end();
    }
    
    FileOperationStatus Delete(const Path& path) override {
        if (m_files.erase(path.string()) > 0) {
            return FileOperationStatus{FileResult::Success, ""};
        }
        return FileOperationStatus{FileResult::FileNotFound, ""};
    }
    
    FileOperationStatus CreateDirectory(const Path& path) override {
        m_directories.insert(path.string());
        return FileOperationStatus{FileResult::Success, ""};
    }
    
    Path GetAbsolutePath(const Path& path) override { return path; }
    size_t GetFileSize(const Path& path) override {
        auto it = m_files.find(path.string());
        return (it != m_files.end()) ? it->second.GetSize() : 0;
    }
    bool IsDirectory(const Path& path) override {
        return m_directories.find(path.string()) != m_directories.end();
    }
    bool IsFile(const Path& path) override { return Exists(path); }

private:
    std::unordered_map<std::string, FileBuffer> m_files;
    std::unordered_set<std::string> m_directories;
};

void CustomFilesystemExample() {
    using namespace gws::file_io;
    
    // Create and set custom filesystem
    auto memory_fs = std::make_shared<MemoryFileSystem>();
    FileSystem::SetInstance(memory_fs);
    
    // Now all file operations use the memory filesystem
    FileSystem::WriteFile("memory://test.txt", "In-memory data");
    FileBuffer buffer = FileSystem::ReadFile("memory://test.txt");
    std::cout << "From memory FS: " << buffer.AsString() << "\n";
}

// ====================
// Example 8: Asset Manager
// ====================

// Define a simple asset type
struct Texture {
    std::vector<uint8_t> pixels;
    int width, height;
};

void AssetManagerExample() {
    using namespace gws::file_io;
    
    AssetManager manager;
    
    // Define how to create a Texture from FileBuffer
    auto texture_loader = [](const FileBuffer& buffer, const Path& path) -> std::shared_ptr<Texture> {
        auto texture = std::make_shared<Texture>();
        texture->pixels.assign(buffer.GetData(), buffer.GetData() + buffer.GetSize());
        texture->width = 256;
        texture->height = 256;
        std::cout << "Loaded texture: " << path.filename() << "\n";
        return texture;
    };
    
    // Asynchronously load a texture
    Handle<Texture> handle = manager.LoadAsset<Texture>("textures/wall.png", texture_loader);
    
    // Try to get the asset (will be nullptr if not yet loaded)
    auto texture = manager.GetAsset(handle);
    
    // Wait a bit for loading to complete (in real code, use manager.WaitForAsset or callbacks)
    // ...
    
    // Get the loaded asset
    texture = manager.GetAsset(handle);
    if (texture) {
        std::cout << "Texture loaded: " << texture->width << "x" << texture->height << "\n";
    }
}

// ====================
// Example 9: Asset Manager with Synchronous Loading
// ====================
void AssetManagerSyncExample() {
    using namespace gws::file_io;
    
    AssetManager manager;
    
    auto texture_loader = [](const FileBuffer& buffer, const Path& path) -> std::shared_ptr<Texture> {
        auto texture = std::make_shared<Texture>();
        texture->pixels.assign(buffer.GetData(), buffer.GetData() + buffer.GetSize());
        texture->width = 256;
        texture->height = 256;
        return texture;
    };
    
    // Synchronously load (blocks until done)
    auto texture = manager.LoadAssetSync<Texture>("textures/wall.png", texture_loader);
    
    if (texture) {
        std::cout << "Texture loaded synchronously\n";
    }
}

}  // namespace gws::file_io::examples
