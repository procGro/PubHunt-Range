#ifndef LOGGER_H
#define LOGGER_H

#include <string>
#include <cstdio> // For vsnprintf, printf
#include <cstdarg> // For va_list, va_start, va_end
#include <mutex>   // For std::mutex
#include <iostream> // For std::cout, std::cerr
#include <cstring>  // For strncmp

enum class LogLevel {
    DEBUG,
    INFO,
    WARNING,
    ERROR,
    FOUND // Special level for found keys
};

class Logger {
public:
    Logger(LogLevel minLevel = LogLevel::INFO) : _minLevel(minLevel) {}

    void Log(LogLevel level, const char* format, ...) {
        if (level < _minLevel) {
            return;
        }

        std::lock_guard<std::mutex> lock(_logMutex);

        // Get current time for timestamp (optional, can be added)
        // time_t now = time(0);
        // tm *ltm = localtime(&now);
        // char timeBuffer[20];
        // strftime(timeBuffer, sizeof(timeBuffer), "%H:%M:%S", ltm);

        const char* levelStr = "";
        FILE* stream = stdout;
        switch (level) {
            case LogLevel::DEBUG:
                levelStr = "[DEBUG]";
                break;
            case LogLevel::INFO:
                levelStr = "[INFO]";
                break;
            case LogLevel::WARNING:
                levelStr = "[WARN]";
                stream = stderr;
                break;
            case LogLevel::ERROR:
                levelStr = "[ERROR]";
                stream = stderr;
                break;
            case LogLevel::FOUND:
                levelStr = "[FOUND]";
                break;
        }
        
        // Check if this is a status update (starts with "Status:")
        bool isStatusUpdate = (level == LogLevel::INFO && strncmp(format, "Status:", 7) == 0);
        
        // For non-status messages, add a newline first if we're in the middle of a status line
        if (!isStatusUpdate && _lastWasStatus) {
            fprintf(stream, "\n");
            _lastWasStatus = false;
        }
        
        if (isStatusUpdate) {
            // For status updates, use carriage return to keep output on same line
            fprintf(stream, "\r%s: ", levelStr);
            _lastWasStatus = true;
        } else {
            // For all other log messages, use normal format with newline
            fprintf(stream, "%s: ", levelStr);
            _lastWasStatus = false;
        }

        va_list args;
        va_start(args, format);
        vfprintf(stream, format, args);
        va_end(args);

        // Only add newline for non-status messages
        if (!isStatusUpdate) {
            fprintf(stream, "\n");
        }
        
        // Ensure immediate output, especially for errors/found items
        fflush(stream);
    }

    void SetMinLevel(LogLevel level) {
        _minLevel = level;
    }

private:
    LogLevel _minLevel;
    std::mutex _logMutex;
    bool _lastWasStatus = false; // Track if the last message was a status update
};

#endif // LOGGER_H 