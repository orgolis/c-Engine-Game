#include <catch2/catch_test_macros.hpp>
#include "core/memory/memory.h"

using namespace gws::memory;

TEST_CASE("StackAllocator basic", "[memory][stack_allocator]") {
    StackAllocator allocator(4096);
    
    void* ptr = allocator.allocate(512);
    REQUIRE(ptr != nullptr);
}

TEST_CASE("StackAllocator sequential", "[memory][stack_allocator]") {
    StackAllocator allocator(4096);
    
    void* ptr1 = allocator.allocate(512);
    void* ptr2 = allocator.allocate(512);
    void* ptr3 = allocator.allocate(512);
    
    REQUIRE(ptr1 != nullptr);
    REQUIRE(ptr2 != nullptr);
    REQUIRE(ptr3 != nullptr);
}

TEST_CASE("StackAllocator reset", "[memory][stack_allocator]") {
    StackAllocator allocator(4096);
    
    allocator.allocate(512);
    allocator.allocate(512);
    allocator.allocate(512);
    
    AllocationStats stats_before = allocator.get_stats();
    REQUIRE(stats_before.total_allocated > 0);
    
    allocator.reset();
    
    AllocationStats stats_after = allocator.get_stats();
    REQUIRE(stats_after.total_allocated == 0);
}
