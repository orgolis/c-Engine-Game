#include <catch2/catch_test_macros.hpp>
#include "core/memory/memory.h"

using namespace gws::memory;

TEST_CASE("GeneralAllocator basic", "[memory][general_allocator]") {
    GeneralAllocator allocator(65536);
    
    void* ptr = allocator.allocate(1024);
    REQUIRE(ptr != nullptr);
    
    allocator.deallocate(ptr);
}

TEST_CASE("GeneralAllocator multiple", "[memory][general_allocator]") {
    GeneralAllocator allocator(65536);
    
    void* ptr1 = allocator.allocate(512);
    void* ptr2 = allocator.allocate(512);
    void* ptr3 = allocator.allocate(512);
    
    REQUIRE(ptr1 != nullptr);
    REQUIRE(ptr2 != nullptr);
    REQUIRE(ptr3 != nullptr);
    
    allocator.deallocate(ptr1);
    allocator.deallocate(ptr2);
    allocator.deallocate(ptr3);
}

TEST_CASE("GeneralAllocator stats", "[memory][general_allocator]") {
    GeneralAllocator allocator(65536);
    
    void* ptr = allocator.allocate(1024);
    AllocationStats stats = allocator.get_stats();
    
    REQUIRE(stats.total_allocated >= 1024);
    
    allocator.deallocate(ptr);
}
