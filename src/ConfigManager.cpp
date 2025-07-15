#include "ConfigManager.hpp"
#include <cstdlib>
#include <hyprlang.hpp>
#include <hyprutils/path/Path.hpp>
#include <string>
#include <sys/ucontext.h>
#include "helpers/Log.hpp"

static std::string getMainConfigPath() {
    static const auto paths = Hyprutils::Path::findConfig("hyprsunset");
    if (paths.first.has_value())
        return paths.first.value();
    else
        throw std::runtime_error("Could not find config in HOME, XDG_CONFIG_HOME, XDG_CONFIG_DIRS or /etc/hypr.");
}

CConfigManager::CConfigManager(std::string configPath) :
    m_config(configPath.empty() ? getMainConfigPath().c_str() : configPath.c_str(), Hyprlang::SConfigOptions{.throwAllErrors = true, .allowMissingConfig = configPath.empty()}) {
    currentConfigPath = configPath.empty() ? getMainConfigPath() : configPath;
}

void CConfigManager::init() {
    m_config.addSpecialCategory("profile", Hyprlang::SSpecialCategoryOptions{.key = nullptr, .anonymousKeyBased = true});
    m_config.addSpecialConfigValue("profile", "time", Hyprlang::STRING{"00:00"});
    m_config.addSpecialConfigValue("profile", "temperature", Hyprlang::INT{6500});
    m_config.addSpecialConfigValue("profile", "gamma", Hyprlang::FLOAT{1.0f});

    m_config.commence();

    auto result = m_config.parse();

    if (result.error)
        Debug::log(ERR, "Config has errors:\n{}\nProceeding ignoring faulty entries", result.getError());
}

std::vector<SSunsetProfile> CConfigManager::getSunsetProfiles() {
    std::vector<SSunsetProfile> result;

    auto                        keys = m_config.listKeysForSpecialCategory("profile");
    result.reserve(keys.size());

    for (auto& key : keys) {
        std::string   time;
        unsigned long temperature;
        float         gamma;

        try {
            time        = std::any_cast<Hyprlang::STRING>(m_config.getSpecialConfigValue("profile", "time", key.c_str()));
            temperature = std::any_cast<Hyprlang::INT>(m_config.getSpecialConfigValue("profile", "temperature", key.c_str()));
            gamma       = std::any_cast<Hyprlang::FLOAT>(m_config.getSpecialConfigValue("profile", "gamma", key.c_str()));
        } catch (const std::bad_any_cast& e) {
            RASSERT(false, "Failed to construct Profile: {}", e.what()); //
        } catch (const std::out_of_range& e) {
            RASSERT(false, "Missing property for Profile: {}", e.what()); //
        }

        int separator = time.find_first_of(':');

        if (separator == -1) {
            RASSERT(false, "Invalid time format for profile {}", key);
        }

        int hour   = std::stoi(time.substr(0, separator));
        int minute = std::stoi(time.substr(separator + 1).c_str());

        // clang-format off
        result.push_back(SSunsetProfile{
            .time = {
                .hour   = std::chrono::hours(hour),
                .minute = std::chrono::minutes(minute),
            },
            .temperature = temperature,
            .gamma       = gamma,
        });
        // clang-format on
    }

    return result;
}
