#include "InstanceLock.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <ranges>
#include <thread>

#include "helpers/Log.hpp"
#include "helpers/GetRuntimeDir.hpp"

#include "IPCSemaphore.hpp"

CInstanceLock::CInstanceLock() : pLockFolder(getHyprsunsetFolder()) {
    static constexpr const char* SEM_NAME = "/hyprsunsetsemaphore";
    CIPCSemaphore                fileSem{SEM_NAME};
    auto                         fileLock = fileSem.getLock();

    if (!lock()) {
        Debug::log(NONE, "✖ Failed to set instance lock {}", pLockFolder.c_str());
        return;
    }

    isOnlyInstance = true;
}

CInstanceLock::~CInstanceLock() {
    unlock();
}

bool CInstanceLock::lock() {
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

void CInstanceLock::unlock() {
    std::filesystem::remove(getLockFile(pIdentifer.pid));
}

void CInstanceLock::writeLock() {
    std::ofstream f{getLockFile(pIdentifer.pid), std::fstream::out | std::fstream::trunc};
    f << pIdentifer.toString();
}

std::vector<CInstanceLock::SInstanceIdentifier> CInstanceLock::readLocks() {
    std::vector<CInstanceLock::SInstanceIdentifier> ids;

    for (const auto& file : std::filesystem::recursive_directory_iterator(pLockFolder)) {
        ids.emplace_back(readFile(file).value_or(CInstanceLock::SInstanceIdentifier{}));
    }

    return ids;
}

std::optional<CInstanceLock::SInstanceIdentifier> CInstanceLock::readFile(const std::filesystem::directory_entry& file) {
    if (!file.is_regular_file())
        return std::nullopt;

    pid_t         pid = 0;
    std::string   waylandEnv;

    std::ifstream f{file.path(), std::fstream::in};
    f >> pid >> waylandEnv;

    return CInstanceLock::SInstanceIdentifier{pid, std::move(waylandEnv)};
}

bool CInstanceLock::killOld(pid_t oldPid) {
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

bool CInstanceLock::isProcessAlive(pid_t pid) {
    return kill(pid, 0) == 0 || errno != ESRCH;
}

std::vector<CInstanceLock::SInstanceIdentifier>::iterator CInstanceLock::findSameEnv(std::vector<SInstanceIdentifier>& ids) {
    return std::ranges::find_if(std::views::all(ids), [this](const CInstanceLock::SInstanceIdentifier& id) { return id.waylandEnv == pIdentifer.waylandEnv; });
}

std::vector<CInstanceLock::SInstanceIdentifier>::iterator CInstanceLock::findUs(std::vector<SInstanceIdentifier>& ids) {
    return std::ranges::find_if(std::views::all(ids), [this](const CInstanceLock::SInstanceIdentifier& id) { return id == pIdentifer; });
}

std::filesystem::path CInstanceLock::getLockFile(pid_t pid) {
    return pLockFolder.string() + '/' + std::to_string(pid);
}

CInstanceLock::CInstanceLock::SInstanceIdentifier CInstanceLock::getInstanceIdentifier() {
    int pid = getpid();
    if (pid == -1) {
        Debug::log(NONE, "✖ Failed to get proccess id. How could it happen...: {}", strerror(errno));
        return {-1, {}};
    }

    const char* display = getenv("WAYLAND_DISPLAY");
    if (!display) {
        Debug::log(NONE, "✖ Failed to get the current wayland display. Is a wayland compositor running?");
        return {-1, {}};
    }

    return {pid, display};
}

std::string CInstanceLock::CInstanceLock::SInstanceIdentifier::toString() const {
    return std::format("{}\n{}\n", pid, waylandEnv);
}

bool CInstanceLock::CInstanceLock::SInstanceIdentifier::operator==(const SInstanceIdentifier& other) const {
    return pid == other.pid && waylandEnv == other.waylandEnv;
}
