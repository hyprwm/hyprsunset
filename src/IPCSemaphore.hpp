#pragma once

#include <fcntl.h>
#include <semaphore.h>

class CIPCSemaphore {
  public:
    class Lock {
      public:
        explicit Lock(sem_t* semaphore);
        ~Lock() noexcept;

      private:
        sem_t* pSemaphore = nullptr;
    };

  public:
    CIPCSemaphore(const char* semName);

    ~CIPCSemaphore() noexcept;

    Lock getLock() noexcept;

  private:
    sem_t* pSemaphore = nullptr;
};
