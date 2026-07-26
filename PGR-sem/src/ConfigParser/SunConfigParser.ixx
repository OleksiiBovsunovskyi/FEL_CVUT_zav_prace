module;
#include <filesystem>
#include <ranges>
#include <string>
export module SunConfigParser;
export import ConfigParser;

import Logger;

export struct SunConfig
{
    glm::vec3 color = glm::vec3(1.0f);
    glm::vec3 direction = glm::vec3(0.0f, -1.0f, 0.0f);
};

export class SunConfigParser : public ConfigParser<SunConfig>
{
    public:
        SunConfigParser(const std::filesystem::path& path) : ConfigParser(path) {}
    [[nodiscard]] std::optional<SunConfig> parseConfig() override
        {
            if (!configFileStream.is_open())
            {
                logError("Config file is not open");
                return std::nullopt;
            }

            SunConfig config;
            std::string line;
            int lineNum = 0;

            while (readConfigLine(line))
            {
                lineNum++;

                if (line.empty() || line[0] == '#')
                    continue;

                auto key = parseOptionName(line);
                if (!key)
                {
                    logWarning("Line " + std::to_string(lineNum) + ": invalid format, skipping");
                    continue;
                }

                if (*key == "Direction")
                {
                    auto vec = parseVec3(line);
                    if (!vec) { logError("Line " + std::to_string(lineNum) + ": failed to parse Direction"); return std::nullopt; }
                    config.direction = *vec;
                }
                else if (*key == "Color")
                {
                    auto vec = parseVec3(line);
                    if (!vec) { logError("Line " + std::to_string(lineNum) + ": failed to parse Color"); return std::nullopt; }
                    config.color = *vec;
                }
                else
                {
                    logWarning("Line " + std::to_string(lineNum) + ": unknown key '" + *key + "'");
                }
            }
            return config;
        }
};