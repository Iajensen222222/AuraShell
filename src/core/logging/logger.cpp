#include "logger.h"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <windows.h>
#include <shlobj.h>
#include <filesystem>

namespace aura::logging {

static Logger* g_instance = nullptr;

// Get AppData path - %LOCALAPPDATA%\AuraShell\logs
static std::string getAppDataPath() {
    wchar_t appData[MAX_PATH] = {};
    HRESULT hr = SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, appData);
    if (FAILED(hr)) {
        return "";
    }

    std::filesystem::path logDir(appData);
    logDir /= L"AuraShell";
    logDir /= L"logs";

    std::error_code ec;
    std::filesystem::create_directories(logDir, ec);

    return logDir.string();
}

} // namespace aura::logging

using namespace aura::logging;

Logger::Logger() 
    : m_outputPath(getAppDataPath()), 
      m_initialized(false),
      m_logger(nullptr) {
    initialize();
}

Logger::~Logger() = default;

Logger& Logger::getInstance() {
    if (g_instance == nullptr) {
        g_instance = new Logger();
    }
    return *g_instance;
}

void Logger::initialize() {
    try {
        if (m_outputPath.empty()) {
            m_outputPath = aura::logging::getAppDataPath();
        }

        std::filesystem::path logPath(m_outputPath);
        logPath /= L"aurashell.log";

        const size_t MAX_FILE_SIZE = 10 * 1024 * 1024;  // 10 MB
        const size_t MAX_FILES = 5;

        // Drop any previously registered logger with this name before re-creating.
        spdlog::drop("AuraShell");

        // Create sinks
        std::vector<spdlog::sink_ptr> sinks;

        try {
            auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                logPath.string(),
                MAX_FILE_SIZE,
                MAX_FILES,
                true  // truncate = true so each initialize() starts a clean file
            );
            sinks.push_back(file_sink);
        } catch (...) {
            // If file sink fails, continue with console only
        }

        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        sinks.push_back(console_sink);

        if (!sinks.empty()) {
            m_logger = std::make_shared<spdlog::logger>("AuraShell", sinks.begin(), sinks.end());
            m_logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%t] %v");
            m_logger->set_level(spdlog::level::debug);
            // Flush every message so reads in tests always see current content.
            m_logger->flush_on(spdlog::level::trace);
            spdlog::register_logger(m_logger);
            m_initialized = true;
        }
    } catch (const std::exception& e) {
        fprintf(stderr, "Logger initialization failed: %s\n", e.what());
        m_initialized = false;
    }
}

void Logger::debug(const std::string& module, const std::string& msg) {
    if (!m_initialized || !m_logger) return;

    try {
        std::string formatted = module + " - " + msg;
        m_logger->debug(formatted);
    } catch (...) {
        // Silently fail
    }
}

void Logger::info(const std::string& module, const std::string& msg) {
    if (!m_initialized || !m_logger) return;

    try {
        std::string formatted = module + " - " + msg;
        m_logger->info(formatted);
    } catch (...) {
        // Silently fail
    }
}

void Logger::warn(const std::string& module, const std::string& msg) {
    if (!m_initialized || !m_logger) return;

    try {
        std::string formatted = module + " - " + msg;
        m_logger->warn(formatted);
    } catch (...) {
        // Silently fail
    }
}

void Logger::error(const std::string& module, const std::string& msg) {
    if (!m_initialized || !m_logger) return;

    try {
        std::string formatted = module + " - " + msg;
        m_logger->error(formatted);
    } catch (...) {
        // Silently fail
    }
}

void Logger::setOutputPath(const std::string& path) {
    m_outputPath = path;
}

std::string Logger::getOutputPath() const {
    return m_outputPath + "\\aurashell.log";
}

void Logger::clearLogs() {
    try {
        // Flush and release the spdlog sink so the file handle is closed before
        // we delete the file.  Without this, on Windows the "deleted" file
        // descriptor remains open and spdlog keeps appending to it.
        if (m_logger) {
            m_logger->flush();
        }
        spdlog::drop("AuraShell");
        m_logger.reset();
        m_initialized = false;

        // Now delete the log file with no open handles.
        std::filesystem::path logPath(m_outputPath);
        logPath /= L"aurashell.log";
        std::error_code ec;
        std::filesystem::remove(logPath, ec);

        // Re-initialize so subsequent logging works (truncate=true opens a fresh file).
        initialize();
    } catch (...) {
        // Silently fail
    }
}

bool Logger::isInitialized() const {
    return m_initialized;
}
