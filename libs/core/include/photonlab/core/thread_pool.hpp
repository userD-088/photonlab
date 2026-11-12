#pragma once
// Einfacher Threadpool für Simulations-Jobs (Sweeps, FDTD-Läufe, Wahrheitstabellen).
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <type_traits>
#include <vector>

namespace photonlab {

class ThreadPool {
 public:
  explicit ThreadPool(std::size_t threads = std::thread::hardware_concurrency());
  ~ThreadPool();
  ThreadPool(const ThreadPool&) = delete;
  ThreadPool& operator=(const ThreadPool&) = delete;

  template <class F>
  auto submit(F&& f) -> std::future<std::invoke_result_t<F>> {
    using R = std::invoke_result_t<F>;
    auto task = std::make_shared<std::packaged_task<R()>>(std::forward<F>(f));
    std::future<R> result = task->get_future();
    {
      std::lock_guard lock(mutex_);
      queue_.emplace([task] { (*task)(); });
    }
    cv_.notify_one();
    return result;
  }

  std::size_t size() const { return workers_.size(); }

 private:
  std::vector<std::thread> workers_;
  std::queue<std::function<void()>> queue_;
  std::mutex mutex_;
  std::condition_variable cv_;
  bool stopping_ = false;
};

}  // namespace photonlab
