#include "ConfigManager.hpp"
#include "helpers/Log.hpp"
#include "IPCSocket.hpp"
#include <cstring>
#include <mutex>
#include <thread>
#include <chrono>
#include <sys/poll.h>
#include <sys/timerfd.h>
#include <wayland-client-core.h>

#define TIMESPEC_NSEC_PER_SEC 1000000000L

static void registerSignalAction(int sig, void (*handler)(int), int sa_flags = 0) {
    struct sigaction sa;
    sa.sa_handler = handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = sa_flags;
    sigaction(sig, &sa, nullptr);
}

static void handleExitSignal(int sig) {
    Debug::log(NONE, "┣ Exiting on user interrupt\n╹");
    g_pHyprsunset->terminate();
}

static void timespecAddNs(timespec* pTimespec, int64_t delta) {
    auto delta_ns_low = delta % TIMESPEC_NSEC_PER_SEC;
    auto delta_s_high = delta / TIMESPEC_NSEC_PER_SEC;

    pTimespec->tv_sec += delta_s_high;

    pTimespec->tv_nsec += (long)delta_ns_low;
    if (pTimespec->tv_nsec >= TIMESPEC_NSEC_PER_SEC) {
        pTimespec->tv_nsec -= TIMESPEC_NSEC_PER_SEC;
        ++pTimespec->tv_sec;
    }
}

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
                makeShared<SOutput>(makeShared<CCWlOutput>((wl_proxy*)wl_registry_bind((wl_registry*)state.pRegistry->resource(), name, &wl_output_interface, 3)), name));

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

    reload();

    state.initialized = true;

    g_pIPCSocket = std::make_unique<CIPCSocket>();
    g_pIPCSocket->initialize();

    registerSignalAction(SIGTERM, ::handleExitSignal);
    registerSignalAction(SIGINT, ::handleExitSignal);

    state.timerFD = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC);

    startEventLoop();

    return 1;
}

void CHyprsunset::startEventLoop() {
    pollfd pollfds[] = {
        {
            .fd     = wl_display_get_fd(state.wlDisplay),
            .events = POLLIN,
        },
        {
            .fd     = state.timerFD,
            .events = POLLIN,
        },
    };

    std::thread pollThread([&]() {
        while (1) {
            bool preparedToRead = wl_display_prepare_read(state.wlDisplay) == 0;
            int  ret            = 0;

            if (m_bTerminate)
                break;

            if (preparedToRead) {
                ret = poll(pollfds, 2, 5000);

                if (ret < 0) {
                    RASSERT(errno == EINTR, "[core] Polling fds failed with {}", errno);
                    wl_display_cancel_read(state.wlDisplay);
                    continue;
                }

                for (size_t i = 0; i < 2; ++i) {
                    RASSERT(!(pollfds[i].revents & POLLHUP), "[core] Disconnected from pollfd id {}", i);
                }

                wl_display_read_events(state.wlDisplay);
            }

            if (ret > 0 || !preparedToRead) {
                Debug::log(TRACE, "[core] got poll event");
                std::lock_guard<std::mutex> lg(m_sEventLoopInternals.loopRequestMutex);
                m_sEventLoopInternals.shouldProcess = true;
                m_sEventLoopInternals.loopSignal.notify_one();
            }
        }
    });

    while (1) {
        std::unique_lock<std::mutex> lk(m_sEventLoopInternals.loopMutex);

        if (!m_sEventLoopInternals.shouldProcess)
            m_sEventLoopInternals.loopSignal.wait_for(lk, std::chrono::milliseconds(5000), [this] { return m_sEventLoopInternals.shouldProcess; });

        if (m_bTerminate)
            break;

        std::lock_guard<std::mutex> lg(m_sEventLoopInternals.loopRequestMutex);

        m_sEventLoopInternals.shouldProcess = false;

        if (pollfds[0].revents & POLLIN) {
            wl_display_dispatch_pending(state.wlDisplay);
            wl_display_flush(state.wlDisplay);
        }

        if (m_sEventLoopInternals.isScheduled)
            reload();
        else
            tick();

        m_sEventLoopInternals.isScheduled = false;
    }

    Debug::log(TRACE, "Exiting loop");
    m_bTerminate = true;

    // cleanup wl resources
    state.outputs.clear();
    state.pRegistry.reset();
    state.pCTMMgr.reset();

    timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    timespecAddNs(&now, 1000L * 1000L * 100L);

    itimerspec ts = {.it_value = now};

    timerfd_settime(state.timerFD, TFD_TIMER_ABSTIME, &ts, nullptr);

    pollThread.join();

    wl_display_disconnect(state.wlDisplay);
    close(state.timerFD);
}

void CHyprsunset::tick() {
    if (g_pIPCSocket && g_pIPCSocket->mainThreadParseRequest())
        reload();
}

void CHyprsunset::reload() {
    calculateMatrix();

    for (auto& o : state.outputs) {
        o->applyCTM(&state);
    }

    commitCTMs();

    wl_display_flush(state.wlDisplay);
}

void CHyprsunset::loadCurrentProfile() {
    profiles = g_pConfigManager->getSunsetProfiles();

    Debug::log(NONE, "┣ Loaded {} profiles", profiles.size());

    std::sort(profiles.begin(), profiles.end(), [](const auto& a, const auto& b) {
        if (a.time.hour < b.time.hour)
            return true;
        else if (a.time.hour > b.time.hour)
            return false;
        else
            return a.time.minute < b.time.minute;
    });

    int current = g_pHyprsunset->currentProfile();

    if (current == -1)
        return;

    SSunsetProfile profile = g_pHyprsunset->profiles[current];
    KELVIN                 = profile.temperature;
    GAMMA                  = profile.gamma;
    identity               = profile.identity;
    MAX_GAMMA              = g_pConfigManager->getMaxGamma();

    Debug::log(NONE, "┣ Applying profile from: {}:{}", profile.time.hour.count(), profile.time.minute.count());
}

int CHyprsunset::currentProfile() {
    if (profiles.empty())
        return -1;
    else if (profiles.size() == 1)
        return 0;

    auto now = std::chrono::zoned_time(std::chrono::current_zone(), std::chrono::system_clock::now()).get_local_time();

    for (size_t i = 0; i < profiles.size(); ++i) {
        const auto& p = profiles[i];

        auto        time = std::chrono::floor<std::chrono::days>(now) + p.time.hour + p.time.minute;

        if (time >= now) {
            if (i == 0)
                return profiles.size() - 1;
            return i - 1;
        }
    }

    return profiles.size() - 1;
}

void CHyprsunset::schedule() {
    std::thread([&]() {
        while (true) {
            int current = currentProfile();
            if (current == -1)
                break;

            SSunsetProfile nextProfile = (size_t)current == profiles.size() - 1 ? profiles[0] : profiles[current + 1];

            auto           now  = std::chrono::zoned_time(std::chrono::current_zone(), std::chrono::system_clock::now()).get_local_time();
            auto           time = std::chrono::floor<std::chrono::days>(now) + nextProfile.time.hour + nextProfile.time.minute;

            if (now >= time)
                time += std::chrono::days(1);

            auto system_time = std::chrono::zoned_time{std::chrono::current_zone(), time}.get_sys_time();

            std::this_thread::sleep_until(system_time);

            std::lock_guard<std::mutex> lg(m_sEventLoopInternals.loopRequestMutex);
            KELVIN   = nextProfile.temperature;
            GAMMA    = nextProfile.gamma;
            identity = nextProfile.identity;

            Debug::log(NONE, "┣ Switched to new profile from: {}:{}", nextProfile.time.hour.count(), nextProfile.time.minute.count());

            m_sEventLoopInternals.shouldProcess = true;
            m_sEventLoopInternals.isScheduled   = true;
            m_sEventLoopInternals.loopSignal.notify_all();
        };
    }).detach();
}

void CHyprsunset::terminate() {
    m_sEventLoopInternals.shouldProcess = true;
    m_sEventLoopInternals.loopSignal.notify_all();

    m_bTerminate = true;
}
