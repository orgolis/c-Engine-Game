#pragma once

#include "allocator_base.h"
#include <cstring>

namespace gws::memory {

// ====================
// General Allocator
// Variable-size allocator with free-list and block coalescing
// Handles arbitrary allocation patterns. O(n) search for free blocks (acceptable for game systems).
// ====================

class GeneralAllocator : public Allocator {
private:
    struct BlockHeader {
        size_t size;      // Size of the allocation (not including header)
        bool is_free;     // Whether block is available
        BlockHeader* prev;
        BlockHeader* next;
    };
    
    uint8_t* buffer;
    size_t capacity;
    BlockHeader* block_list;
    size_t total_allocated;
    size_t peak_used;
    
    // Find a free block that fits the requested size
    BlockHeader* find_free_block(size_t size) {
        BlockHeader* current = block_list;
        while (current) {
            if (current->is_free && current->size >= size) {
                return current;
            }
            current = current->next;
        }
        return nullptr;
    }
    
    // Coalesce adjacent free blocks
    void coalesce() {
        BlockHeader* current = block_list;
        while (current && current->next) {
            if (current->is_free && current->next->is_free) {
                // Merge current and next
                current->size += sizeof(BlockHeader) + current->next->size;
                current->next = current->next->next;
                if (current->next) {
                    current->next->prev = current;
                }
            } else {
                current = current->next;
            }
        }
    }
    
public:
    // Construct with fixed buffer capacity
    explicit GeneralAllocator(size_t buffer_size)
        : capacity(buffer_size), total_allocated(0), peak_used(0) {
        
        buffer = new uint8_t[buffer_size];
        
        // Create initial free block covering entire buffer
        block_list = reinterpret_cast<BlockHeader*>(buffer);
        block_list->size = buffer_size - sizeof(BlockHeader);
        block_list->is_free = true;
        block_list->prev = nullptr;
        block_list->next = nullptr;
    }
    
    ~GeneralAllocator() {
        delete[] buffer;
    }
    
    // Allocate memory with alignment
    void* allocate(size_t size, size_t alignment = 16) override {
        if (size == 0) return nullptr;
        
        // Align the allocation size itself
        size = align_up(size, alignment);
        
        // Find a free block large enough
        BlockHeader* block = find_free_block(size);
        if (!block) {
            return nullptr;  // Out of memory
        }
        
        // If block is larger than needed, split it
        if (block->size > size) {
            BlockHeader* new_block = reinterpret_cast<BlockHeader*>(
                reinterpret_cast<uint8_t*>(block) + sizeof(BlockHeader) + size
            );
            new_block->size = block->size - size - sizeof(BlockHeader);
            new_block->is_free = true;
            new_block->next = block->next;
            new_block->prev = block;
            
            if (block->next) {
                block->next->prev = new_block;
            }
            block->next = new_block;
            block->size = size;
        }
        
        // Mark block as in-use
        block->is_free = false;
        
        // Update tracking
        total_allocated += size;
        if (total_allocated > peak_used) {
            peak_used = total_allocated;
        }
        
        // Return pointer to data (after header)
        return reinterpret_cast<uint8_t*>(block) + sizeof(BlockHeader);
    }
    
    // Deallocate memory
    void deallocate(void* ptr) override {
        if (!ptr) return;
        
        // Find the block header (it's right before the data)
        BlockHeader* block = reinterpret_cast<BlockHeader*>(
            reinterpret_cast<uint8_t*>(ptr) - sizeof(BlockHeader)
        );
        
        if (block->is_free) {
            return;  // Double-free detected
        }
        
        block->is_free = true;
        total_allocated -= block->size;
        
        // Try to coalesce with neighbors
        coalesce();
    }
    
    // Get statistics
    AllocationStats get_stats() const override {
        AllocationStats stats;
        stats.total_allocated = total_allocated;
        stats.total_reserved = capacity;
        stats.peak_used = peak_used;
        
        // Count allocations and estimate fragmentation
        size_t free_blocks = 0;
        size_t free_space = 0;
        BlockHeader* current = block_list;
        while (current) {
            if (current->is_free) {
                free_blocks++;
                free_space += current->size;
            } else {
                stats.allocation_count++;
            }
            current = current->next;
        }
        
        // Fragmentation: how many free blocks exist
        stats.fragmentation = free_blocks;
        
        return stats;
    }
    
    // Defragment by coalescing all free blocks
    void defragment() override {
        coalesce();
    }
};

}  // namespace gws::memory
