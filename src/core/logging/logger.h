#pragma once

#include <string>
#include <memory>

// Forward declare spdlog types to avoid including spdlog headers here
namespace spdlog {
    class logger;
}

namespace aura::logging {

/**
 * @class Logger
 * @brief Centralized logging system using spdlog backend
 * 
 * Singleton pattern for application-wide logging.
 * Thread-safe with rotating file sink.
 * 
 * Example:
 * @code
 *   Logger& logger = Logger::getInstance();
 *   logger.info("module_name", "Application started");
 *   logger.error("module_name", "Fatal error occurred");
 * @endcode
 */
class Logger {
public:
    /// Get singleton instance of Logger
    static Logger& getInstance();

    // Logging methods - all take module name and message
    void debug(const std::string& module, const std::string& msg);
    void info(const std::string& module, const std::string& msg);
    void warn(const std::string& module, const std::string& msg);
    void error(const std::string& module, const std::string& msg);

    // Configuration
    void setOutputPath(const std::string& path);
    std::string getOutputPath() const;
    void clearLogs();
    bool isInitialized() const;

private:
    Logger();
    ~Logger();

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    void initialize();

    std::string m_outputPath;
    bool m_initialized;
    std::shared_ptr<spdlog::logger> m_logger;
};

} // namespace aura::logging
