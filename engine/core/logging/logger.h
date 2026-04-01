#pragma once

#include <string_view>
#include <memory>
#include <string>

// Forward declare spdlog types to keep it internal
namespace spdlog {
    class logger;
}

namespace gws::logging {

// ====================
// Logger
// Thread-safe logging system backed by spdlog
// Provides simple interface: debug, info, warn, error, critical
// ====================

class Logger {
public:
    // Initialize logging system
    // Creates console sink by default, optionally adds file sink
    static void Initialize(std::string_view app_name, bool enable_file_logging = false);
    
    // Shutdown and flush all loggers
    static void Shutdown();
    
    // Get or create a named logger
    static std::shared_ptr<Logger> GetLogger(std::string_view name);
    
    // Get the default logger
    static std::shared_ptr<Logger> GetDefault();
    
    // Logging methods (variadic, printf-style)
    template<typename... Args>
    void Debug(std::string_view fmt, Args&&... args);
    
    template<typename... Args>
    void Info(std::string_view fmt, Args&&... args);
    
    template<typename... Args>
    void Warn(std::string_view fmt, Args&&... args);
    
    template<typename... Args>
    void Error(std::string_view fmt, Args&&... args);
    
    template<typename... Args>
    void Critical(std::string_view fmt, Args&&... args);
    
    // Set log level for this logger
    void SetLevel(int level);  // 0=trace, 1=debug, 2=info, 3=warn, 4=error, 5=critical
    
    // Flush pending logs
    void Flush();
    
    // For internal use only - creates a Logger wrapping a spdlog logger
    explicit Logger(std::shared_ptr<spdlog::logger> spdlog_impl);

private:
    std::shared_ptr<spdlog::logger> impl;
};

// Global convenience functions
template<typename... Args>
void Debug(std::string_view fmt, Args&&... args);

template<typename... Args>
void Info(std::string_view fmt, Args&&... args);

template<typename... Args>
void Warn(std::string_view fmt, Args&&... args);

template<typename... Args>
void Error(std::string_view fmt, Args&&... args);

template<typename... Args>
void Critical(std::string_view fmt, Args&&... args);

}  // namespace gws::logging

// Convenience macros for common patterns
#define GWS_LOG_DEBUG(...) gws::logging::Debug(__VA_ARGS__)
#define GWS_LOG_INFO(...) gws::logging::Info(__VA_ARGS__)
#define GWS_LOG_WARN(...) gws::logging::Warn(__VA_ARGS__)
#define GWS_LOG_ERROR(...) gws::logging::Error(__VA_ARGS__)
#define GWS_LOG_CRITICAL(...) gws::logging::Critical(__VA_ARGS__)
