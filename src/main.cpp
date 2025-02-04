#include <iostream>
#include "Hyprsunset.hpp"

static void printHelp() {
    Debug::log(NONE, "┣ --gamma             -g  →  Set the display gamma (default 100%)");
    Debug::log(NONE, "┣ --gamma_max             →  Set the maximum display gamma (default 100%, maximum 200%)");
    Debug::log(NONE, "┣ --temperature       -t  →  Set the temperature in K (default 6000)");
    Debug::log(NONE, "┣ --identity          -i  →  Use the identity matrix (no color change)");
    Debug::log(NONE, "┣ --help              -h  →  Print this info");
    Debug::log(NONE, "╹");
}

int main(int argc, char** argv, char** envp) {
    Debug::log(NONE, "┏ hyprsunset v{} ━━╸\n┃", HYPRSUNSET_VERSION);

    g_pHyprsunset = std::make_unique<CHyprsunset>();

    if (!g_pHyprsunset->state.instLock.isOnlyInstance) {
        Debug::log(NONE, "✖ Another instance of hyprsunset is running");
        return 1;
    }

    for (int i = 1; i < argc; ++i) {
        if (argv[i] == std::string{"-t"} || argv[i] == std::string{"--temperature"}) {
            if (i + 1 >= argc) {
                Debug::log(NONE, "✖ No temperature provided for {}", argv[i]);
                return 1;
            }

            try {
                g_pHyprsunset->KELVIN    = std::stoull(argv[i + 1]);
                g_pHyprsunset->kelvinSet = true;
            } catch (std::exception& e) {
                Debug::log(NONE, "✖ Temperature {} is not valid", argv[i + 1]);
                return 1;
            }

            ++i;
        } else if (argv[i] == std::string{"-g"} || argv[i] == std::string{"--gamma"}) {
            if (i + 1 >= argc) {
                Debug::log(NONE, "✖ No gamma provided for {}", argv[i]);
                return 1;
            }

            try {
                g_pHyprsunset->GAMMA = std::stof(argv[i + 1]) / 100;
            } catch (std::exception& e) {
                Debug::log(NONE, "✖ Gamma {} is not valid", argv[i + 1]);
                return 1;
            }

            ++i;
        } else if (argv[i] == std::string{"--gamma_max"}) {
            if (i + 1 >= argc) {
                Debug::log(NONE, "✖ No gamma provided for {}", argv[i]);
                return 1;
            }

            try {
                g_pHyprsunset->MAX_GAMMA = std::stof(argv[i + 1]) / 100;
            } catch (std::exception& e) {
                Debug::log(NONE, "✖ Maximum gamma {} is not valid", argv[i + 1]);
                return 1;
            }

            ++i;
        } else if (argv[i] == std::string{"-i"} || argv[i] == std::string{"--identity"}) {
            g_pHyprsunset->identity = true;
        } else if (argv[i] == std::string{"-h"} || argv[i] == std::string{"--help"}) {
            printHelp();
            return 0;
        } else {
            Debug::log(NONE, "✖ Argument not recognized: {}", argv[i]);
            printHelp();
            return 1;
        }
    }

    if (!g_pHyprsunset->calculateMatrix())
        return 1;
    if (!g_pHyprsunset->init())
        return 1;

    return 0;
}
