#include <format>

#include "helpers/Log.hpp"
#include "IPCSemaphore.hpp"

CIPCSemaphore::CIPCSemaphore(const char* semName) : pSemaphore(sem_open(semName, O_CREAT, 0666)) {
    if (pSemaphore == SEM_FAILED) {
        Debug::log(ERR, "✖ Failed to open semaphore");
        pSemaphore = nullptr;
    }
}

CIPCSemaphore::~CIPCSemaphore() noexcept {
    sem_close(pSemaphore);
}

CIPCSemaphore::Lock CIPCSemaphore::getLock() noexcept {
    return Lock{pSemaphore};
}

CIPCSemaphore::Lock::Lock(sem_t* semaphore) : pSemaphore(semaphore) {
    if (!semaphore) {
        Debug::log(ERR, "✖ Lock failed null semaphore");
        return;
    }
    sem_wait(semaphore);
}

CIPCSemaphore::Lock::~Lock() noexcept {
    if (pSemaphore)
        sem_post(pSemaphore);
}
