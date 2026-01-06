#include "ConfigManager.hpp"
#include <cstdlib>
#include <hyprlang.hpp>
#include <hyprutils/path/Path.hpp>
#include <string>
#include <sys/ucontext.h>
#include "helpers/Log.hpp"
#include "helpers/MiscFunctions.hpp"
#include <glob.h>
#include <cstring>
#include <filesystem>

static std::string getMainConfigPath() {
    static const auto paths = Hyprutils::Path::findConfig("hyprsunset");

    return paths.first.value_or("");
}

CConfigManager::CConfigManager(std::string configPath) :
    m_config(configPath.empty() ? getMainConfigPath().c_str() : configPath.c_str(), Hyprlang::SConfigOptions{.throwAllErrors = true, .allowMissingConfig = true}) {
    currentConfigPath = configPath.empty() ? getMainConfigPath() : configPath;
}

static Hyprlang::CParseResult handleSource(const char* c, const char* v) {
    const std::string      VALUE   = v;
    const std::string      COMMAND = c;

    const auto             RESULT = g_pConfigManager->handleSource(COMMAND, VALUE);

    Hyprlang::CParseResult result;
    if (RESULT.has_value())
        result.setError(RESULT.value().c_str());
    return result;
}

void CConfigManager::init() {
    m_config.addConfigValue("max-gamma", Hyprlang::INT{100});

    m_config.addSpecialCategory("profile", Hyprlang::SSpecialCategoryOptions{.key = nullptr, .anonymousKeyBased = true});
    m_config.addSpecialConfigValue("profile", "time", Hyprlang::STRING{"00:00"});
    m_config.addSpecialConfigValue("profile", "temperature", Hyprlang::INT{6000});
    m_config.addSpecialConfigValue("profile", "gamma", Hyprlang::FLOAT{1.0f});
    m_config.addSpecialConfigValue("profile", "identity", Hyprlang::INT{0});

    // track the file in the circular dependency chain
    const auto mainConfigPath = getMainConfigPath();
    alreadyIncludedSourceFiles.insert(std::filesystem::canonical(mainConfigPath));

    m_config.registerHandler(&::handleSource, "source", {.allowFlags = false});

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
        bool          identity;

        try {
            time        = std::any_cast<Hyprlang::STRING>(m_config.getSpecialConfigValue("profile", "time", key.c_str()));
            temperature = std::any_cast<Hyprlang::INT>(m_config.getSpecialConfigValue("profile", "temperature", key.c_str()));
            gamma       = std::any_cast<Hyprlang::FLOAT>(m_config.getSpecialConfigValue("profile", "gamma", key.c_str()));
            identity    = std::any_cast<Hyprlang::INT>(m_config.getSpecialConfigValue("profile", "identity", key.c_str()));
        } catch (const std::bad_any_cast& e) {
            RASSERT(false, "Failed to construct Profile: {}", e.what()); //
        } catch (const std::out_of_range& e) {
            RASSERT(false, "Missing property for Profile: {}", e.what()); //
        }

        size_t separator = time.find(':');

        if (separator == std::string::npos) {
            Debug::log(ERR, "Invalid time format: {}, skipping profile {}", time, key);
            continue;
        }

        int hour = 0, minute = 0;
        try {
            hour   = std::stoi(time.substr(0, separator));
            minute = std::stoi(time.substr(separator + 1).c_str());
        } catch (const std::exception& e) {
            Debug::log(ERR, "Invalid time format: {}, skipping profile {}", time, key);
            continue;
        }

        // clang-format off
        result.push_back(SSunsetProfile{
            .time = {
                .hour   = std::chrono::hours(hour),
                .minute = std::chrono::minutes(minute),
            },
            .temperature = temperature,
            .gamma       = gamma,
            .identity    = identity,
        });
        // clang-format on
    }

    return result;
}

float CConfigManager::getMaxGamma() {
    try {
        return std::any_cast<Hyprlang::INT>(m_config.getConfigValue("max-gamma")) / 100.f;
    } catch (const std::bad_any_cast& e) {
        RASSERT(false, "Failed to construct max-gamma: {}", e.what()); //
    }
}

std::optional<std::string> CConfigManager::handleSource(const std::string& command, const std::string& rawpath) {
    if (rawpath.length() < 2) {
        return "source path " + rawpath + " bogus!";
    }
    std::unique_ptr<glob_t, void (*)(glob_t*)> glob_buf{new glob_t, [](glob_t* g) { globfree(g); }};
    memset(glob_buf.get(), 0, sizeof(glob_t));

    const auto CURRENTDIR = std::filesystem::path(currentConfigPath).parent_path().string();

    if (auto r = glob(absolutePath(rawpath, CURRENTDIR).c_str(), GLOB_TILDE, nullptr, glob_buf.get()); r != 0) {
        std::string err = std::format("source= globbing error: {}", r == GLOB_NOMATCH ? "found no match" : GLOB_ABORTED ? "read error" : "out of memory");
        Debug::log(ERR, "{}", err);
        return err;
    }

    for (size_t i = 0; i < glob_buf->gl_pathc; i++) {
        const auto PATH = absolutePath(glob_buf->gl_pathv[i], CURRENTDIR);

        if (PATH.empty() || PATH == currentConfigPath) {
            Debug::log(WARN, "source= skipping invalid path");
            continue;
        }

        if (std::find(alreadyIncludedSourceFiles.begin(), alreadyIncludedSourceFiles.end(), PATH) != alreadyIncludedSourceFiles.end()) {
            Debug::log(WARN, "source= skipping already included source file {} to prevent circular dependency", PATH);
            continue;
        }

        if (!std::filesystem::is_regular_file(PATH)) {
            if (std::filesystem::exists(PATH)) {
                Debug::log(WARN, "source= skipping non-file {}", PATH);
                continue;
            }

            Debug::log(ERR, "source= file doesnt exist");
            return "source file " + PATH + " doesn't exist!";
        }

        // track the file in the circular dependency chain
        alreadyIncludedSourceFiles.insert(PATH);

        // allow for nested config parsing
        auto backupConfigPath = currentConfigPath;
        currentConfigPath     = PATH;

        m_config.parseFile(PATH.c_str());

        currentConfigPath = backupConfigPath;
    }

    return {};
}
