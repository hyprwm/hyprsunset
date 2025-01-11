#pragma once

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <wait.h>
#include <fstream>

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

    std::string getLockName() {
        std::string lockname = getenv("XDG_RUNTIME_DIR");
        if (lockname.back() != '/')
            lockname += '/';
        lockname += "hypr/.hyprsunsetlockfile";

        return lockname;
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
