# File I/O Abstraction System

## Overview

The File I/O system provides a complete abstraction layer for file operations in the GameWorldshaper engine. It features:

- **Virtual Filesystem Interface** — Pluggable I/O backends (disk, memory, network, pak files)
- **Asynchronous Loading** — Non-blocking file operations with futures and callbacks
- **Asset Management** — Type-safe asset handles and resource caching
- **Cross-platform Support** — Works seamlessly on Windows, Linux, and macOS
- **Comprehensive Utilities** — Path manipulation, directory operations, error handling

## Architecture

```
┌─────────────────────────────────────────────────┐
│           AssetManager                           │
│  (High-level resource caching and loading)      │
└────────────────┬────────────────────────────────┘
                 │
┌────────────────▼────────────────────────────────┐
│       AsyncFileLoader                           │
│  (Non-blocking I/O with futures & callbacks)   │
└────────────────┬────────────────────────────────┘
                 │
┌────────────────▼────────────────────────────────┐
│       IFileSystem (Virtual Interface)            │
│  • StandardFileSystem (disk-based)               │
│  • Custom implementations (memory, pak, etc.)    │
└─────────────────────────────────────────────────┘
```

## Core Components

### FileBuffer
Simple binary data container wrapping `std::vector<uint8_t>`.

```cpp
FileBuffer buffer("path/to/file.bin");
const uint8_t* data = buffer.GetData();
size_t size = buffer.GetSize();
std::string_view text = buffer.AsString();  // For text files
```

### FileSystem (Static API)
Synchronous file operations with global instance management.

```cpp
using namespace gws::file_io;

// Read/write files
FileBuffer buffer = FileSystem::ReadFile("file.txt");
FileOperationStatus status = FileSystem::WriteFile("file.txt", buffer);

// Check status
if (status.IsSuccess()) {
    // File written successfully
} else {
    std::cout << status.error_message << "\n";
}

// Directory operations
FileSystem::CreateDirectory("path/to/directory");
FileSystem::Delete("file.txt");
FileSystem::Exists("file.txt");
FileSystem::GetFileSize("file.txt");
FileSystem::IsDirectory("path");
FileSystem::IsFile("path");
```

### Path Utilities
Safe path manipulation with cross-platform support.

```cpp
using namespace gws::file_io::path;

Path exe_dir = GetExecutableDirectory();
Path assets = GetAssetDirectory();
Path normalized = Normalize("some/path/./other");
Path joined = Join(assets, "textures/wall.png");
std::string ext = GetExtension("file.png");      // "png"
Path stem = GetStemPath("file.png");             // "file"
Path parent = GetParent("dir/file.txt");         // "dir"
```

### IFileSystem (Virtual Interface)
Abstract interface for pluggable filesystem implementations.

```cpp
class IFileSystem {
    virtual FileBuffer ReadFile(const Path& path) = 0;
    virtual FileOperationStatus WriteFile(const Path& path, const FileBuffer& buffer) = 0;
    virtual bool Exists(const Path& path) = 0;
    virtual FileOperationStatus Delete(const Path& path) = 0;
    virtual FileOperationStatus CreateDirectory(const Path& path) = 0;
    virtual Path GetAbsolutePath(const Path& path) = 0;
    virtual size_t GetFileSize(const Path& path) = 0;
    virtual bool IsDirectory(const Path& path) = 0;
    virtual bool IsFile(const Path& path) = 0;
};
```

Set a custom filesystem:
```cpp
auto memory_fs = std::make_shared<MyCustomFileSystem>();
FileSystem::SetInstance(memory_fs);
```

### AsyncFileLoader
Non-blocking file operations using `std::future` and callbacks.

```cpp
using namespace gws::file_io;

AsyncFileLoader loader;

// Future-based API
std::future<FileBuffer> future = loader.ReadFileAsync("file.txt");
FileBuffer data = future.get();  // Blocks until ready

// Callback-based API
loader.ReadFileAsync("file.txt", 
    [](const FileBuffer& buffer, const FileOperationStatus& status) {
        if (status.IsSuccess()) {
            // Process buffer
        }
    });
```

### AssetManager
High-level asset loading with type-safe handles and caching.

```cpp
using namespace gws::file_io;

// Define your asset type
struct MyAsset {
    std::vector<uint8_t> data;
};

AssetManager manager;

// Define loader callback
auto asset_factory = [](const FileBuffer& buf, const Path& path) {
    auto asset = std::make_shared<MyAsset>();
    asset->data.assign(buf.GetData(), buf.GetData() + buf.GetSize());
    return asset;
};

// Load asynchronously
Handle<MyAsset> handle = manager.LoadAsset<MyAsset>("path/to/asset", asset_factory);

// Load synchronously (blocks)
auto asset = manager.LoadAssetSync<MyAsset>("path/to/asset", asset_factory);

// Retrieve loaded asset (returns nullptr if not yet loaded)
auto loaded = manager.GetAsset(handle);

// Unload
manager.UnloadAsset(handle);
manager.ClearAssets<MyAsset>();
manager.ClearAll();
```

## Error Handling

All operations return or report results through `FileOperationStatus`:

```cpp
struct FileOperationStatus {
    FileResult result;      // Success, FileNotFound, PermissionDenied, IOError, etc.
    std::string error_message;
    
    bool IsSuccess() const { return result == FileResult::Success; }
};
```

## Design Decisions

### No Exceptions
File operations fail gracefully by returning status objects instead of throwing exceptions.

### Virtual Interface for Extensibility
The `IFileSystem` interface allows:
- **Memory filesystem** — For testing and temporary data
- **Pak archive filesystem** — For packed game content
- **Network filesystem** — For streaming from servers
- **Encrypted filesystem** — For secure content delivery

### Type-Safe Asset Handles
The `Handle<T>` template ensures compile-time type safety for assets:
```cpp
Handle<Mesh> mesh_handle = manager.LoadAsset<Mesh>(...);
// Handle<Texture> texture = mesh_handle;  // Compiler error!
auto mesh = manager.GetAsset(mesh_handle);  // Returns Mesh*, not void*
```

### Async Foundation for Phase 3
- Phase 2 uses synchronous loading for simple scenes
- Phase 3 integrates async loading for mesh/texture streaming
- Phase 4 adds streaming managers for continuous content loading

## Integration Examples

### Loading Configuration Files
```cpp
auto cfg_loader = [](const FileBuffer& buf, const Path& path) {
    auto config = std::make_shared<ConfigFile>();
    config->Parse(buf.AsString());
    return config;
};

auto config = manager.LoadAssetSync<ConfigFile>("config/game.cfg", cfg_loader);
```

### Streaming Textures
```cpp
auto texture_loader = [](const FileBuffer& buf, const Path& path) {
    auto texture = std::make_shared<Texture>();
    texture->LoadFromPNG(buf);
    return texture;
};

// Queue multiple texture loads
std::vector<Handle<Texture>> handles;
for (const auto& texture_path : texture_list) {
    handles.push_back(manager.LoadAsset<Texture>(texture_path, texture_loader));
}

// Poll for completion or wait
for (auto handle : handles) {
    // Check periodically
    if (auto tex = manager.GetAsset(handle)) {
        // Texture is ready
    }
}
```

### Custom Filesystem for Testing
```cpp
class MockFileSystem : public IFileSystem {
    // Implement with predetermined test data
};

auto mock_fs = std::make_shared<MockFileSystem>();
FileSystem::SetInstance(mock_fs);

// All subsequent file operations use mock data
// Perfect for unit tests
```

## Phase Roadmap

### Phase 1 (Current) ✓
- Core I/O abstractions
- Virtual filesystem interface
- Basic async loading
- Asset manager foundation

### Phase 2
- Configuration system integration
- Scene loading from files
- Asset discovery and catalogs

### Phase 3
- Mesh/texture streaming
- Progressive resource loading
- Memory budgeting

### Phase 4+
- Network filesystem
- Pak archive support
- DLC/patching system
