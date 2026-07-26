module;

#include <string>
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

