#pragma once

// Container types with custom allocator support and asset handles
#include "containers.h"

namespace gws::containers {
    // ====================
    // Type-Safe Asset Handles
    // ====================
    // Use Handle<T> from file_io module for type-safe asset references:
    // 
    // #include "core/file_io.h"
    // using namespace gws::file_io;
    // 
    // Handle<Mesh> mesh_handle = asset_manager.LoadAsset<Mesh>("mesh.obj", loader);
    // auto mesh = asset_manager.GetAsset(mesh_handle);
    // 
    // Benefits:
    // - Compile-time type safety (cannot confuse Handle<Mesh> with Handle<Texture>)
    // - Integer ID underneath (fast comparison)
    // - Invalid handle value is always 0xFFFFFFFF
    //
    // Available containers:
    // - DynamicArray<T> — Growth-based dynamic array
    // - HashMap<Key, Value> — Type-safe hash table
    // - RingBuffer<T> — Fixed-size circular buffer
    // - ObjectPool<T> — Fixed-size object pool
    // - Vector<T>, Deque<T> — Standard STL aliases
    // - AllocVector<T, Alloc> — Custom allocator support
}

