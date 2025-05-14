#ifndef LOGGER_H
#define LOGGER_H

#include <string>
#include <cstdio> // For vsnprintf, printf
#include <cstdarg> // For va_list, va_start, va_end
#include <mutex>   // For std::mutex
#include <iostream> // For std::cout, std::cerr

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
        // fprintf(stream, "%s %s: ", timeBuffer, levelStr);
        fprintf(stream, "%s: ", levelStr);

        va_list args;
        va_start(args, format);
        vfprintf(stream, format, args);
        va_end(args);

        fprintf(stream, "\n");
        fflush(stream); // Ensure immediate output, especially for errors/found items
    }

    void SetMinLevel(LogLevel level) {
        _minLevel = level;
    }

private:
    LogLevel _minLevel;
    std::mutex _logMutex;
};

#endif // LOGGER_H 