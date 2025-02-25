#include "EventManager.hpp"
#include "Hyprsunset.hpp"

#include <algorithm>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>
#include <cstring>
#include <format>
#include <pwd.h>
#include <thread>
#include <fcntl.h>
#include <cerrno>

CEventManager::CEventManager() {
    m_iSocketFD = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);

    if (m_iSocketFD < 0) {
        Debug::log(ERR, "Couldn't start the hyprsunset Socket 2. (1) Event IPC will not work.");
        return;
    }

    sockaddr_un       SERVERADDRESS = {.sun_family = AF_UNIX};

    const auto        HISenv     = getenv("HYPRLAND_INSTANCE_SIGNATURE");
    const auto        RUNTIMEdir = getenv("XDG_RUNTIME_DIR");
    const std::string USERID     = std::to_string(getpwuid(getuid())->pw_uid);

    const auto        USERDIR = RUNTIMEdir ? RUNTIMEdir + std::string{"/hypr/"} : "/run/user/" + USERID + "/hypr/";

    std::string       socketPath = HISenv ? USERDIR + std::string(HISenv) + "/.hyprsunset2.sock" : USERDIR + ".hyprsunset2.sock";

    if (!HISenv)
        mkdir(USERDIR.c_str(), S_IRWXU);

    unlink(socketPath.c_str());

    if (socketPath.length() > sizeof(SERVERADDRESS.sun_path) - 1) {
        Debug::log(ERR, "Socket2 path is too long. (2) Event IPC will not work.");
        return;
    }

    strncpy(SERVERADDRESS.sun_path, socketPath.c_str(), sizeof(SERVERADDRESS.sun_path) - 1);

    if (bind(m_iSocketFD, (sockaddr*)&SERVERADDRESS, SUN_LEN(&SERVERADDRESS)) < 0) {
        Debug::log(ERR, "Couldn't bind the hyprsunset Socket 2. (3) Event IPC will not work.");
        return;
    }

    if (listen(m_iSocketFD, 10) < 0) {
        Debug::log(ERR, "Couldn't listen on the hyprsunset Socket 2. (4) Event IPC will not work.");
        return;
    }

    Debug::log(LOG, "hyprsunset event socket started at {} (fd: {})", socketPath, m_iSocketFD);

    std::thread([this]() {
        fd_set  rfds;
        timeval tv;

        while (true) {
            FD_ZERO(&rfds);
            FD_SET(m_iSocketFD, &rfds);

            tv.tv_sec  = 1;
            tv.tv_usec = 0;

            int retval = select(m_iSocketFD + 1, &rfds, NULL, NULL, &tv);

            if (retval == -1) {
                Debug::log(ERR, "select() error in event socket");
                break;
            } else if (retval) {
                onServerEvent(m_iSocketFD, WL_EVENT_READABLE);
            }
        }
    }).detach();
}

CEventManager::~CEventManager() {
    for (const auto& client : m_vClients) {
        close(client.fd);
    }

    if (m_iSocketFD >= 0) {
        close(m_iSocketFD);
        m_iSocketFD = -1;
    }
}

int CEventManager::onClientEvent(int fd, uint32_t mask, void* data) {
    return g_pEventManager->onServerEvent(fd, mask);
}

int CEventManager::onServerEvent(int fd, uint32_t mask) {
    if (fd == m_iSocketFD && (mask & WL_EVENT_READABLE)) {
        sockaddr_in clientAddress;
        socklen_t   clientSize         = sizeof(clientAddress);
        int         ACCEPTEDCONNECTION = accept(m_iSocketFD, (sockaddr*)&clientAddress, &clientSize);

        if (ACCEPTEDCONNECTION < 0) {
            if (errno != EAGAIN) {
                Debug::log(ERR, "Socket2 failed receiving connection, errno: {}", errno);
            }
            return 0;
        }

        Debug::log(LOG, "Socket2 accepted a new client at FD {}", ACCEPTEDCONNECTION);

        int flags = fcntl(ACCEPTEDCONNECTION, F_GETFL, 0);
        fcntl(ACCEPTEDCONNECTION, F_SETFL, flags | O_NONBLOCK);

        m_vClients.emplace_back(SClient{ACCEPTEDCONNECTION, {}, nullptr});

        return 0;
    }

    if (mask & WL_EVENT_ERROR || mask & WL_EVENT_HANGUP) {
        Debug::log(LOG, "Socket2 fd {} hung up", fd);
        removeClientByFD(fd);
        return 0;
    }

    if (mask & WL_EVENT_WRITABLE) {
        const auto CLIENTIT = findClientByFD(fd);
        if (CLIENTIT == m_vClients.end())
            return 0;

        while (!CLIENTIT->events.empty()) {
            const auto& event = CLIENTIT->events.front();
            if (write(CLIENTIT->fd, event->c_str(), event->length()) < 0)
                break;

            CLIENTIT->events.erase(CLIENTIT->events.begin());
        }
    }

    return 0;
}

std::vector<CEventManager::SClient>::iterator CEventManager::findClientByFD(int fd) {
    return std::find_if(m_vClients.begin(), m_vClients.end(), [fd](const auto& client) { return client.fd == fd; });
}

std::vector<CEventManager::SClient>::iterator CEventManager::removeClientByFD(int fd) {
    const auto CLIENTIT = findClientByFD(fd);
    if (CLIENTIT != m_vClients.end()) {
        close(CLIENTIT->fd);
        return m_vClients.erase(CLIENTIT);
    }
    return m_vClients.end();
}

std::string CEventManager::formatEvent(const SHyprIPCEvent& event) const {
    std::string eventString = std::format("{}>>{}\n", event.event, event.data);
    std::replace(eventString.begin() + event.event.length() + 2, eventString.end() - 1, '\n', ' ');
    return eventString;
}

void CEventManager::postEvent(const SHyprIPCEvent& event) {
    const size_t MAX_QUEUED_EVENTS = 64;
    auto         sharedEvent       = makeShared<std::string>(formatEvent(event));

    Debug::log(LOG, "Broadcasting event: {}", event.event);

    for (auto it = m_vClients.begin(); it != m_vClients.end();) {
        if (write(it->fd, sharedEvent->c_str(), sharedEvent->length()) < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                if (it->events.size() >= MAX_QUEUED_EVENTS) {
                    Debug::log(ERR, "Socket2 fd {} overflowed event queue, removing", it->fd);
                    it = removeClientByFD(it->fd);
                    continue;
                }

                it->events.push_back(sharedEvent);
            } else {
                Debug::log(ERR, "Socket2 fd {} write error, removing: {}", it->fd, strerror(errno));
                it = removeClientByFD(it->fd);
                continue;
            }
        }
        ++it;
    }
}
