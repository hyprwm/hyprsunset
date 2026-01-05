#include <cmath>
#include <sys/signal.h>
#include <wayland-client.h>
#include <vector>
#include <mutex>
#include <optional>
#include <condition_variable>
#include "protocols/hyprland-ctm-control-v1.hpp"
#include "protocols/wayland.hpp"

#include <hyprutils/math/Mat3x3.hpp>
#include <hyprutils/memory/WeakPtr.hpp>
using namespace Hyprutils::Math;
using namespace Hyprutils::Memory;
#define UP CUniquePointer
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
    int                               timerFD = -1;
};

struct SSunsetProfile {
    struct {
        std::chrono::hours   hour;
        std::chrono::minutes minute;
    } time;

    unsigned long temperature = 6000;
    float         gamma       = 1.0f;
    bool          identity    = false;
};

class CHyprsunset {
  public:
    float                         MAX_GAMMA = 1.0f; // default
    float                         GAMMA     = 1.0f; // default
    unsigned long long            KELVIN    = 6000; // default
    bool                          kelvinSet = false, identity = false;
    SState                        state;
    bool                          m_bTerminate = false;

    int                           calculateMatrix();
    int                           init();
    void                          tick();
    void                          loadCurrentProfile();
    std::optional<SSunsetProfile> getCurrentProfile();
    void                          terminate();

    struct {
        std::condition_variable loopSignal;
        std::mutex              loopMutex;
        std::mutex              loopRequestMutex;

        bool                    shouldProcess = false;
        bool                    isScheduled   = false;
    } m_sEventLoopInternals;

  private:
    static void                 commitCTMs();
    void                        reload();
    void                        schedule();
    int                         currentProfile();
    void                        startEventLoop();

    std::vector<SSunsetProfile> profiles;
};

inline std::unique_ptr<CHyprsunset> g_pHyprsunset;
