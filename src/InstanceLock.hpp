#pragma once

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <pwd.h>
#include <ranges>
#include <sys/types.h>
#include <thread>
#include <wait.h>

#include "helpers/Log.hpp"

#include "IPCSemaphore.hpp"

class InstanceLock {
  public:
    InstanceLock() {
        static constexpr const char* SEM_NAME = "/hyprsunsetlocksemaphore";
        IPCSemaphore                 fileSem{SEM_NAME};
        auto                         fileLock = fileSem.GetLock();

        createLockFileIfNotExist();

        if (!lock()) {
            Debug::log(NONE, "✖ Failed to set instance lock {}", pLockName);
            return;
        }

        isOnlyInstance = true;
    }

    ~InstanceLock() {
        auto ids = readLockFile();
        unlock(ids);
    }

    bool isOnlyInstance = false;

  private:
    struct InstanceIdentifier {
        pid_t       pid = -1;
        std::string waylandEnv;
        InstanceIdentifier(pid_t pid, std::string&& display) : pid(pid), waylandEnv(std::move(display)) {}

        std::string toString() const {
            return std::format("{}\n{}\n", pid, waylandEnv);
        }

        bool operator==(const InstanceIdentifier& other) const {
            return pid == other.pid && waylandEnv == other.waylandEnv;
        }
    };

    using IdsVec         = std::vector<InstanceIdentifier>;
    using IdsVecIterator = IdsVec::iterator;

  private:
    std::string        pLockName{getLockFileName()};
    InstanceIdentifier pIdentifer{getInstanceIdentifier()};

  private:
    bool lock() {
        auto ids = readLockFile();

        auto sameEnvIt = findSameEnv(ids);

        if (sameEnvIt != ids.end()) {
            pid_t oldPid = sameEnvIt->pid;

            if (!killOld(oldPid))
                return false;

            ids = readLockFile();
        }

        ids.emplace_back(pIdentifer);

        commitLockFile(ids);
        return true;
    }

    void unlock(IdsVec& ids) {
        auto thisInstanceIt = findUs(ids);
        ids.erase(thisInstanceIt);

        commitLockFile(ids);
    }

    IdsVec readLockFile() {
        auto   file = getReadFile();

        IdsVec ids;
        while (file.peek() != -1) {
            pid_t       pid = 0;
            std::string display;

            file >> pid >> display;
            file.get();

            ids.emplace_back(pid, std::move(display));
        }

        return ids;
    }

    void commitLockFile(std::span<const InstanceIdentifier> ids) {

        auto file = getWriteFile();
        for (const auto& id : ids) {
            file << id.toString();
        }
    }

    static bool killOld(pid_t oldPid) {
        if (oldPid <= 0)
            return false;

        if (kill(oldPid, SIGTERM) == -1) {
            Debug::log(NONE, "✖ Failed to to kill the other running instance: {}", strerror(errno));
            return false;
        }

        while (isProcessAlive(oldPid)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        return true;
    }

    static bool isProcessAlive(pid_t pid) {
        return kill(pid, 0) == 0 || errno != ESRCH;
    }

    IdsVecIterator findSameEnv(IdsVec& ids) {
        return std::ranges::find_if(std::views::all(ids), [this](const InstanceIdentifier& id) { return id.waylandEnv == pIdentifer.waylandEnv; });
    }

    IdsVecIterator findUs(IdsVec& ids) {
        return std::ranges::find_if(std::views::all(ids), [this](const InstanceIdentifier& id) { return id == pIdentifer; });
    }

    void createLockFileIfNotExist() const {
        std::ofstream give_me_a_name{pLockName, std::fstream::out | std::fstream::app};
    }

    std::ifstream getReadFile() const {
        return std::ifstream{pLockName, std::fstream::in};
    }

    std::ofstream getWriteFile() const {
        return std::ofstream{pLockName, std::fstream::out | std::fstream::trunc};
    }

    static std::string getLockFileName() {
        return getRuntimeDir() + "/hyprsunset.lock";
    }

    // taken from Hyprland/hyprctl/main.cpp
    static unsigned getUID() {
        const auto UID   = getuid();
        const auto PWUID = getpwuid(UID);
        return PWUID ? PWUID->pw_uid : UID;
    }

    // taken  from Hyprland/hyprctl/main.cpp
    static std::string getRuntimeDir() {
        const auto XDG = getenv("XDG_RUNTIME_DIR");

        if (!XDG) {
            const auto USERID = std::to_string(getUID());
            return "/run/user/" + USERID + "/hypr";
        }

        return std::string{XDG} + "/hypr";
    }

    // returns pid and WAYLAND_DISPLAY
    static InstanceIdentifier getInstanceIdentifier() {
        int pid = getpid();
        if (pid == -1) {
            Debug::log(NONE, "✖ Failed getpid: {}", strerror(errno));
            return {-1, {}};
        }

        const char* display = getenv("WAYLAND_DISPLAY");
        if (!display) {
            Debug::log(NONE, "✖ Failed getenv(\"WAYLAND_DISPLAY\"): {}", strerror(errno));
            return {-1, {}};
        }

        return {pid, display};
    }
};
