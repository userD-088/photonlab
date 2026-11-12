#include "photonlab/core/thread_pool.hpp"

namespace photonlab {

ThreadPool::ThreadPool(std::size_t threads) {
  if (threads == 0) threads = 1;
  workers_.reserve(threads);
  for (std::size_t i = 0; i < threads; ++i) {
    workers_.emplace_back([this] {
      for (;;) {
        std::function<void()> job;
        {
          std::unique_lock lock(mutex_);
          cv_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
          if (stopping_ && queue_.empty()) return;
          job = std::move(queue_.front());
          queue_.pop();
        }
        job();
      }
    });
  }
}

ThreadPool::~ThreadPool() {
  {
    std::lock_guard lock(mutex_);
    stopping_ = true;
  }
  cv_.notify_all();
  for (auto& w : workers_) w.join();
}

}  // namespace photonlab
