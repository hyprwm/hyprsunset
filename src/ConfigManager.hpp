#pragma once

#include "Hyprsunset.hpp"
#include <hyprlang.hpp>
#include <vector>

class CConfigManager {
  public:
    CConfigManager(std::string configPath);

    std::vector<SSunsetProfile> getSunsetProfiles();

    void                        init();

  private:
    Hyprlang::CConfig m_config;

    std::string       currentConfigPath;
};

inline UP<CConfigManager> g_pConfigManager;
