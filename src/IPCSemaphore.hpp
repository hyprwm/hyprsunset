#pragma once

#include <format>
#include <fcntl.h>
#include <semaphore.h>

#include "helpers/Log.hpp"

class IPCSemaphore {
  public:
    class Lock {
      public:
        explicit Lock(sem_t* semaphore) : pSemaphore(semaphore) {
            if (!semaphore) {
                Debug::log(ERR, "✖ Lock failed null semaphore");
                return;
            }
            sem_wait(semaphore);
        }
        ~Lock() noexcept {
            if (pSemaphore)
                sem_post(pSemaphore);
        }

      private:
        sem_t* pSemaphore = nullptr;
    };

  public:
    IPCSemaphore(const char* semName) : pSemaphore(sem_open(semName, O_CREAT, 0666)) {
        if (pSemaphore == SEM_FAILED) {
            Debug::log(ERR, "✖ Failed to open semaphore");
            pSemaphore = nullptr;
        }
    }

    ~IPCSemaphore() noexcept {
        sem_close(pSemaphore);
    }

    Lock getLock() noexcept {
        return Lock{pSemaphore};
    }

  private:
    sem_t* pSemaphore = nullptr;
};
