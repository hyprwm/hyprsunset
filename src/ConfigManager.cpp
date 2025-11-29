#include "ConfigManager.hpp"
#include "SunCalc.hpp"
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <hyprlang.hpp>
#include <hyprutils/path/Path.hpp>
#include <limits>
#include <string>
#include <sys/ucontext.h>
#include "helpers/Log.hpp"

static std::string getMainConfigPath() {
    static const auto paths = Hyprutils::Path::findConfig("hyprsunset");

    return paths.first.value_or("");
}

CConfigManager::CConfigManager(std::string configPath) :
    m_config(configPath.empty() ? getMainConfigPath().c_str() : configPath.c_str(), Hyprlang::SConfigOptions{.throwAllErrors = true, .allowMissingConfig = true}) {
    currentConfigPath = configPath.empty() ? getMainConfigPath() : configPath;
}

void CConfigManager::init() {
    m_config.addConfigValue("max-gamma", Hyprlang::INT{100});
    m_config.addConfigValue("latitude", Hyprlang::FLOAT{std::numeric_limits<double>::quiet_NaN()});
    m_config.addConfigValue("longitude", Hyprlang::FLOAT{std::numeric_limits<double>::quiet_NaN()});

    m_config.addSpecialCategory("profile", Hyprlang::SSpecialCategoryOptions{.key = nullptr, .anonymousKeyBased = true});
    m_config.addSpecialConfigValue("profile", "time", Hyprlang::STRING{"00:00"});
    m_config.addSpecialConfigValue("profile", "temperature", Hyprlang::INT{6000});
    m_config.addSpecialConfigValue("profile", "gamma", Hyprlang::FLOAT{1.0f});
    m_config.addSpecialConfigValue("profile", "identity", Hyprlang::INT{0});

    m_config.commence();

    auto result = m_config.parse();

    if (result.error)
        Debug::log(ERR, "Config has errors:\n{}\nProceeding ignoring faulty entries", result.getError());
}

std::vector<SSunsetProfile> CConfigManager::getSunsetProfiles() {
    std::vector<SSunsetProfile> result;

    auto                        keys = m_config.listKeysForSpecialCategory("profile");
    result.reserve(keys.size());

    const double        latitude  = static_cast<double>(std::any_cast<Hyprlang::FLOAT>(m_config.getConfigValue("latitude")));
    const double        longitude = static_cast<double>(std::any_cast<Hyprlang::FLOAT>(m_config.getConfigValue("longitude")));

    bool                sunTimesCached = false;
    NSunCalc::SSunTimes cachedSunTimes{};
    auto                ensureSunTimes = [&](const std::string& key, const std::string& descriptor) -> const NSunCalc::SSunTimes& {
        if (std::isnan(latitude) || std::isnan(longitude))
            RASSERT(false, "Profile {} uses '{}' time, but latitude and longitude must be configured", key, descriptor);

        if (!sunTimesCached) {
            const auto       now              = std::chrono::system_clock::now();
            const auto*      zone             = std::chrono::current_zone();
            const auto       info             = zone->get_info(now);
            constexpr double SECONDS_PER_HOUR = 3600.0;
            const double     timezoneHours    = static_cast<double>(info.offset.count()) / SECONDS_PER_HOUR;
            std::time_t      nowT             = std::chrono::system_clock::to_time_t(now);
            std::tm          localTm{};
            localtime_r(&nowT, &localTm);
            const int                 year  = localTm.tm_year + 1900;
            const int                 month = localTm.tm_mon + 1;
            const int                 day   = localTm.tm_mday;
            const NSunCalc::SLocation location{
                               .latitude  = latitude,
                               .longitude = longitude,
                               .timezone  = timezoneHours,
            };

            NSunCalc::CSunCalculator calculator(location);
            cachedSunTimes = calculator.computeWithFallback(year, month, day);
            sunTimesCached = true;
        }

        return cachedSunTimes;
    };

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

        const bool wantsSunrise = time == "sunrise";
        const bool wantsSunset  = time == "sunset";

        if (wantsSunrise || wantsSunset) {
            const auto&  sunTimes    = ensureSunTimes(key, time);
            const double decimalHour = wantsSunrise ? sunTimes.sunrise : sunTimes.sunset;
            if (decimalHour < 0) {
                Debug::log(ERR, "Failed to compute {} time for profile {}, skipping", time, key);
                continue;
            }
            const std::string formatted = NSunCalc::CSunCalculator::formatTime(decimalHour);
            if (formatted == "--:--")
                RASSERT(false, "Computed {} time invalid for profile {}", time, key);
            time = formatted;
        }

        size_t separator = time.find(':');
        if (separator == std::string::npos)
            RASSERT(false, "Invalid time format for profile {}", key);

        int hour = 0, minute = 0;
        try {
            hour   = std::stoi(time.substr(0, separator));
            minute = std::stoi(time.substr(separator + 1).c_str());
        } catch (const std::exception& e) {
            Debug::log(ERR, "Invalid time format: {}, skipping this profile", time);
            continue;
        }

        for (const auto& existing : result) {
            if (existing.time.hour == std::chrono::hours(hour) && existing.time.minute == std::chrono::minutes(minute)) {
                Debug::log(WARN, "Profile {} has the same time {}:{} as an earlier profile; scheduling may delay switching.", key, hour, minute);
            }
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
