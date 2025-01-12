#pragma once

#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <pwd.h>
#include <sys/types.h>
#include <thread>
#include <wait.h>

#include "helpers/Log.hpp"

class InstanceLock {
  public:
    InstanceLock() {
        if (pFd == -1)
            return;

        constexpr int MAX_TRIES = 50;
        int           i         = 0;

        while (i < MAX_TRIES && !tryLock()) {
            i++;
        }

        if (i == MAX_TRIES) {
            Debug::log(NONE, "✖ Failed to set instance lock {}", pLockName);
            return; // it is not the only instance
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        isOnlyInstance = true;
    }

    ~InstanceLock() {
        unlock();
    }

    bool isOnlyInstance = false;

  private:
    std::string  pLockName{getLockName()};
    std::fstream pLockFile{pLockName, std::fstream::out | std::fstream::trunc};
    int          pFd{getFd()};

  private:
    int getFd() {
        if (pLockFile)
            return pLockFile.native_handle();

        Debug::log(NONE, "✖ Failed to open {}", pLockName);

        return -1;
    }

    static std::string getLockName() {
        return getRuntimeDir() + "/.hyprsunsetlockfile";
    }

    // stolen from Hyprland/hyprctl/main.cpp
    static unsigned getUID() {
        const auto UID   = getuid();
        const auto PWUID = getpwuid(UID);
        return PWUID ? PWUID->pw_uid : UID;
    }

    // stolen from Hyprland/hyprctl/main.cpp
    static std::string getRuntimeDir() {
        const auto XDG = getenv("XDG_RUNTIME_DIR");

        if (!XDG) {
            const auto USERID = std::to_string(getUID());
            return "/run/user/" + USERID + "/hypr";
        }

        return std::string{XDG} + "/hypr";
    }

    bool tryLock() {
        struct flock flstate{};
        if (fcntl(pFd, F_GETLK, &flstate) == -1) {
            Debug::log(NONE, "✖ failed to get flock state: {}", strerror(errno));
            return false;
        }

        if (flstate.l_type == F_WRLCK) {
            if (!killOld(flstate.l_pid))
                return false;
        }

        struct flock fl{};
        fl.l_type   = F_WRLCK;
        fl.l_whence = SEEK_SET;
        fl.l_pid    = -1;

        if (fcntl(pFd, F_SETLKW, &fl) == -1)
            Debug::log(NONE, "✖ set flock failed: {}", strerror(errno));

        return true;
    }

    void unlock() {
        struct flock fl{};
        fl.l_type   = F_UNLCK;
        fl.l_whence = SEEK_SET;
        fl.l_pid    = -1;

        if (fcntl(pFd, F_SETLKW, &fl) == -1)
            Debug::log(NONE, "✖ failed to unlock the instance {}: {}", pLockName, strerror(errno));
    }

    bool killOld(pid_t oldPid) {
        if (oldPid <= 0)
            return false;

        if (kill(oldPid, SIGTERM) == -1) {
            Debug::log(NONE, "✖ failed to to kill the other running instance: {}", strerror(errno));
            return false;
        }

        waitpid(oldPid, nullptr, 0);

        return true;
    }
};
