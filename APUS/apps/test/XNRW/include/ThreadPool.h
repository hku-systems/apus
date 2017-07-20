#pragma once

#include <mutex>
#include <thread>
#include <future>
#include <queue>
#include <condition_variable>

namespace XNRW {
  class ThreadPool {

    typedef std::packaged_task<void()> task_t;
    typedef std::function<void()> func_t;

  private:
    std::vector<std::thread> threads;
    std::queue<std::function<void()> > tasks;
    std::mutex mtx;
    std::condition_variable taskCond, doneCond;
    std::atomic<bool> stopFlag;
    size_t size, busy;

  public:
    ThreadPool(void);
    ThreadPool(const size_t&);

    template <class Func, class... Args>
    void addTask(Func&& func, Args&&... args) {
      auto task = std::make_shared<task_t>(
        std::bind(std::forward<Func>(func), std::forward<Args>(args)...)
      );

      {
        std::lock_guard<std::mutex> l(mtx);
        tasks.emplace([task] {(*task)(); });
      }

      taskCond.notify_one();
    }

    void wait();
    void stop();
    ~ThreadPool();
  };
}
