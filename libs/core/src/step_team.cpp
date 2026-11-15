#include "photonlab/core/step_team.hpp"

#include <algorithm>

namespace photonlab {
namespace {
// So oft prüft ein wartendes Mitglied den Auftragszähler, bevor es sich schlafen legt (einige 10 us)
constexpr int spin_checks = 20000;
}  // namespace

StepTeam::StepTeam(int threads) : size_(std::max(1, threads)) {
  for (int t = 1; t < size_; ++t) threads_.emplace_back([this, t] { worker(t); });
}

StepTeam::~StepTeam() {
  {
    std::lock_guard lock(mutex_);
    stopping_.store(true, std::memory_order_release);
    generation_.fetch_add(1, std::memory_order_release);
  }
  start_cv_.notify_all();
  for (auto& thread : threads_) thread.join();
}

int StepTeam::suggested_size(std::size_t work, std::size_t min_work) {
  const std::size_t cores = std::max(1u, std::thread::hardware_concurrency());
  const std::size_t by_work = std::max<std::size_t>(1, work / std::max<std::size_t>(1, min_work));
  return static_cast<int>(std::min(cores, by_work));
}

int StepTeam::share_for(std::size_t tasks, std::size_t pool_threads) {
  if (pool_threads == 0 || tasks <= 1) return 0;
  const std::size_t cores = std::max(1u, std::thread::hardware_concurrency());
  const std::size_t parallel = std::min(tasks, pool_threads + 1);
  return static_cast<int>(std::max<std::size_t>(1, cores / parallel));
}

void StepTeam::run(const std::function<void(int)>& body) {
  if (size_ == 1) {
    body(0);
    return;
  }
  body_ = &body;
  error_ = nullptr;
  pending_.store(size_ - 1, std::memory_order_relaxed);
  {
    std::lock_guard lock(mutex_);
    generation_.fetch_add(1, std::memory_order_release);
  }
  start_cv_.notify_all();

  std::exception_ptr own;
  try {
    body(0);
  } catch (...) {
    own = std::current_exception();
  }
  for (int s = 0; s < spin_checks && pending_.load(std::memory_order_acquire) != 0; ++s) {
  }
  if (pending_.load(std::memory_order_acquire) != 0) {
    std::unique_lock lock(mutex_);
    done_cv_.wait(lock, [&] { return pending_.load(std::memory_order_acquire) == 0; });
  }
  body_ = nullptr;
  if (own) std::rethrow_exception(own);
  if (error_) std::rethrow_exception(error_);
}

void StepTeam::worker(int member) {
  std::uint64_t seen = 0;
  for (;;) {
    std::uint64_t current = generation_.load(std::memory_order_acquire);
    for (int s = 0; s < spin_checks && current == seen; ++s)
      current = generation_.load(std::memory_order_acquire);
    if (current == seen) {
      std::unique_lock lock(mutex_);
      start_cv_.wait(lock, [&] { return generation_.load(std::memory_order_acquire) != seen; });
      current = generation_.load(std::memory_order_acquire);
    }
    seen = current;
    if (stopping_.load(std::memory_order_acquire)) return;
    try {
      (*body_)(member);
    } catch (...) {
      std::lock_guard lock(mutex_);
      if (!error_) error_ = std::current_exception();
    }
    if (pending_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
      std::lock_guard lock(mutex_);
      done_cv_.notify_one();
    }
  }
}

}  // namespace photonlab
