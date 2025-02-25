#pragma once

#include <string>
#include <vector>
#include <memory>
#include <wayland-server-core.h>
#include <hyprutils/memory/WeakPtr.hpp>

using namespace Hyprutils::Memory;

#ifndef SP
#define SP CSharedPointer
#endif

struct SHyprIPCEvent {
    std::string event;
    std::string data;
};

class CEventManager {
  public:
    CEventManager();
    ~CEventManager();

    void postEvent(const SHyprIPCEvent& event);

  private:
    struct SClient {
        int                          fd;
        std::vector<SP<std::string>> events;
        struct wl_event_source*      eventSource;
    };

    int                            m_iSocketFD    = -1;
    struct wl_event_source*        m_pEventSource = nullptr;
    std::vector<SClient>           m_vClients;

    static int                     onClientEvent(int fd, uint32_t mask, void* data);
    int                            onServerEvent(int fd, uint32_t mask);
    std::string                    formatEvent(const SHyprIPCEvent& event) const;
    std::vector<SClient>::iterator findClientByFD(int fd);
    std::vector<SClient>::iterator removeClientByFD(int fd);
};

inline std::unique_ptr<CEventManager> g_pEventManager;
