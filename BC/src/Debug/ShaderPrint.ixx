module;
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>

export module ShaderPrint;

/**
 * On screen debug message sink for shader debugPrintfEXT output.
 */
export class ShaderPrint {
public:
    ShaderPrint() = default;

    ShaderPrint(const ShaderPrint&)            = delete;
    ShaderPrint& operator=(const ShaderPrint&) = delete;

    /**
     * Trims the validation-layer prefix and stores the line. A repeat of the
     * newest line bumps its counter instead of adding a row.
     *
     * @param message one whole debug-utils message.
     */
    void push(const std::string& message);

    /// Draws the "Shader Print" window. Call inside an ImGui frame.
    void drawUI();

    void clear();

private:
    struct Line {
        std::string text;
        uint64_t    count = 1;
    };

    static constexpr size_t MAX_LINES = 256;

    std::mutex        mutex_;
    std::deque<Line>  lines_;
    bool              autoScroll_ = true;
};
