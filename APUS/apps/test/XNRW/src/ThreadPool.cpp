#include "ThreadPool.h"

XNRW::ThreadPool::ThreadPool(void) :ThreadPool(0) {}

XNRW::ThreadPool::ThreadPool(const size_t& numThreads) :stopFlag(false), size(numThreads), busy(0) {
  if (size <= 0) {
    size = std::thread::hardware_concurrency();
    size = size == 0 ? 2 : size;
  }

  for (size_t i = 0; i < size; i++)
    threads.push_back(std::thread([this] {
      std::unique_lock<std::mutex> l(this->mtx, std::defer_lock);

      while (not stopFlag.load(std::memory_order_acquire)) {
        l.lock();

        func_t currTask;

        this->taskCond.wait(l, [this] {
          return stopFlag.load(std::memory_order_acquire) or not this->tasks.empty();
        });

        if (stopFlag.load(std::memory_order_acquire))
          return;

        ++busy;
        currTask = std::move(this->tasks.front());
        tasks.pop();
        l.unlock();

        currTask();

        l.lock();
        --busy;
        l.unlock();

        doneCond.notify_one();
      }
    }));
}

void XNRW::ThreadPool::wait() {
  std::unique_lock<std::mutex> l(mtx);
  doneCond.wait(l, [this] {
    return tasks.empty() and busy == 0;
  });
}

void XNRW::ThreadPool::stop() {
  stopFlag.store(true, std::memory_order_release);
}

XNRW::ThreadPool::~ThreadPool() {
  stop();
  taskCond.notify_all();

  for (auto& th : threads)
    if (th.joinable())
      th.join();
}
