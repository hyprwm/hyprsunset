#include "Hyprsunset.hpp"

// kindly borrowed from https://tannerhelland.com/2012/09/18/convert-temperature-rgb-algorithm-code.html
static Mat3x3 matrixForKelvin(unsigned long long temp) {
    float r = 1.F, g = 1.F, b = 1.F;

    temp /= 100;

    if (temp <= 66) {
        r = 255;
        g = std::clamp(99.4708025861 * std::log(temp) - 161.1195681661, 0.0, 255.0);
        if (temp <= 19)
            b = 0;
        else
            b = std::clamp(std::log(temp - 10) * 138.5177312231 - 305.0447927307, 0.0, 255.0);
    } else {
        r = std::clamp(329.698727446 * (std::pow(temp - 60, -0.1332047592)), 0.0, 255.0);
        g = std::clamp(288.1221695283 * (std::pow(temp - 60, -0.0755148492)), 0.0, 255.0);
        b = 255;
    }

    return std::array<float, 9>{r / 255.F, 0, 0, 0, g / 255.F, 0, 0, 0, b / 255.F};
}

static void sigHandler(int sig) {
    if (g_pHyprsunset->state.pCTMMgr) // reset the CTM state...
        g_pHyprsunset->state.pCTMMgr.reset();

    Debug::log(NONE, "┣ Exiting on user interrupt\n╹");

    exit(0);
}

void SOutput::applyCTM(struct SState* state) {
    auto arr = state->ctm.getMatrix();
    state->pCTMMgr->sendSetCtmForOutput(output->resource(), wl_fixed_from_double(arr[0]), wl_fixed_from_double(arr[1]), wl_fixed_from_double(arr[2]), wl_fixed_from_double(arr[3]),
                                        wl_fixed_from_double(arr[4]), wl_fixed_from_double(arr[5]), wl_fixed_from_double(arr[6]), wl_fixed_from_double(arr[7]),
                                        wl_fixed_from_double(arr[8]));
}

void CHyprsunset::commitCTMs() {
    g_pHyprsunset->state.pCTMMgr->sendCommit();
}

int CHyprsunset::calculateMatrix() {
    if (KELVIN < 1000 || KELVIN > 20000) {
        Debug::log(NONE, "✖ Temperature invalid: {}. The temperature has to be between 1000 and 20000K", KELVIN);
        return 0;
    }

    if (GAMMA < 0 || GAMMA > MAX_GAMMA) {
        Debug::log(NONE, "✖ Gamma invalid: {}%. The gamma has to be between 0% and {}%", GAMMA * 100, MAX_GAMMA * 100);
        return 0;
    }

    if (!identity)
        Debug::log(NONE, "┣ Setting the temperature to {}K{}\n┃", KELVIN, kelvinSet ? "" : " (default)");
    else
        Debug::log(NONE, "┣ Resetting the matrix (--identity passed)\n┃", KELVIN, kelvinSet ? "" : " (default)");

    // calculate the matrix
    state.ctm = identity ? Mat3x3::identity() : matrixForKelvin(KELVIN);
    state.ctm.multiply(std::array<float, 9>{GAMMA, 0, 0, 0, GAMMA, 0, 0, 0, GAMMA});

    Debug::log(NONE, "┣ Calculated the CTM to be {}\n┃", state.ctm.toString());

    return 1;
}

int CHyprsunset::init() {
    // connect to the wayland server
    if (const auto SERVER = getenv("XDG_CURRENT_DESKTOP"); SERVER)
        Debug::log(NONE, "┣ Running on {}", SERVER);

    state.wlDisplay = wl_display_connect(nullptr);

    if (!state.wlDisplay) {
        Debug::log(NONE, "✖ Couldn't connect to a wayland compositor", KELVIN);
        return 0;
    }

    signal(SIGINT, sigHandler);
    signal(SIGTERM, sigHandler);

    state.pRegistry = makeShared<CCWlRegistry>((wl_proxy*)wl_display_get_registry(state.wlDisplay));
    state.pRegistry->setGlobal([this](CCWlRegistry* r, uint32_t name, const char* interface, uint32_t version) {
        const std::string IFACE = interface;

        if (IFACE == hyprland_ctm_control_manager_v1_interface.name) {
            auto targetVersion = std::min(version, 2u);

            Debug::log(NONE, "┣ Found hyprland-ctm-control-v1 supported with version {}, binding to v{}", version, targetVersion);
            state.pCTMMgr = makeShared<CCHyprlandCtmControlManagerV1>(
                (wl_proxy*)wl_registry_bind((wl_registry*)state.pRegistry->resource(), name, &hyprland_ctm_control_manager_v1_interface, targetVersion));

            if (targetVersion >= 2) {
                state.pCTMMgr->setBlocked([](CCHyprlandCtmControlManagerV1*) {
                    Debug::log(NONE, "✖ A CTM manager is already running on the current compositor.");
                    exit(1);
                });
            }
        } else if (IFACE == wl_output_interface.name) {
            if (std::find_if(state.outputs.begin(), state.outputs.end(), [name](const auto& el) { return el->id == name; }) != state.outputs.end())
                return;

            Debug::log(NONE, "┣ Found new output with ID {}, binding", name);
            auto o = state.outputs.emplace_back(
                makeShared<SOutput>(makeShared<CCWlOutput>((wl_proxy*)wl_registry_bind((wl_registry*)state.pRegistry->resource(), name, &wl_output_interface, 1)), name));

            if (state.initialized) {
                Debug::log(NONE, "┣ already initialized, applying CTM instantly", name);
                o->applyCTM(&state);
                commitCTMs();
            }
        }
    });

    state.pRegistry->setGlobalRemove([this](CCWlRegistry* r, uint32_t name) { std::erase_if(state.outputs, [name](const auto& e) { return e->id == name; }); });

    wl_display_roundtrip(state.wlDisplay);

    if (!state.pCTMMgr) {
        Debug::log(NONE, "✖ Compositor doesn't support hyprland-ctm-control-v1, are you running on Hyprland?", KELVIN);
        return 0;
    }

    Debug::log(NONE, "┣ Found {} outputs, applying CTMs", state.outputs.size());

    for (auto& o : state.outputs) {
        o->applyCTM(&state);
    }

    commitCTMs();

    state.initialized = true;

    g_pIPCSocket = std::make_unique<CIPCSocket>();
    g_pIPCSocket->initialize();

    while (wl_display_dispatch(state.wlDisplay) != -1) {
        std::lock_guard<std::mutex> lg(m_mtTickMutex);
        tick();
    }

    return 1;
}

void CHyprsunset::tick() {
    if (g_pIPCSocket && g_pIPCSocket->mainThreadParseRequest()) {
        // Reload
        calculateMatrix();

        for (auto& o : state.outputs) {
            o->applyCTM(&state);
        }

        commitCTMs();

        wl_display_flush(state.wlDisplay);
    }
}
