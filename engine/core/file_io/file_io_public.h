#pragma once

// File I/O system
#include "file_io.h"
#include "asset_manager.h"

namespace gws::file_io {
    // All file I/O functionality is available under gws::file_io namespace
    // 
    // Core Components:
    // - FileBuffer: Simple binary data container
    // - FileSystem: Synchronous file operations
    // - AsyncFileLoader: Non-blocking file loading
    // - IFileSystem: Virtual filesystem interface for custom implementations
    // - AssetManager: High-level asset loading and caching system
}
