module;

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

export module Metrics;

/**
 * Per-frame measurements collected by name and written as one CSV row a frame.
 */
export class Metrics {
public:
    static Metrics& get();

    Metrics(const Metrics&)            = delete;
    Metrics& operator=(const Metrics&) = delete;

    /**
     * Opens a CSV of this run, named after the local date and time.
     * @param directory folder the file is created in; created when missing.
     * @return false when the file could not be opened; set() then does nothing.
     */
    [[nodiscard]] bool open(const std::filesystem::path& directory);

    /// Writes the rows still held and closes the file.
    void close();

    /**
     * Records one value of the frame being collected.
     * @param name column
     * @param value the measurement.
     * @note A repeat of a name within one frame replaces its value.
     */
    void set(const char* name, double value);

    /**
     * Ends the frame being collected and starts the next.
     */
    void endFrame();

private:
    Metrics() = default;
    ~Metrics() { close(); }

    /// One frame's measurements, in the order they were set.
    using Row = std::vector<std::pair<const char*, double>>;

    /// Frames held before the columns are fixed and rows start streaming.
    static constexpr size_t HEADER_FRAMES = 8;

    void writeHeaderAndHeldRows();
    void writeRow(const Row& row);

    std::ofstream            file_;
    Row                      row_;
    std::vector<Row>         heldRows_;
    std::vector<const char*> columns_;
    bool                     columnsFixed_ = false;
};
