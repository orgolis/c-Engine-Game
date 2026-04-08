#include "profiler.h"
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <numeric>

namespace schizo::renderer {

// ============================================================================
// PerformanceProfiler Implementation
// ============================================================================

class PerformanceProfilerImpl : public PerformanceProfiler {
public:
    PerformanceProfilerImpl() : enabled_(true) {}
    
    void BeginScope(const std::string& scope_name) override {
        if (!enabled_) return;
        scope_stack_.push_back({scope_name, std::chrono::high_resolution_clock::now()});
    }
    
    void EndScope() override {
        if (!enabled_ || scope_stack_.empty()) return;
        
        auto end_time = std::chrono::high_resolution_clock::now();
        auto scope = scope_stack_.back();
        scope_stack_.pop_back();
        
        auto duration = std::chrono::duration<double, std::milli>(end_time - scope.start_time).count();
        
        auto& result = results_[scope.name];
        result.name = scope.name;
        result.duration_ms += duration;
        result.call_count++;
        result.min_ms = (result.min_ms == 0.0) ? duration : std::min(result.min_ms, duration);
        result.max_ms = std::max(result.max_ms, duration);
        result.avg_ms = result.duration_ms / result.call_count;
    }
    
    ProfileResult* GetResult(const std::string& scope_name) override {
        auto it = results_.find(scope_name);
        return (it != results_.end()) ? &it->second : nullptr;
    }
    
    const ProfileResult* GetResult(const std::string& scope_name) const override {
        auto it = results_.find(scope_name);
        return (it != results_.end()) ? &it->second : nullptr;
    }
    
    std::vector<ProfileResult> GetAllResults() const override {
        std::vector<ProfileResult> results;
        for (const auto& pair : results_) {
            results.push_back(pair.second);
        }
        return results;
    }
    
    void Reset() override {
        results_.clear();
        scope_stack_.clear();
    }
    
    void ClearResult(const std::string& scope_name) override {
        results_.erase(scope_name);
    }
    
    void PrintReport(bool sort_by_time = true) const override {
        std::cout << GetReportString(sort_by_time);
    }
    
    std::string GetReportString(bool sort_by_time = true) const override {
        std::stringstream ss;
        
        std::vector<ProfileResult> sorted_results;
        for (const auto& pair : results_) {
            sorted_results.push_back(pair.second);
        }
        
        if (sort_by_time) {
            std::sort(sorted_results.begin(), sorted_results.end(),
                [](const ProfileResult& a, const ProfileResult& b) {
                    return a.duration_ms > b.duration_ms;
                });
        } else {
            std::sort(sorted_results.begin(), sorted_results.end(),
                [](const ProfileResult& a, const ProfileResult& b) {
                    return a.name < b.name;
                });
        }
        
        ss << "\n" << std::string(70, '=') << "\n";
        ss << "PERFORMANCE PROFILE REPORT\n";
        ss << std::string(70, '=') << "\n\n";
        
        ss << std::left << std::setw(30) << "Scope Name"
           << std::right << std::setw(12) << "Calls"
           << std::setw(12) << "Total (ms)"
           << std::setw(12) << "Avg (ms)"
           << std::setw(12) << "Min (ms)"
           << std::setw(12) << "Max (ms)" << "\n";
        ss << std::string(70, '-') << "\n";
        
        for (const auto& result : sorted_results) {
            ss << std::left << std::setw(30) << result.name
               << std::right << std::setw(12) << result.call_count
               << std::setw(12) << std::fixed << std::setprecision(3) << result.duration_ms
               << std::setw(12) << std::fixed << std::setprecision(3) << result.avg_ms
               << std::setw(12) << std::fixed << std::setprecision(3) << result.min_ms
               << std::setw(12) << std::fixed << std::setprecision(3) << result.max_ms << "\n";
        }
        
        ss << std::string(70, '=') << "\n\n";
        
        return ss.str();
    }
    
    void SetEnabled(bool enabled) override { enabled_ = enabled; }
    bool IsEnabled() const override { return enabled_; }
    
    double GetTotalTime() const override {
        double total = 0.0;
        for (const auto& pair : results_) {
            total += pair.second.duration_ms;
        }
        return total;
    }

private:
    struct ScopeRecord {
        std::string name;
        std::chrono::high_resolution_clock::time_point start_time;
    };
    
    std::unordered_map<std::string, ProfileResult> results_;
    std::vector<ScopeRecord> scope_stack_;
    bool enabled_;
};

std::unique_ptr<PerformanceProfiler> PerformanceProfiler::Create() {
    return std::make_unique<PerformanceProfilerImpl>();
}

// ============================================================================
// GPUProfiler Implementation
// ============================================================================

class GPUProfilerImpl : public GPUProfiler {
public:
    void BeginQuery(const std::string& query_name) override {
        // Stub implementation - would need OpenGL query objects
        current_query_name_ = query_name;
        query_start_time_ = std::chrono::high_resolution_clock::now();
    }
    
    void EndQuery() override {
        // Stub - would submit GPU query
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration<double, std::milli>(end_time - query_start_time_).count();
        
        if (!current_query_name_.empty()) {
            results_[current_query_name_] = duration;
        }
    }
    
    double GetQueryResult(const std::string& query_name) const override {
        auto it = results_.find(query_name);
        return (it != results_.end()) ? it->second : 0.0;
    }
    
    void Flush() override {
        // Stub - would glFlush/glFinish
    }
    
    void Reset() override {
        results_.clear();
    }
    
    std::vector<ProfileResult> GetAllResults() const override {
        std::vector<ProfileResult> results;
        for (const auto& pair : results_) {
            ProfileResult r;
            r.name = pair.first;
            r.duration_ms = pair.second;
            results.push_back(r);
        }
        return results;
    }
    
    void PrintReport() const override {
        std::cout << "\nGPU PROFILER REPORT\n";
        std::cout << std::string(50, '=') << "\n";
        std::cout << std::left << std::setw(30) << "Query Name"
                  << std::right << std::setw(15) << "Time (ms)" << "\n";
        std::cout << std::string(50, '-') << "\n";
        
        for (const auto& pair : results_) {
            std::cout << std::left << std::setw(30) << pair.first
                      << std::right << std::setw(15) << std::fixed << std::setprecision(3) << pair.second << "\n";
        }
        std::cout << std::string(50, '=') << "\n\n";
    }

private:
    std::unordered_map<std::string, double> results_;
    std::string current_query_name_;
    std::chrono::high_resolution_clock::time_point query_start_time_;
};

std::unique_ptr<GPUProfiler> GPUProfiler::Create() {
    return std::make_unique<GPUProfilerImpl>();
}

// ============================================================================
// FrameTimeTracker Implementation
// ============================================================================

class FrameTimeTrackerImpl : public FrameTimeTracker {
public:
    FrameTimeTrackerImpl() : frame_count_(0), min_frame_time_(999999.0), max_frame_time_(0.0) {}
    
    void BeginFrame() override {
        frame_start_ = std::chrono::high_resolution_clock::now();
    }
    
    void EndFrame() override {
        auto frame_end = std::chrono::high_resolution_clock::now();
        double frame_time_ms = std::chrono::duration<double, std::milli>(frame_end - frame_start_).count();
        
        frame_times_.push_back(frame_time_ms);
        if (frame_times_.size() > 60) {
            frame_times_.erase(frame_times_.begin());  // Keep last 60 frames
        }
        
        min_frame_time_ = std::min(min_frame_time_, frame_time_ms);
        max_frame_time_ = std::max(max_frame_time_, frame_time_ms);
        frame_count_++;
    }
    
    double GetAverageFrameTime() const override {
        if (frame_times_.empty()) return 0.0;
        return std::accumulate(frame_times_.begin(), frame_times_.end(), 0.0) / frame_times_.size();
    }
    
    double GetMinFrameTime() const override { return min_frame_time_; }
    double GetMaxFrameTime() const override { return max_frame_time_; }
    
    double GetFPS() const override {
        double avg = GetAverageFrameTime();
        return (avg > 0.0) ? 1000.0 / avg : 0.0;
    }
    
    uint64_t GetFrameCount() const override { return frame_count_; }
    
    void Reset() override {
        frame_times_.clear();
        frame_count_ = 0;
        min_frame_time_ = 999999.0;
        max_frame_time_ = 0.0;
    }

private:
    std::vector<double> frame_times_;
    uint64_t frame_count_;
    double min_frame_time_;
    double max_frame_time_;
    std::chrono::high_resolution_clock::time_point frame_start_;
};

std::unique_ptr<FrameTimeTracker> FrameTimeTracker::Create() {
    return std::make_unique<FrameTimeTrackerImpl>();
}

// ============================================================================
// MemoryProfiler Implementation
// ============================================================================

class MemoryProfilerImpl : public MemoryProfiler {
public:
    void TrackBufferAllocation(uint32_t buffer_id, size_t size_bytes, const std::string& buffer_type) override {
        allocations_[buffer_id] = {size_bytes, buffer_type};
    }
    
    void TrackBufferDeallocation(uint32_t buffer_id) override {
        allocations_.erase(buffer_id);
    }
    
    size_t GetTotalMemoryUsed() const override {
        size_t total = 0;
        for (const auto& pair : allocations_) {
            total += pair.second.size_bytes;
        }
        return total;
    }
    
    size_t GetMemoryUsedByType(const std::string& type) const override {
        size_t total = 0;
        for (const auto& pair : allocations_) {
            if (pair.second.type == type) {
                total += pair.second.size_bytes;
            }
        }
        return total;
    }
    
    uint32_t GetAllocationCount() const override {
        return static_cast<uint32_t>(allocations_.size());
    }
    
    void PrintReport() const override {
        std::cout << "\nMEMORY PROFILER REPORT\n";
        std::cout << std::string(50, '=') << "\n";
        std::cout << "Total Memory: " << (GetTotalMemoryUsed() / 1024.0 / 1024.0) << " MB\n";
        std::cout << "Allocation Count: " << GetAllocationCount() << "\n";
        std::cout << std::string(50, '=') << "\n";
    }
    
    void Reset() override {
        allocations_.clear();
    }

private:
    struct Allocation {
        size_t size_bytes;
        std::string type;
    };
    
    std::unordered_map<uint32_t, Allocation> allocations_;
};

std::unique_ptr<MemoryProfiler> MemoryProfiler::Create() {
    return std::make_unique<MemoryProfilerImpl>();
}

} // namespace schizo::renderer
