#include "logger.h"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <memory>
#include <mutex>

namespace gws::logging {

// Global logger instance for convenience functions
static std::shared_ptr<spdlog::logger> g_default_logger;
static std::mutex g_logger_mutex;

Logger::Logger(std::shared_ptr<spdlog::logger> spdlog_impl)
    : impl(std::move(spdlog_impl)) {
}

void Logger::Initialize(std::string_view app_name, bool enable_file_logging) {
    (void)enable_file_logging;  // Currently unused - file logging will be added later
    
    std::lock_guard<std::mutex> lock(g_logger_mutex);
    
    try {
        // Create a simple console logger
        // Ignore file_logging for now - can be added in Phase 2 if needed
        std::string logger_name(app_name);
        
        g_default_logger = spdlog::stdout_color_mt(logger_name);
        g_default_logger->set_pattern("[%H:%M:%S] [%n] [%^%l%$] %v");
        g_default_logger->set_level(spdlog::level::debug);
        g_default_logger->flush_on(spdlog::level::err);
    } catch (const spdlog::spdlog_ex& ex) {
        throw std::runtime_error(std::string("Log initialization failed: ") + ex.what());
    }
}

void Logger::Shutdown() {
    std::lock_guard<std::mutex> lock(g_logger_mutex);
    spdlog::shutdown();
    g_default_logger.reset();
}

std::shared_ptr<Logger> Logger::GetLogger(std::string_view name) {
    std::lock_guard<std::mutex> lock(g_logger_mutex);
    
    if (!g_default_logger) {
        throw std::runtime_error("Logger system not initialized. Call Logger::Initialize() first.");
    }
    
    std::string name_str(name);
    auto spdlog_logger = spdlog::get(name_str);
    if (!spdlog_logger) {
        spdlog_logger = g_default_logger;
    }
    
    return std::make_shared<Logger>(spdlog_logger);
}

std::shared_ptr<Logger> Logger::GetDefault() {
    std::lock_guard<std::mutex> lock(g_logger_mutex);
    
    if (!g_default_logger) {
        throw std::runtime_error("Logger system not initialized. Call Logger::Initialize() first.");
    }
    
    return std::make_shared<Logger>(g_default_logger);
}

template<typename... Args>
void Logger::Debug(std::string_view fmt, Args&&... args) {
    if (impl) impl->debug(fmt, std::forward<Args>(args)...);
}

template<typename... Args>
void Logger::Info(std::string_view fmt, Args&&... args) {
    if (impl) impl->info(fmt, std::forward<Args>(args)...);
}

template<typename... Args>
void Logger::Warn(std::string_view fmt, Args&&... args) {
    if (impl) impl->warn(fmt, std::forward<Args>(args)...);
}

template<typename... Args>
void Logger::Error(std::string_view fmt, Args&&... args) {
    if (impl) impl->error(fmt, std::forward<Args>(args)...);
}

template<typename... Args>
void Logger::Critical(std::string_view fmt, Args&&... args) {
    if (impl) impl->critical(fmt, std::forward<Args>(args)...);
}

void Logger::SetLevel(int level) {
    if (impl) impl->set_level(static_cast<spdlog::level::level_enum>(level));
}

void Logger::Flush() {
    if (impl) impl->flush();
}

// Global convenience functions
template<typename... Args>
void Debug(std::string_view fmt, Args&&... args) {
    if (g_default_logger) g_default_logger->debug(fmt, std::forward<Args>(args)...);
}

template<typename... Args>
void Info(std::string_view fmt, Args&&... args) {
    if (g_default_logger) g_default_logger->info(fmt, std::forward<Args>(args)...);
}

template<typename... Args>
void Warn(std::string_view fmt, Args&&... args) {
    if (g_default_logger) g_default_logger->warn(fmt, std::forward<Args>(args)...);
}

template<typename... Args>
void Error(std::string_view fmt, Args&&... args) {
    if (g_default_logger) g_default_logger->error(fmt, std::forward<Args>(args)...);
}

template<typename... Args>
void Critical(std::string_view fmt, Args&&... args) {
    if (g_default_logger) g_default_logger->critical(fmt, std::forward<Args>(args)...);
}

}  // namespace gws::logging
