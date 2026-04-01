#include <catch2/catch_test_macros.hpp>
#include "core/memory/memory.h"

using namespace gws::memory;

TEST_CASE("PoolAllocator basic", "[memory][pool_allocator]") {
    PoolAllocator allocator(256, 16);
    
    void* ptr = allocator.allocate(256);
    REQUIRE(ptr != nullptr);
    
    allocator.deallocate(ptr);
}

TEST_CASE("PoolAllocator multiple", "[memory][pool_allocator]") {
    PoolAllocator allocator(256, 10);
    
    void* ptrs[10];
    for (int i = 0; i < 10; i++) {
        ptrs[i] = allocator.allocate(256);
        REQUIRE(ptrs[i] != nullptr);
    }
    
    for (int i = 0; i < 10; i++) {
        allocator.deallocate(ptrs[i]);
    }
}

TEST_CASE("PoolAllocator different sizes", "[memory][pool_allocator]") {
    PoolAllocator allocator128(128, 10);
    PoolAllocator allocator256(256, 10);
    PoolAllocator allocator1024(1024, 10);
    
    void* ptr128 = allocator128.allocate(128);
    void* ptr256 = allocator256.allocate(256);
    void* ptr1024 = allocator1024.allocate(1024);
    
    REQUIRE(ptr128 != nullptr);
    REQUIRE(ptr256 != nullptr);
    REQUIRE(ptr1024 != nullptr);
    
    allocator128.deallocate(ptr128);
    allocator256.deallocate(ptr256);
    allocator1024.deallocate(ptr1024);
}
