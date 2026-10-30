#pragma once

#include <atomic>
#include <string>
#include <memory>
#include <mutex>
#include <thread>

class CIPCSocket {
  public:
    ~CIPCSocket();

    void initialize();
    // Wake the listener if it is still blocked, then join it.
    void stop();

    bool mainThreadParseRequest();

  private:
    std::thread m_thread;
    std::mutex  m_mtRequestMutex;
    std::string m_szRequest = "";
    std::string m_szReply   = "";

    bool        m_bRequestReady = false;
    bool        m_bReplyReady   = false;
};

// Cleared at the start of ~CIPCSocket, before the allocation is freed.
// Exit destroys the two globals in an unspecified order, and a dead
// unique_ptr can still hold the freed address.
inline std::atomic<bool>           g_ipcSocketAlive{false};
inline std::unique_ptr<CIPCSocket> g_pIPCSocket;
