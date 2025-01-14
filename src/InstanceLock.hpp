#pragma once

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <ranges>
#include <thread>
#include <wait.h>

#include "helpers/Log.hpp"
#include "helpers/GetRuntimeDir.hpp"

#include "IPCSemaphore.hpp"

class CInstanceLock {
  public:
    CInstanceLock() : pLockFolder(getHyprsunsetFolder()) {
        static constexpr const char* SEM_NAME = "/hyprsunsetsemaphore";
        IPCSemaphore                 fileSem{SEM_NAME};
        auto                         fileLock = fileSem.getLock();

        if (!lock()) {
            Debug::log(NONE, "✖ Failed to set instance lock {}", pLockFolder.c_str());
            return;
        }

        isOnlyInstance = true;
    }

    ~CInstanceLock() {
        unlock();
    }

    bool isOnlyInstance = false;

  private:
    struct SInstanceIdentifier {
        pid_t       pid = -1;
        std::string waylandEnv;

        SInstanceIdentifier() = default;
        SInstanceIdentifier(pid_t pid, std::string&& display) : pid(pid), waylandEnv(std::move(display)) {}

        std::string toString() const {
            return std::format("{}\n{}\n", pid, waylandEnv);
        }

        bool operator==(const SInstanceIdentifier& other) const {
            return pid == other.pid && waylandEnv == other.waylandEnv;
        }
    };

  private:
    std::filesystem::path pLockFolder;
    SInstanceIdentifier   pIdentifer{getInstanceIdentifier()};

  private:
    bool lock() {
        auto ids = readLocks();

        auto sameEnvIt = findSameEnv(ids);

        if (sameEnvIt != ids.end()) {
            pid_t oldPid = sameEnvIt->pid;

            if (!killOld(oldPid))
                return false;
        }

        writeLock();

        return true;
    }

    void unlock() {
        std::filesystem::remove(getLockFile(pIdentifer.pid));
    }

    std::vector<SInstanceIdentifier> readLocks() {
        std::vector<SInstanceIdentifier> ids;

        for (const auto& file : std::filesystem::recursive_directory_iterator(pLockFolder)) {
            ids.emplace_back(readFile(file).value_or(SInstanceIdentifier{}));
        }

        return ids;
    }

    void writeLock() {
        std::ofstream f{getLockFile(pIdentifer.pid), std::fstream::out | std::fstream::trunc};
        f << pIdentifer.toString();
    }

    std::optional<SInstanceIdentifier> readFile(const std::filesystem::directory_entry& file) {
        if (!file.is_regular_file())
            return std::nullopt;

        pid_t         pid = 0;
        std::string   waylandEnv;

        std::ifstream f{file.path(), std::fstream::in};
        f >> pid >> waylandEnv;

        return SInstanceIdentifier{pid, std::move(waylandEnv)};
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

    std::vector<SInstanceIdentifier>::iterator findSameEnv(std::vector<SInstanceIdentifier>& ids) {
        return std::ranges::find_if(std::views::all(ids), [this](const SInstanceIdentifier& id) { return id.waylandEnv == pIdentifer.waylandEnv; });
    }

    std::vector<SInstanceIdentifier>::iterator findUs(std::vector<SInstanceIdentifier>& ids) {
        return std::ranges::find_if(std::views::all(ids), [this](const SInstanceIdentifier& id) { return id == pIdentifer; });
    }

    std::filesystem::path getLockFile(pid_t pid) {
        return pLockFolder.string() + '/' + std::to_string(pid);
    }

    // returns pid and WAYLAND_DISPLAY
    static SInstanceIdentifier getInstanceIdentifier() {
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
