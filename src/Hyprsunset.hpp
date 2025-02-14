#include <iostream>
#include <cmath>
#include <algorithm>
#include <sys/signal.h>
#include <wayland-client.h>
#include <vector>
#include "protocols/hyprland-ctm-control-v1.hpp"
#include "protocols/wayland.hpp"

#include "helpers/Log.hpp"

#include "InstanceLock.hpp"

#include "IPCSocket.hpp"
#include <mutex>

#include <hyprutils/math/Mat3x3.hpp>
#include <hyprutils/memory/WeakPtr.hpp>
using namespace Hyprutils::Math;
using namespace Hyprutils::Memory;
#define SP CSharedPointer
#define WP CWeakPointer

struct SOutput {
    SP<CCWlOutput> output;
    uint32_t       id = 0;
    void           applyCTM(struct SState*);
};

struct SState {
    SP<CCWlRegistry>                  pRegistry;
    SP<CCHyprlandCtmControlManagerV1> pCTMMgr;
    wl_display*                       wlDisplay = nullptr;
    std::vector<SP<SOutput>>          outputs;
    bool                              initialized = false;
    Mat3x3                            ctm;
    CInstanceLock                     instLock;
};

class CHyprsunset {
  public:
    float              MAX_GAMMA = 1.0f; // default
    float              GAMMA     = 1.0f; // default
    unsigned long long KELVIN    = 6000; // default
    bool               kelvinSet = false, identity = false;
    SState             state;
    std::mutex         m_mtTickMutex;

    int                calculateMatrix();
    int                applySettings();
    int                init();
    void               tick();

  private:
    static void commitCTMs();
};

inline std::unique_ptr<CHyprsunset> g_pHyprsunset;
