module;

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <format>
#include <string_view>
#include <utility>

module Metrics;

import Logger;

namespace {

bool same(const char* a, const char* b) {
    return std::string_view{a} == std::string_view{b};
}

/// @return the entry named `name`, or the end of `row`.
auto findEntry(auto& row, const char* name) {
    return std::ranges::find_if(
        row, [name](const auto& entry) { return same(entry.first, name); });
}

} // namespace

Metrics& Metrics::get() {
    static Metrics instance;
    return instance;
}

bool Metrics::open(const std::filesystem::path& directory) {
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) {
        logError("Metrics: could not create " + directory.string() + ": " +
                 error.message());
        return false;
    }

    const auto now = std::chrono::floor<std::chrono::seconds>(
        std::chrono::system_clock::now());
    const std::filesystem::path path =
        directory / std::format("{:%Y-%m-%d_%H-%M-%S}.csv",
                                std::chrono::zoned_time{
                                    std::chrono::current_zone(), now});

    file_.open(path, std::ios::out);
    if (!file_) {
        logError("Metrics: could not open " + path.string());
        return false;
    }
    return true;
}

void Metrics::close() {
    if (!file_.is_open()) return;

    if (!columnsFixed_) writeHeaderAndHeldRows();
    file_.close();
}

void Metrics::set(const char* name, double value) {
    if (!name || !file_.is_open()) return;

    const auto existing = findEntry(row_, name);
    if (existing != row_.end()) {
        existing->second = value;
        return;
    }
    row_.emplace_back(name, value);
}

void Metrics::endFrame() {
    if (!file_.is_open() || row_.empty()) {
        row_.clear();
        return;
    }

    if (columnsFixed_) {
        writeRow(row_);
        row_.clear();
        return;
    }

    for (const auto& [name, value] : row_)
        if (std::ranges::none_of(columns_, [n = name](const char* c) {
                return same(c, n);
            }))
            columns_.push_back(name);

    heldRows_.push_back(std::move(row_));
    row_.clear();

    if (heldRows_.size() >= HEADER_FRAMES) writeHeaderAndHeldRows();
}

void Metrics::writeHeaderAndHeldRows() {
    for (size_t column = 0; column < columns_.size(); ++column)
        file_ << (column == 0 ? "" : ",") << columns_[column];
    file_ << '\n';

    columnsFixed_ = true;
    for (const Row& held : heldRows_) writeRow(held);
    heldRows_.clear();
}

void Metrics::writeRow(const Row& row) {
    for (size_t column = 0; column < columns_.size(); ++column) {
        if (column != 0) file_ << ',';

        const auto entry = findEntry(row, columns_[column]);
        if (entry != row.end()) file_ << std::format("{}", entry->second);
    }
    file_ << '\n';
}
