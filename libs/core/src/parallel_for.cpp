#include "photonlab/core/parallel_for.hpp"

#include <atomic>
#include <condition_variable>
#include <exception>
#include <memory>
#include <mutex>

namespace photonlab {
namespace {

// Gemeinsamer Zustand aller Helfer; lebt über shared_ptr, bis auch der letzte Helfer fertig ist.
struct LoopState {
  std::size_t count = 0;
  const std::function<void(std::size_t)>* body = nullptr;  // gehört dem Aufrufer
  JobControl* control = nullptr;                           // gehört dem Aufrufer
  std::atomic<std::size_t> next{0};
  std::atomic<bool> stop{false};
  std::mutex mutex;
  std::condition_variable cv;
  std::size_t in_flight = 0;  // gerade rechnende Helfer (unter mutex)
  std::size_t done = 0;       // fertige Indizes (unter mutex)
  std::exception_ptr error;

  void work() {
    {
      std::lock_guard lock(mutex);
      if (stop) return;  // zu spät gestarteter Helfer: nichts mehr anfassen
      ++in_flight;
    }
    for (;;) {
      if (control && control->cancel_requested) stop = true;
      if (stop) break;
      const std::size_t i = next.fetch_add(1);
      if (i >= count) break;
      try {
        (*body)(i);
      } catch (...) {
        std::lock_guard lock(mutex);
        if (!error) error = std::current_exception();
        stop = true;
        break;
      }
      std::lock_guard lock(mutex);
      ++done;
      if (control) control->progress = static_cast<float>(done) / static_cast<float>(count);
    }
    {
      std::lock_guard lock(mutex);
      --in_flight;
    }
    cv.notify_all();
  }
};

}  // namespace

bool parallel_for(std::size_t count, const std::function<void(std::size_t)>& body, ThreadPool* pool,
                  JobControl* control) {
  auto state = std::make_shared<LoopState>();
  state->count = count;
  state->body = &body;
  state->control = control;
  if (control) control->progress = 0.0f;

  if (pool)
    for (std::size_t i = 0; i < pool->size(); ++i) pool->submit([state] { state->work(); });
  state->work();

  {
    // Nach diesem Block startet kein Helfer mehr, und keiner rechnet noch.
    std::unique_lock lock(state->mutex);
    state->stop = true;
    state->cv.wait(lock, [&] { return state->in_flight == 0; });
  }
  // Ab hier berührt kein Helfer mehr body oder control (beides gehört dem Aufrufer).
  if (state->error) std::rethrow_exception(state->error);
  if (control && control->cancel_requested) return false;
  if (control) control->progress = 1.0f;
  return true;
}

}  // namespace photonlab
