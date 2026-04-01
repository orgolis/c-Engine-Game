#pragma once

#include <vector>
#include <unordered_map>
#include <deque>
#include <memory>
#include <functional>

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
// Simple wrapper around std::vector providing:
// - Convenience methods (Push, Pop, etc.)
// - Safe iteration with range-for loops
// - Optional bounds checking in debug builds
// ====================

template<typename T>
class DynamicArray {
public:
    using value_type = T;
    using pointer = T*;
    using const_pointer = const T*;
    using reference = T&;
    using const_reference = const T&;
    using iterator = typename std::vector<T>::iterator;
    using const_iterator = typename std::vector<T>::const_iterator;
    
    // Constructors
    DynamicArray() = default;
    explicit DynamicArray(size_t capacity) : data(capacity) {}
    explicit DynamicArray(size_t count, const T& value) : data(count, value) {}
    
    // Copy/Move semantics
    DynamicArray(const DynamicArray&) = default;
    DynamicArray(DynamicArray&&) noexcept = default;
    DynamicArray& operator=(const DynamicArray&) = default;
    DynamicArray& operator=(DynamicArray&&) noexcept = default;
    
    // Element access
    reference operator[](size_t index) { 
        return data[index]; 
    }
    
    const_reference operator[](size_t index) const { 
        return data[index]; 
    }
    
    // Safe access with bounds checking
    reference At(size_t index) {
        if (index >= data.size()) {
            throw std::out_of_range("DynamicArray index out of range");
        }
        return data[index];
    }
    
    const_reference At(size_t index) const {
        if (index >= data.size()) {
            throw std::out_of_range("DynamicArray index out of range");
        }
        return data[index];
    }
    
    // First/Last element
    reference Front() { return data.front(); }
    const_reference Front() const { return data.front(); }
    reference Back() { return data.back(); }
    const_reference Back() const { return data.back(); }
    
    // Data access
    pointer Data() { return data.data(); }
    const_pointer Data() const { return data.data(); }
    
    // Get underlying vector
    std::vector<T>& GetData() { return data; }
    const std::vector<T>& GetData() const { return data; }
    
    // Size/capacity
    size_t Size() const { return data.size(); }
    size_t Capacity() const { return data.capacity(); }
    bool IsEmpty() const { return data.empty(); }
    
    // Modifiers
    template<typename... Args>
    reference Emplace(Args&&... args) {
        data.emplace_back(std::forward<Args>(args)...);
        return data.back();
    }
    
    void Push(const T& value) { 
        data.push_back(value); 
    }
    
    void Push(T&& value) { 
        data.push_back(std::move(value)); 
    }
    
    void Pop() { 
        if (!data.empty()) data.pop_back();
    }
    
    T PopValue() {
        if (data.empty()) {
            throw std::runtime_error("Cannot pop from empty DynamicArray");
        }
        T value = std::move(data.back());
        data.pop_back();
        return value;
    }
    
    void Clear() { 
        data.clear(); 
    }
    
    void Reserve(size_t capacity) { 
        data.reserve(capacity); 
    }
    
    void Resize(size_t size) { 
        data.resize(size); 
    }
    
    void Resize(size_t size, const T& value) { 
        data.resize(size, value); 
    }
    
    void Shrink() {
        data.shrink_to_fit();
    }
    
    // Erase element
    iterator Erase(const_iterator it) {
        return data.erase(it);
    }
    
    iterator Erase(const_iterator first, const_iterator last) {
        return data.erase(first, last);
    }
    
    // Iterators
    iterator begin() { return data.begin(); }
    iterator end() { return data.end(); }
    const_iterator begin() const { return data.begin(); }
    const_iterator end() const { return data.end(); }
    const_iterator cbegin() const { return data.cbegin(); }
    const_iterator cend() const { return data.cend(); }
    
private:
    std::vector<T> data;
};

// ====================
// Hash Map
// Wrapper around std::unordered_map with convenient APIs
// Type-safe generic key-value container
// ====================

template<typename Key, typename Value>
class HashMap {
public:
    using key_type = Key;
    using mapped_type = Value;
    using value_type = std::pair<const Key, Value>;
    using iterator = typename std::unordered_map<Key, Value>::iterator;
    using const_iterator = typename std::unordered_map<Key, Value>::const_iterator;
    
    // Constructors
    HashMap() = default;
    explicit HashMap(size_t bucket_count) : data(bucket_count) {}
    
    // Copy/Move semantics
    HashMap(const HashMap&) = default;
    HashMap(HashMap&&) noexcept = default;
    HashMap& operator=(const HashMap&) = default;
    HashMap& operator=(HashMap&&) noexcept = default;
    
    // Insert/Update
    std::pair<iterator, bool> Insert(const Key& key, const Value& value) {
        return data.insert({key, value});
    }
    
    std::pair<iterator, bool> Insert(const Key& key, Value&& value) {
        return data.insert({key, std::move(value)});
    }
    
    // Emplace
    template<typename... Args>
    std::pair<iterator, bool> Emplace(const Key& key, Args&&... args) {
        return data.emplace(key, Value(std::forward<Args>(args)...));
    }
    
    // Set/replace value (always inserts or updates)
    Value& Set(const Key& key, const Value& value) {
        return data[key] = value;
    }
    
    Value& Set(const Key& key, Value&& value) {
        return data[key] = std::move(value);
    }
    
    // Get value (safe interface)
    bool Get(const Key& key, Value& out_value) const {
        auto it = data.find(key);
        if (it != data.end()) {
            out_value = it->second;
            return true;
        }
        return false;
    }
    
    // Try to get (returns pointer or nullptr)
    Value* Find(const Key& key) {
        auto it = data.find(key);
        return (it != data.end()) ? &it->second : nullptr;
    }
    
    const Value* Find(const Key& key) const {
        auto it = data.find(key);
        return (it != data.end()) ? &it->second : nullptr;
    }
    
    // Element access with []
    Value& operator[](const Key& key) {
        return data[key];
    }
    
    // Contains check
    bool Contains(const Key& key) const {
        return data.find(key) != data.end();
    }
    
    // Erase
    bool Erase(const Key& key) {
        return data.erase(key) > 0;
    }
    
    iterator Erase(const_iterator it) {
        return data.erase(it);
    }
    
    // Size/capacity
    size_t Size() const { return data.size(); }
    bool IsEmpty() const { return data.empty(); }
    size_t BucketCount() const { return data.bucket_count(); }
    
    // Clear
    void Clear() { 
        data.clear(); 
    }
    
    // Rehash for performance
    void Rehash(size_t bucket_count) {
        data.rehash(bucket_count);
    }
    
    // Iterators
    iterator begin() { return data.begin(); }
    iterator end() { return data.end(); }
    const_iterator begin() const { return data.begin(); }
    const_iterator end() const { return data.end(); }
    const_iterator cbegin() const { return data.cbegin(); }
    const_iterator cend() const { return data.cend(); }
    
    // Get underlying map
    std::unordered_map<Key, Value>& GetData() { return data; }
    const std::unordered_map<Key, Value>& GetData() const { return data; }
    
private:
    std::unordered_map<Key, Value> data;
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
