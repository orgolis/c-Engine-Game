#include "asset_manager.h"

namespace gws::file_io {

AssetManager::AssetManager() 
    : m_async_loader(std::make_unique<AsyncFileLoader>(std::make_shared<StandardFileSystem>())) {
}

AssetManager::AssetManager(std::shared_ptr<IFileSystem> filesystem)
    : m_async_loader(std::make_unique<AsyncFileLoader>(filesystem)) {
}

AssetManager::~AssetManager() = default;

}  // namespace gws::file_io
