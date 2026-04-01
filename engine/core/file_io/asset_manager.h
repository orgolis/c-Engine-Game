#pragma once

#include "file_io.h"
#include <unordered_map>
#include <typeindex>
#include <type_traits>
#include <memory>

namespace gws::file_io {

// ====================
// Asset Handle
// Type-safe reference to a loaded asset
// ====================

template<typename AssetType>
class Handle {
public:
    using Type = AssetType;
    
    Handle() : m_id(INVALID_ID) {}
    explicit Handle(uint32_t id) : m_id(id) {}
    
    uint32_t GetId() const { return m_id; }
    bool IsValid() const { return m_id != INVALID_ID; }
    
    bool operator==(const Handle& other) const { return m_id == other.m_id; }
    bool operator!=(const Handle& other) const { return m_id != other.m_id; }
    
private:
    static constexpr uint32_t INVALID_ID = 0xFFFFFFFF;
    uint32_t m_id;
};

// ====================
// Asset Manager
// Manages asset loading, caching, and lifetime
// ====================

class AssetManager {
public:
    AssetManager();
    ~AssetManager();
    
    // Initialize with custom filesystem
    explicit AssetManager(std::shared_ptr<IFileSystem> filesystem);
    
    // Subscribe to asset load completion
    // The callback receives the loaded buffer and file path
    template<typename AssetType>
    using LoadCallback = std::function<std::shared_ptr<AssetType>(const FileBuffer&, const Path&)>;
    
    // Load asset (calls registered callback to create asset from buffer)
    // Returns immediately with a handle; actual loading happens asynchronously
    template<typename AssetType>
    Handle<AssetType> LoadAsset(const Path& path, LoadCallback<AssetType> callback) {
        Handle<AssetType> handle(m_next_asset_id++);
        
        // Kick off async load
        m_async_loader->ReadFileAsync(path, [this, handle, path, callback](const FileBuffer& buffer, const FileOperationStatus& status) {
            if (status.IsSuccess()) {
                auto asset = callback(buffer, path);
                if (asset) {
                    std::lock_guard<std::mutex> lock(m_asset_mutex);
                    m_assets[std::type_index(typeid(AssetType))][handle.GetId()] = asset;
                }
            }
        });
        
        return handle;
    }
    
    // Get loaded asset (returns nullptr if not yet loaded)
    template<typename AssetType>
    std::shared_ptr<AssetType> GetAsset(Handle<AssetType> handle) {
        if (!handle.IsValid()) {
            return nullptr;
        }
        
        std::lock_guard<std::mutex> lock(m_asset_mutex);
        auto type_it = m_assets.find(std::type_index(typeid(AssetType)));
        if (type_it == m_assets.end()) {
            return nullptr;
        }
        
        auto id_it = type_it->second.find(handle.GetId());
        if (id_it == type_it->second.end()) {
            return nullptr;
        }
        
        return std::static_pointer_cast<AssetType>(id_it->second);
    }
    
    // Synchronously load asset (blocks until loaded)
    template<typename AssetType>
    std::shared_ptr<AssetType> LoadAssetSync(const Path& path, LoadCallback<AssetType> callback) {
        FileBuffer buffer = m_async_loader->GetFileSystem()->ReadFile(path);
        if (buffer.IsEmpty()) {
            return nullptr;
        }
        return callback(buffer, path);
    }
    
    // Unload asset
    template<typename AssetType>
    void UnloadAsset(Handle<AssetType> handle) {
        if (!handle.IsValid()) {
            return;
        }
        
        std::lock_guard<std::mutex> lock(m_asset_mutex);
        auto type_it = m_assets.find(std::type_index(typeid(AssetType)));
        if (type_it != m_assets.end()) {
            type_it->second.erase(handle.GetId());
        }
    }
    
    // Clear all assets of a type
    template<typename AssetType>
    void ClearAssets() {
        std::lock_guard<std::mutex> lock(m_asset_mutex);
        m_assets.erase(std::type_index(typeid(AssetType)));
    }
    
    // Clear all assets
    void ClearAll() {
        std::lock_guard<std::mutex> lock(m_asset_mutex);
        m_assets.clear();
    }
    
    // Set the filesystem backend
    void SetFileSystem(std::shared_ptr<IFileSystem> filesystem) {
        if (m_async_loader) {
            m_async_loader->SetFileSystem(filesystem);
        }
    }
    
    // Get the filesystem backend
    std::shared_ptr<IFileSystem> GetFileSystem() const {
        if (m_async_loader) {
            return m_async_loader->GetFileSystem();
        }
        return nullptr;
    }
    
private:
    std::unique_ptr<AsyncFileLoader> m_async_loader;
    std::unordered_map<std::type_index, std::unordered_map<uint32_t, std::shared_ptr<void>>> m_assets;
    std::mutex m_asset_mutex;
    uint32_t m_next_asset_id = 1;  // Start at 1 to avoid invalid handle (0xFFFFFFFF)
};

}  // namespace gws::file_io
