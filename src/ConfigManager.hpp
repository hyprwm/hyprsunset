#pragma once

#include "Hyprsunset.hpp"
#include <hyprlang.hpp>
#include <vector>
#include <set>

class CConfigManager {
  public:
    CConfigManager(std::string configPath);

    std::vector<SSunsetProfile> getSunsetProfiles();
    float                       getMaxGamma();

    void                        init();

    std::optional<std::string>  handleSource(const std::string&, const std::string&);

  private:
    Hyprlang::CConfig           m_config;

    std::string                 currentConfigPath;
    std::set<std::string>       alreadyIncludedSourceFiles;
};

inline UP<CConfigManager> g_pConfigManager;
