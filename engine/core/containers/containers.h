#pragma once

#include <vector>
#include <unordered_map>
#include <deque>
#include <memory>

namespace gws::containers {

// ====================
// Custom Allocator Support
// Wrappers around STL containers that support custom allocators
// ====================

// Vector with default allocator
template<typename T>
using Vector = std::vector<T>;

// Hash map with default allocator
template<typename Key, typename Value>
using HashMap = std::unordered_map<Key, Value>;

// Deque with default allocator
template<typename T>
using Deque = std::deque<T>;

// ====================
// Allocator Aware Containers
// For use with custom allocators (GeneralAllocator, StackAllocator, etc.)
// ====================

// Vector with custom allocator
template<typename T, typename Allocator = std::allocator<T>>
using AllocVector = std::vector<T, Allocator>;

// Hash map with custom allocator
template<typename Key, typename Value, typename Allocator = std::allocator<std::pair<const Key, Value>>>
using AllocHashMap = std::unordered_map<Key, Value, std::hash<Key>, std::equal_to<Key>, Allocator>;

// Deque with custom allocator
template<typename T, typename Allocator = std::allocator<T>>
using AllocDeque = std::deque<T, Allocator>;

// ====================
// Dynamic Array
// Simple wrapper providing bounds checking in debug builds
// ====================

template<typename T>
class DynamicArray {
public:
    DynamicArray() = default;
    explicit DynamicArray(size_t capacity) : data(capacity) {}
    
    // Element access
    T& operator[](size_t index) { return data[index]; }
    const T& operator[](size_t index) const { return data[index]; }
    
    // Get underlying vector
    std::vector<T>& GetData() { return data; }
    const std::vector<T>& GetData() const { return data; }
    
    // Size/capacity
    size_t Size() const { return data.size(); }
    size_t Capacity() const { return data.capacity(); }
    bool IsEmpty() const { return data.empty(); }
    
    // Modifiers
    void Push(const T& value) { data.push_back(value); }
    void Push(T&& value) { data.push_back(std::move(value)); }
    
    void Pop() { 
        if (!data.empty()) data.pop_back();
    }
    
    void Clear() { data.clear(); }
    void Reserve(size_t capacity) { data.reserve(capacity); }
    void Resize(size_t size) { data.resize(size); }
    void Resize(size_t size, const T& value) { data.resize(size, value); }
    
    // Iterators
    auto begin() { return data.begin(); }
    auto end() { return data.end(); }
    auto begin() const { return data.begin(); }
    auto end() const { return data.end(); }
    
private:
    std::vector<T> data;
};

// ====================
// Ring Buffer (Circular Buffer)
// Useful for fixed-size buffers like frame data, queues
// ====================

template<typename T>
class RingBuffer {
public:
    explicit RingBuffer(size_t capacity) 
        : data(capacity), capacity(capacity), head(0), tail(0), count(0) {}
    
    // Push element (overwrites if full)
    void Push(const T& value) {
        data[head] = value;
        head = (head + 1) % capacity;
        if (count < capacity) {
            count++;
        } else {
            tail = (tail + 1) % capacity;
        }
    }
    
    // Pop element from back
    T Pop() {
        if (count == 0) throw std::runtime_error("RingBuffer is empty");
        tail = (tail + 1) % capacity;
        count--;
        return data[tail];
    }
    
    // Access front element
    T& Front() {
        if (count == 0) throw std::runtime_error("RingBuffer is empty");
        return data[tail];
    }
    
    const T& Front() const {
        if (count == 0) throw std::runtime_error("RingBuffer is empty");
        return data[tail];
    }
    
    // Size info
    size_t Size() const { return count; }
    size_t Capacity() const { return capacity; }
    bool IsEmpty() const { return count == 0; }
    bool IsFull() const { return count == capacity; }
    
    // Clear buffer
    void Clear() {
        head = 0;
        tail = 0;
        count = 0;
    }
    
private:
    std::vector<T> data;
    size_t capacity;
    size_t head;      // Write position
    size_t tail;      // Read position
    size_t count;     // Number of elements
};

// ====================
// Pool (Fixed-size object pool)
// Useful for frame allocations, entity pools
// ====================

template<typename T>
class ObjectPool {
public:
    explicit ObjectPool(size_t initial_capacity = 256) {
        available.reserve(initial_capacity);
        for (size_t i = 0; i < initial_capacity; ++i) {
            available.push_back(std::make_unique<T>());
        }
    }
    
    // Acquire an object from the pool
    std::unique_ptr<T> Acquire() {
        if (!available.empty()) {
            auto obj = std::move(available.back());
            available.pop_back();
            return obj;
        }
        return std::make_unique<T>();
    }
    
    // Return an object to the pool
    void Release(std::unique_ptr<T> obj) {
        if (obj) {
            available.push_back(std::move(obj));
        }
    }
    
    // Get pool statistics
    size_t AvailableCount() const { return available.size(); }
    
    // Clear all objects
    void Clear() { available.clear(); }
    
private:
    std::vector<std::unique_ptr<T>> available;
};

}  // namespace gws::containers
