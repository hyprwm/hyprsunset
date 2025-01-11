#include <cerrno>
#include <cstring>
#include <signal.h>
#include <sys/file.h>
#include <thread>
#include <unistd.h>
#include <wait.h>

class InstanceLock {
public:
    InstanceLock() : pPid(getpid()) {
        using namespace std::chrono_literals;

        pFd = open(pLockName.c_str(), O_CREAT | O_RDWR, 0666);

        if (pFd == -1) {
            int         ern       = errno;
            const char* ernString = strerror(ern);
            Debug::log(NONE, "Failed to open instance lock {}: {}", pLockName, ernString);
            return; // it is not the only instance
        }

        constexpr int MAX_TRIES = 50;
        int           i         = 0;

        while (i < MAX_TRIES && !tryLock()) {
            i++;
            std::this_thread::sleep_for(1000ms);
        }

        if (i == MAX_TRIES) {
            Debug::log(NONE, "Failed to set instance lock {}", pLockName);
            return; // it is not the only instance
        }

        if (write(pFd, &pPid, sizeof(pPid)) == -1) {
            int         ern       = errno;
            const char* ernString = strerror(ern);
            Debug::log(NONE, "Failed to write current pid to lock file: {}", ernString);
        }

        // without sleep the color stays as if no filter was applied
        std::this_thread::sleep_for(100ms);
        isOnlyInstance = true;
    }

    ~InstanceLock() {
        unlock();
    }

    bool isOnlyInstance = false;

  private:
    pid_t pPid = -1;
    int   pFd  = -1;

  private:
    static constexpr const char* pLockName = "/tmp/hyprsunsetlockfile";

  private:
    bool tryLock() {
        if (flock(pFd, LOCK_EX | LOCK_NB) == -1) {
            // it will never kill old then
            if (!killOld())
                return false;
        }

        Debug::log(INFO, "Acquired lock");

        return true;
    }

    void unlock() {
        if (flock(pFd, LOCK_UN) == -1) {
            int         ern       = errno;
            const char* ernString = strerror(ern);
            Debug::log(NONE, "Failed to unlock the instance {}: {}", pLockName, ernString);
        }

        if (close(pFd) == -1) {
            int         ern       = errno;
            const char* ernString = strerror(ern);
            Debug::log(NONE, "Failed to close lock fd {}: {}", pLockName, ernString);
        }

        Debug::log(INFO, "Unlocked");
    }

    bool killOld() {
        pid_t oldPid = getOldPid();
        Debug::log(INFO, "oldPid: {}", oldPid);
        if (oldPid == -1)
            return false;

        if (kill(oldPid, SIGTERM) == -1) {
            int         ern       = errno;
            const char* ernString = strerror(ern);
            Debug::log(NONE, "Failed to to kill the other running instance: {}", ernString);

            return false;
        }

        waitpid(oldPid, nullptr, 0);

        Debug::log(INFO, "Killed: {}", oldPid);

        return true;
    }

    pid_t getOldPid() {
        pid_t oldPid = 0;
        if (read(pFd, &oldPid, sizeof(oldPid)) == -1) {
            int         ern       = errno;
            const char* ernString = strerror(ern);
            Debug::log(NONE, "Failed to read pid of the other running instance: {}", ernString);

            return -1;
        }

        return oldPid;
    }
};
