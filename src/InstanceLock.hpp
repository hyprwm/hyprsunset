#pragma once

#include <filesystem>
#include <string>
#include <vector>
#include <wait.h>

class CInstanceLock {
  public:
    CInstanceLock();
    ~CInstanceLock();

    bool isOnlyInstance = false;

  private:
    struct SInstanceIdentifier {
        pid_t       pid = -1;
        std::string waylandEnv;

        SInstanceIdentifier() = default;
        SInstanceIdentifier(pid_t pid, std::string&& display) : pid(pid), waylandEnv(std::move(display)) {}

        std::string toString() const;

        bool        operator==(const SInstanceIdentifier& other) const;
    };

  private:
    std::filesystem::path pLockFolder;
    SInstanceIdentifier   pIdentifer{getInstanceIdentifier()};

  private:
    bool                                       lock();
    void                                       unlock();
    std::vector<SInstanceIdentifier>           readLocks();
    void                                       writeLock();
    std::optional<SInstanceIdentifier>         readFile(const std::filesystem::directory_entry& file);
    static bool                                killOld(pid_t oldPid);
    static bool                                isProcessAlive(pid_t pid);
    std::vector<SInstanceIdentifier>::iterator findSameEnv(std::vector<SInstanceIdentifier>& ids);
    std::vector<SInstanceIdentifier>::iterator findUs(std::vector<SInstanceIdentifier>& ids);
    std::filesystem::path                      getLockFile(pid_t pid);
    static SInstanceIdentifier                 getInstanceIdentifier();
};