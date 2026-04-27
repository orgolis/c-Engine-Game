#pragma once

#include <spdlog/spdlog.h>

#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace gws::logging {

namespace detail {
    spdlog::logger* default_spdlog_logger();
}

class Logger {
public:
    static void Initialize(std::string_view app_name, bool enable_file_logging = false);
    static void Shutdown();
    static std::shared_ptr<Logger> GetLogger(std::string_view name);
    static std::shared_ptr<Logger> GetDefault();

    template<typename... Args>
    void Debug(std::string_view fmt, Args&&... args) {
        if (impl) impl->debug(SPDLOG_FMT_RUNTIME(fmt), std::forward<Args>(args)...);
    }

    template<typename... Args>
    void Info(std::string_view fmt, Args&&... args) {
        if (impl) impl->info(SPDLOG_FMT_RUNTIME(fmt), std::forward<Args>(args)...);
    }

    template<typename... Args>
    void Warn(std::string_view fmt, Args&&... args) {
        if (impl) impl->warn(SPDLOG_FMT_RUNTIME(fmt), std::forward<Args>(args)...);
    }

    template<typename... Args>
    void Error(std::string_view fmt, Args&&... args) {
        if (impl) impl->error(SPDLOG_FMT_RUNTIME(fmt), std::forward<Args>(args)...);
    }

    template<typename... Args>
    void Critical(std::string_view fmt, Args&&... args) {
        if (impl) impl->critical(SPDLOG_FMT_RUNTIME(fmt), std::forward<Args>(args)...);
    }

    void SetLevel(int level);
    void Flush();

    explicit Logger(std::shared_ptr<spdlog::logger> spdlog_impl);

private:
    std::shared_ptr<spdlog::logger> impl;
};

template<typename... Args>
void Debug(std::string_view fmt, Args&&... args) {
    if (auto* l = detail::default_spdlog_logger()) l->debug(SPDLOG_FMT_RUNTIME(fmt), std::forward<Args>(args)...);
}

template<typename... Args>
void Info(std::string_view fmt, Args&&... args) {
    if (auto* l = detail::default_spdlog_logger()) l->info(SPDLOG_FMT_RUNTIME(fmt), std::forward<Args>(args)...);
}

template<typename... Args>
void Warn(std::string_view fmt, Args&&... args) {
    if (auto* l = detail::default_spdlog_logger()) l->warn(SPDLOG_FMT_RUNTIME(fmt), std::forward<Args>(args)...);
}

template<typename... Args>
void Error(std::string_view fmt, Args&&... args) {
    if (auto* l = detail::default_spdlog_logger()) l->error(SPDLOG_FMT_RUNTIME(fmt), std::forward<Args>(args)...);
}

template<typename... Args>
void Critical(std::string_view fmt, Args&&... args) {
    if (auto* l = detail::default_spdlog_logger()) l->critical(SPDLOG_FMT_RUNTIME(fmt), std::forward<Args>(args)...);
}

}  // namespace gws::logging

#define GWS_LOG_DEBUG(...) gws::logging::Debug(__VA_ARGS__)
#define GWS_LOG_INFO(...) gws::logging::Info(__VA_ARGS__)
#define GWS_LOG_WARN(...) gws::logging::Warn(__VA_ARGS__)
#define GWS_LOG_ERROR(...) gws::logging::Error(__VA_ARGS__)
#define GWS_LOG_CRITICAL(...) gws::logging::Critical(__VA_ARGS__)
