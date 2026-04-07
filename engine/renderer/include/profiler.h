#pragma once

#include <chrono>
#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include <cstdint>

namespace schizo::renderer {

// ============================================================================
// Performance Profiling System
// ============================================================================

/**
 * @struct ProfileResult
 * @brief Result of a profiled operation
 */
struct ProfileResult {
    std::string name;
    double duration_ms = 0.0;
    uint64_t call_count = 0;
    double min_ms = 0.0;
    double max_ms = 0.0;
    double avg_ms = 0.0;
    
    ProfileResult() = default;
    ProfileResult(const std::string& n) : name(n) {}
};

/**
 * @class PerformanceProfiler
 * @brief Simple CPU profiler for rendering performance analysis
 * 
 * Usage:
 * - Create profiler and add scopes
 * - Profiles named CPU operations
 * - Tracks min/max/average times
 * - Generates reports
 * 
 * Not for GPU profiling (see RenderDevice for that)
 */
class PerformanceProfiler {
public:
    /**
     * Create a performance profiler instance
     * @return Unique pointer to profiler
     */
    static std::unique_ptr<PerformanceProfiler> Create();
    
    virtual ~PerformanceProfiler() = default;
    
    /**
     * Begin profiling a named scope
     * Call within a scope or use PROFILE_SCOPE macro
     */
    virtual void BeginScope(const std::string& scope_name) = 0;
    
    /**
     * End current profiling scope
     * Must match BeginScope call
     */
    virtual void EndScope() = 0;
    
    /**
     * Get result for a specific scope
     * @param scope_name Name of profiled scope
     * @return Profile result, or nullptr if not found
     */
    virtual ProfileResult* GetResult(const std::string& scope_name) = 0;
    virtual const ProfileResult* GetResult(const std::string& scope_name) const = 0;
    
    /**
     * Get all results
     */
    virtual std::vector<ProfileResult> GetAllResults() const = 0;
    
    /**
     * Reset all profiling data
     */
    virtual void Reset() = 0;
    
    /**
     * Clear a specific result
     */
    virtual void ClearResult(const std::string& scope_name) = 0;
    
    /**
     * Print report to console
     * @param sort_by_time If true, sort by duration; otherwise by name
     */
    virtual void PrintReport(bool sort_by_time = true) const = 0;
    
    /**
     * Get report as string
     */
    virtual std::string GetReportString(bool sort_by_time = true) const = 0;
    
    /**
     * Enable/disable profiling
     */
    virtual void SetEnabled(bool enabled) = 0;
    virtual bool IsEnabled() const = 0;
    
    /**
     * Get total profiled time across all scopes
     */
    virtual double GetTotalTime() const = 0;

protected:
    PerformanceProfiler() = default;
};

/**
 * @class ProfileScope
 * @brief RAII helper for automatic profiling
 * 
 * Usage:
 * {
 *     ProfileScope scope(profiler, "MyOperation");
 *     // Do work here
 * }  // Automatically ends scope
 */
class ProfileScope {
public:
    ProfileScope(PerformanceProfiler* profiler, const std::string& scope_name)
        : profiler_(profiler) {
        if (profiler_) {
            profiler_->BeginScope(scope_name);
        }
    }
    
    ~ProfileScope() {
        if (profiler_) {
            profiler_->EndScope();
        }
    }
    
    // No copying
    ProfileScope(const ProfileScope&) = delete;
    ProfileScope& operator=(const ProfileScope&) = delete;
    
private:
    PerformanceProfiler* profiler_;
};

// Macro for convenient profiling
#define PROFILE_SCOPE(profiler, name) ProfileScope _profile_scope(profiler, name)

// ============================================================================
// GPU Performance Profiler
// ============================================================================

/**
 * @class GPUProfiler
 * @brief GPU-based performance profiler using OpenGL queries
 * 
 * Measures actual GPU execution time using timer queries
 * More accurate for GPU bottleneck identification
 */
class GPUProfiler {
public:
    static std::unique_ptr<GPUProfiler> Create();
    
    virtual ~GPUProfiler() = default;
    
    /**
     * Begin GPU measurement
     * @param query_name Name of query
     */
    virtual void BeginQuery(const std::string& query_name) = 0;
    
    /**
     * End GPU measurement
     */
    virtual void EndQuery() = 0;
    
    /**
     * Get GPU time for a query (in milliseconds)
     * May return 0 if query not yet complete
     */
    virtual double GetQueryResult(const std::string& query_name) const = 0;
    
    /**
     * Wait for all pending queries to complete
     * Blocks until GPU is done
     */
    virtual void Flush() = 0;
    
    /**
     * Reset all queries
     */
    virtual void Reset() = 0;
    
    /**
     * Get all query results
     */
    virtual std::vector<ProfileResult> GetAllResults() const = 0;
    
    /**
     * Print report
     */
    virtual void PrintReport() const = 0;

protected:
    GPUProfiler() = default;
};

// ============================================================================
// Frame Time Tracker
// ============================================================================

/**
 * @class FrameTimeTracker
 * @brief Tracks frame time statistics
 * 
 * Useful for FPS calculations and frame time graphs
 */
class FrameTimeTracker {
public:
    static std::unique_ptr<FrameTimeTracker> Create();
    
    virtual ~FrameTimeTracker() = default;
    
    /**
     * Mark beginning of frame
     */
    virtual void BeginFrame() = 0;
    
    /**
     * Mark end of frame
     * Auto-calculates frame time
     */
    virtual void EndFrame() = 0;
    
    /**
     * Get average frame time (ms)
     */
    virtual double GetAverageFrameTime() const = 0;
    
    /**
     * Get minimum frame time (ms)
     */
    virtual double GetMinFrameTime() const = 0;
    
    /**
     * Get maximum frame time (ms)
     */
    virtual double GetMaxFrameTime() const = 0;
    
    /**
     * Get current FPS (frames per second)
     */
    virtual double GetFPS() const = 0;
    
    /**
     * Get frame count since tracking started
     */
    virtual uint64_t GetFrameCount() const = 0;
    
    /**
     * Reset tracking
     */
    virtual void Reset() = 0;

protected:
    FrameTimeTracker() = default;
};

// ============================================================================
// Memory Profiler
// ============================================================================

/**
 * @class MemoryProfiler
 * @brief Tracks memory usage of rendering resources
 */
class MemoryProfiler {
public:
    static std::unique_ptr<MemoryProfiler> Create();
    
    virtual ~MemoryProfiler() = default;
    
    /**
     * Track buffer allocation
     * @param buffer_id Unique buffer ID
     * @param size_bytes Size in bytes
     * @param buffer_type Type description (e.g., "VBO", "Texture")
     */
    virtual void TrackBufferAllocation(uint32_t buffer_id, size_t size_bytes, const std::string& buffer_type) = 0;
    
    /**
     * Track buffer deallocation
     */
    virtual void TrackBufferDeallocation(uint32_t buffer_id) = 0;
    
    /**
     * Get total GPU memory used
     */
    virtual size_t GetTotalMemoryUsed() const = 0;
    
    /**
     * Get memory used by type
     */
    virtual size_t GetMemoryUsedByType(const std::string& type) const = 0;
    
    /**
     * Get number of allocations
     */
    virtual uint32_t GetAllocationCount() const = 0;
    
    /**
     * Print memory report
     */
    virtual void PrintReport() const = 0;
    
    /**
     * Reset tracking
     */
    virtual void Reset() = 0;

protected:
    MemoryProfiler() = default;
};

} // namespace schizo::renderer
