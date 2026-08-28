module;
#include "imgui.h"

#include <mutex>
#include <string>
#include <utility>

module ShaderPrint;

namespace {

/**
 * @return the payload of a validation message, or the whole thing when neither
 *         marker is present.
 */
std::string trimPrefix(const std::string& message) {
    if (const size_t printf = message.find("Shader Printf:");
        printf != std::string::npos)
        return message.substr(printf + sizeof("Shader Printf:"));

    if (const size_t bar = message.rfind("| "); bar != std::string::npos)
        return message.substr(bar + 2);

    return message;
}

} // namespace

void ShaderPrint::push(const std::string& message) {
    std::string text = trimPrefix(message);
    if (text.empty()) return;

    const std::lock_guard lock(mutex_);

    if (!lines_.empty() && lines_.back().text == text) {
        ++lines_.back().count;
        return;
    }

    lines_.push_back({std::move(text), 1});
    if (lines_.size() > MAX_LINES) lines_.pop_front();
}

void ShaderPrint::clear() {
    const std::lock_guard lock(mutex_);
    lines_.clear();
}

void ShaderPrint::drawUI() {
    ImGui::SetNextWindowSize(ImVec2(900, 400), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Shader Print")) {
        if (ImGui::Button("Clear")) clear();
        ImGui::SameLine();
        ImGui::Checkbox("Auto-scroll", &autoScroll_);
        ImGui::Separator();

        if (ImGui::BeginChild("lines")) {
            const std::lock_guard lock(mutex_);
            for (const Line& line : lines_) {
                if (line.count > 1)
                    ImGui::Text("%s  (x%llu)", line.text.c_str(),
                                static_cast<unsigned long long>(line.count));
                else
                    ImGui::TextUnformatted(line.text.c_str());
            }
            if (autoScroll_ && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
                ImGui::SetScrollHereY(1.0f);
        }
        ImGui::EndChild();
    }
    ImGui::End();
}
