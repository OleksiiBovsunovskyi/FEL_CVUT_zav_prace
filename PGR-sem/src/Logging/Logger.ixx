module;

#include <cstdint>
#include <string>
#include <utility>
#include <iostream>
#include <chrono>
#include <source_location>
#include <format>
export module Logger;

constexpr std::string_view RESET  = "\033[0m";
constexpr std::string_view WHITE  = "\033[37m";
constexpr std::string_view YELLOW = "\033[33m";
constexpr std::string_view RED    = "\033[31m";

export std::string getCurrentTime() {
    const auto now = std::chrono::system_clock::now();
    auto time = std::chrono::floor<std::chrono::seconds>(now);
    return std::format("{:%H:%M:%S}", time);
}

/// Abbreviates a count for reading: 456, 3k, 2.1m, 18.2b.
export std::string formatCount(uint64_t count) {
    constexpr std::pair<uint64_t, char> units[]{
        {1'000'000'000, 'b'}, {1'000'000, 'm'}, {1'000, 'k'}};
    for (const auto& [scale, suffix] : units)
        if (count >= scale)
            return std::format("{:.3g}{}",
                               static_cast<double>(count) / scale, suffix);
    return std::format("{}", count);
}

export void logMessage(const std::string& message,
    const std::source_location loc = std::source_location::current())
{
    std::cout << WHITE
              << "[" << getCurrentTime() << "] "
              << "[" << loc.file_name() << ":" << loc.line() << " " << loc.function_name() << "] "
              << message
              << RESET << "\n";
}

export void logWarning(const std::string& message,
    const std::source_location loc = std::source_location::current())
{
    std::cerr << YELLOW
              << "[" << getCurrentTime() << "] "
              << "[" << loc.file_name() << ":" << loc.line() << " " << loc.function_name() << "] "
              << "Warning: " << message
              << RESET << "\n";
}

export void logError(const std::string& message,
    const std::source_location loc = std::source_location::current())
{
    std::cerr << RED
              << "[" << getCurrentTime() << "] "
              << "[" << loc.file_name() << ":" << loc.line() << " " << loc.function_name() << "] "
              << "Error: " << message
              << RESET << "\n";
}

