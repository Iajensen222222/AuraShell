#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_all.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>
#include <chrono>

// Mock logger interface for testing (before implementation)
namespace aura::logging {

class Logger {
public:
    static Logger& getInstance();

    // Logging methods
    void debug(const std::string& module, const std::string& msg);
    void info(const std::string& module, const std::string& msg);
    void warn(const std::string& module, const std::string& msg);
    void error(const std::string& module, const std::string& msg);

    // Test utilities
    void setOutputPath(const std::string& path);
    std::string getOutputPath() const;
    void clearLogs();
    bool isInitialized() const;

private:
    Logger();
    std::string m_outputPath;
    bool m_initialized;
};

} // namespace aura::logging

// ============================================================================
// TESTS: Logger Initialization & Configuration
// ============================================================================

TEST_CASE("Logger::Initialization", "[logging]") {
    using namespace aura::logging;

    SECTION("Logger singleton accessible") {
        Logger& logger1 = Logger::getInstance();
        Logger& logger2 = Logger::getInstance();

        // Should return same instance
        REQUIRE(&logger1 == &logger2);
    }

    SECTION("Logger initializes successfully") {
        Logger& logger = Logger::getInstance();

        // Should have initialized
        REQUIRE(logger.isInitialized());
    }

    SECTION("Default log output path configured") {
        Logger& logger = Logger::getInstance();
        std::string path = logger.getOutputPath();

        // Should contain AuraShell in path
        REQUIRE_THAT(path, Catch::Matchers::ContainsSubstring("AuraShell"));
        REQUIRE_THAT(path, Catch::Matchers::ContainsSubstring("logs"));
    }
}

// ============================================================================
// TESTS: Logging Output & Format
// ============================================================================

TEST_CASE("Logger::LogOutput", "[logging]") {
    using namespace aura::logging;

    Logger& logger = Logger::getInstance();
    logger.clearLogs();

    SECTION("Log message written to file") {
        logger.info("test_module", "Test message");

        std::string logPath = logger.getOutputPath();
        std::ifstream logFile(logPath);
        std::string line;

        // Log file should exist and contain message
        REQUIRE(std::filesystem::exists(logPath));
        REQUIRE(std::getline(logFile, line));
        REQUIRE_THAT(line, Catch::Matchers::ContainsSubstring("Test message"));
    }

    SECTION("Log format includes timestamp") {
        logger.info("test_module", "Timestamped message");

        std::string logPath = logger.getOutputPath();
        std::ifstream logFile(logPath);
        std::string line;
        std::getline(logFile, line);

        // Format: [YYYY-MM-DD HH:MM:SS.mmm] [LEVEL] [module] message
        REQUIRE_THAT(line, Catch::Matchers::ContainsSubstring("["));
        REQUIRE_THAT(line, Catch::Matchers::ContainsSubstring("]"));
    }

    SECTION("Log format includes level indicator") {
        logger.debug("test_module", "Debug msg");
        logger.info("test_module", "Info msg");
        logger.warn("test_module", "Warn msg");
        logger.error("test_module", "Error msg");

        std::string logPath = logger.getOutputPath();
        std::ifstream logFile(logPath);
        std::string line;
        int lineCount = 0;

        while (std::getline(logFile, line)) {
            lineCount++;
            if (lineCount == 1) {
                REQUIRE_THAT(line, Catch::Matchers::ContainsSubstring("DEBUG"));
            } else if (lineCount == 2) {
                REQUIRE_THAT(line, Catch::Matchers::ContainsSubstring("INFO"));
            } else if (lineCount == 3) {
                REQUIRE_THAT(line, Catch::Matchers::ContainsSubstring("WARN"));
            } else if (lineCount == 4) {
                REQUIRE_THAT(line, Catch::Matchers::ContainsSubstring("ERROR"));
            }
        }

        REQUIRE(lineCount == 4);
    }

    SECTION("Log format includes module name") {
        logger.info("taskbar_engine", "Taskbar message");
        logger.info("audio_visualizer", "Audio message");

        std::string logPath = logger.getOutputPath();
        std::ifstream logFile(logPath);
        std::string line1, line2;

        std::getline(logFile, line1);
        std::getline(logFile, line2);

        REQUIRE_THAT(line1, Catch::Matchers::ContainsSubstring("taskbar_engine"));
        REQUIRE_THAT(line2, Catch::Matchers::ContainsSubstring("audio_visualizer"));
    }
}

// ============================================================================
// TESTS: Logging Levels
// ============================================================================

TEST_CASE("Logger::LogLevels", "[logging]") {
    using namespace aura::logging;

    Logger& logger = Logger::getInstance();
    logger.clearLogs();

    SECTION("All log levels produce output") {
        logger.debug("test", "DEBUG message");
        logger.info("test", "INFO message");
        logger.warn("test", "WARN message");
        logger.error("test", "ERROR message");

        std::string logPath = logger.getOutputPath();
        std::ifstream logFile(logPath);
        int lineCount = 0;
        std::string line;

        while (std::getline(logFile, line)) {
            lineCount++;
        }

        // Should have 4 log lines
        REQUIRE(lineCount == 4);
    }

    SECTION("Error messages are prioritized in output") {
        logger.error("test", "Critical error");
        logger.info("test", "Info message");

        std::string logPath = logger.getOutputPath();
        std::ifstream logFile(logPath);
        std::string line1, line2;

        std::getline(logFile, line1);
        std::getline(logFile, line2);

        // Both should exist, error first
        REQUIRE_THAT(line1, Catch::Matchers::ContainsSubstring("Critical error"));
        REQUIRE_THAT(line2, Catch::Matchers::ContainsSubstring("Info message"));
    }
}

// ============================================================================
// TESTS: File Rotation & Management
// ============================================================================

TEST_CASE("Logger::FileRotation", "[logging]") {
    using namespace aura::logging;

    Logger& logger = Logger::getInstance();
    logger.clearLogs();

    SECTION("Log file path is deterministic") {
        std::string path1 = logger.getOutputPath();
        std::string path2 = logger.getOutputPath();

        // Same path on repeated calls
        REQUIRE(path1 == path2);
    }

    SECTION("Multiple log entries accumulate in file") {
        for (int i = 0; i < 10; i++) {
            logger.info("test", "Message " + std::to_string(i));
        }

        std::string logPath = logger.getOutputPath();
        std::ifstream logFile(logPath);
        int lineCount = 0;
        std::string line;

        while (std::getline(logFile, line)) {
            lineCount++;
        }

        // Should have 10 lines
        REQUIRE(lineCount == 10);
    }
}

// ============================================================================
// TESTS: Thread Safety
// ============================================================================

TEST_CASE("Logger::ThreadSafety", "[logging]") {
    using namespace aura::logging;

    Logger& logger = Logger::getInstance();
    logger.clearLogs();

    SECTION("Multiple threads can log simultaneously") {
        const int NUM_THREADS = 4;
        const int MSGS_PER_THREAD = 25;
        std::vector<std::thread> threads;

        for (int t = 0; t < NUM_THREADS; t++) {
            threads.emplace_back([&logger, t]() {
                for (int i = 0; i < MSGS_PER_THREAD; i++) {
                    logger.info("thread_" + std::to_string(t),
                               "Message " + std::to_string(i));
                }
            });
        }

        // Wait for all threads
        for (auto& thread : threads) {
            thread.join();
        }

        // Verify all messages logged
        std::string logPath = logger.getOutputPath();
        std::ifstream logFile(logPath);
        int lineCount = 0;
        std::string line;

        while (std::getline(logFile, line)) {
            lineCount++;
        }

        REQUIRE(lineCount == NUM_THREADS * MSGS_PER_THREAD);
    }
}

// ============================================================================
// TESTS: Performance (< 1ms per log call target)
// ============================================================================

TEST_CASE("Logger::Performance", "[logging][performance]") {
    using namespace aura::logging;

    Logger& logger = Logger::getInstance();
    logger.clearLogs();

    SECTION("Single log call completes in < 1ms") {
        auto start = std::chrono::high_resolution_clock::now();

        logger.info("test", "Performance test message");

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

        // Should complete quickly (< 1ms for single call)
        REQUIRE(duration.count() < 1);
    }

    SECTION("1000 log calls complete in < 100ms") {
        auto start = std::chrono::high_resolution_clock::now();

        for (int i = 0; i < 1000; i++) {
            logger.debug("test", "Message " + std::to_string(i));
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

        // 1000 calls should be batched/efficient (< 100ms)
        REQUIRE(duration.count() < 100);
    }
}
