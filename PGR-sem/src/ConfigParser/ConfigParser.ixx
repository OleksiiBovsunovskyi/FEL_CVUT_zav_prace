module;
#include <cctype>
#include <filesystem>
#include <fstream>
#include <string>
export module ConfigParser;

import Logger;

export enum class EConfigName
{
    WindowConfig,
    FXAAConfig,
    TonemapConfig,
    SSRConfig,
    SSAOConfig,
    AmbientLightConfig,
    SunConfig,
    ShadowConfig,
    LightSpawnConfig
};

export template <typename CONFIG_TYPE>
class ConfigParser
{
protected:
    std::filesystem::path configFilePath;
    std::ifstream configFileStream;

    /**
     * Parses a line containing 3 float values separated by spaces into a glm::vec3
     * @param line line to parse
     * @return std::optional containing the parsed glm::vec3 if successful, std::nullopt otherwise
     */
    [[nodiscard]] std::optional<glm::vec3> parseVec3(const std::string& line)
    {
        std::istringstream ss(line);
        std::vector<float> values;
        std::string token;

        while (ss >> token)
        {
            try { values.push_back(std::stof(token)); }
            catch (...) { /* skip non-float tokens */ }
        }

        if (values.size() < 3)
            return std::nullopt;

        return glm::vec3(values[0], values[1], values[2]);
    }
    
    /**
     * Parses a line to extract an option name (first word) if it starts with an alphabetic character
     * @param line line to parse
     * @return std::optional containing the extracted option name if successful, std::nullopt otherwise
     */
    [[nodiscard]] static std::optional<std::string> parseOptionName(const std::string& line)
    {
        size_t start = line.find_first_not_of(" \t");
        // std::isalpha takes an int that must be representable as unsigned char
        // (or EOF); char is signed on MSVC, so any byte > 127 would otherwise be
        // passed as a negative value and trip the UCRT debug assertion.
        if (start == std::string::npos || !std::isalpha(static_cast<unsigned char>(line[start])))
            return std::nullopt;

        size_t end = line.find_first_of(" \t", start);
        return line.substr(start, end - start);
    }

    /**
     * Reads the next line from the config file, stripping a leading UTF-8 BOM.
     * Editors on Windows frequently save .config files with a BOM; its bytes are
     * not whitespace and not '#', so without this the first line escapes the
     * comment check and its bytes are parsed as part of an option name.
     * @param line out-parameter receiving the line read
     * @return true if a line was read, false at end of file
     */
    [[nodiscard]] bool readConfigLine(std::string& line)
    {
        if (!std::getline(configFileStream, line))
            return false;

        if (line.size() >= 3
            && static_cast<unsigned char>(line[0]) == 0xEF
            && static_cast<unsigned char>(line[1]) == 0xBB
            && static_cast<unsigned char>(line[2]) == 0xBF)
            line.erase(0, 3);

        return true;
    }

    /**
     * Attempts to open the config file, checks if it exists, is a regular file and has the correct extension
     * Saves the opened file stream in configFileStream if successful
     * @return true on success, false on failure
     */
    [[nodiscard]] bool tryOpenConfigFile()
    {
        if (!std::filesystem::exists(configFilePath))
        {
            logError("ConfigParser: config file does not exist: " + configFilePath.string());
            return false;
        }
        if (!std::filesystem::is_regular_file(configFilePath))
        {
            logError("ConfigParser: config path is not a regular file: " + configFilePath.string());
            return false;
        }
        if (configFilePath.extension() != ".config")
        {
            logError("ConfigParser: config file has invalid extension: " + configFilePath.string());
            return false;
        }
        configFileStream.open(configFilePath, std::ios::in);
        if (!configFileStream.is_open())
        {
            logError("ConfigParser: failed to open config file: " + configFilePath.string());
            return false;
        }

        return true;
    }

    ~ConfigParser()
    {
        if (configFileStream.is_open())
            configFileStream.close();
    };

public:
    ConfigParser(const std::filesystem::path& path) : configFilePath(path)
    {
        if (!tryOpenConfigFile())
            pgr::dieWithError("ConfigParser was not created, failed to open config file: " + path.string());;
    }

    /**
     * Parses the config file
     * @return std::optional containing the parsed config if successful, std::nullopt otherwise
     */
    [[nodiscard]] virtual std::optional<CONFIG_TYPE> parseConfig() = 0;
};
