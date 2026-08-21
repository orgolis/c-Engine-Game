#include "logger.h"

#include <spdlog/sinks/stdout_color_sinks.h>

#include <memory>
#include <mutex>
#include <stdexcept>

namespace gws::logging {

static std::shared_ptr<spdlog::logger> g_default_logger;
static std::mutex g_logger_mutex;

namespace detail {
spdlog::logger* default_spdlog_logger() {
    // gws::logging::Logger::Initialize() is never called by the editor (it
    // sets up logging via gws::diag::init_logging instead, which mutates
    // spdlog's own default logger rather than this module's). Before this
    // fallback, every GWS_LOG_* call anywhere in the engine was a silent
    // no-op for the entire life of the process -- including error/warn
    // paths in vulkan_device.cpp, vulkan_swapchain.cpp, barriers.cpp,
    // graphics_pipeline.cpp, render_pass.cpp, shader_compiler.cpp,
    // vulkan_command_buffer.cpp, vulkan_descriptor_set.cpp, and
    // vulkan_surface.cpp: exactly the layer that would otherwise explain a
    // GPU crash or hang. Falling back to spdlog's active default logger
    // means those calls reach the same sinks (console + rotating file +
    // hang/crash ring buffer) as every spdlog::info()-style call elsewhere,
    // with zero call-site changes required.
    if (g_default_logger) return g_default_logger.get();
    return spdlog::default_logger_raw();
}
}

Logger::Logger(std::shared_ptr<spdlog::logger> spdlog_impl)
    : impl(std::move(spdlog_impl)) {
}

void Logger::Initialize(std::string_view app_name, bool enable_file_logging) {
    (void)enable_file_logging;

    std::lock_guard<std::mutex> lock(g_logger_mutex);

    try {
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

void Logger::SetLevel(int level) {
    if (impl) impl->set_level(static_cast<spdlog::level::level_enum>(level));
}

void Logger::Flush() {
    if (impl) impl->flush();
}

}  // namespace gws::logging
