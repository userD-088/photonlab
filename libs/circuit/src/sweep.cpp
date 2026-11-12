#include "photonlab/circuit/sweep.hpp"

#include <atomic>
#include <condition_variable>
#include <exception>
#include <memory>
#include <mutex>

namespace photonlab::circuit {

std::vector<double> sweep_wavelengths(const SweepSettings& settings) {
  if (settings.points < 1) throw CircuitError("Sweep braucht mindestens einen Punkt");
  if (!(settings.wavelength_start > 0.0) || !(settings.wavelength_stop > 0.0))
    throw CircuitError("Sweep-Wellenlängen müssen positiv sein");
  std::vector<double> wavelengths(static_cast<std::size_t>(settings.points));
  for (int i = 0; i < settings.points; ++i) {
    const double f = settings.points > 1 ? static_cast<double>(i) / (settings.points - 1) : 0.0;
    wavelengths[static_cast<std::size_t>(i)] =
        settings.wavelength_start + f * (settings.wavelength_stop - settings.wavelength_start);
  }
  return wavelengths;
}

namespace {

// Gemeinsamer Zustand aller Helfer. Der Aufrufer rechnet selbst mit und wartet nur auf begonnene
// Punkte, nie auf wartende Pool-Aufgaben – so kann ein Sweep auch aus einem Pool-Thread heraus laufen.
struct SweepState {
  const CompiledCircuit* circuit = nullptr;
  JobControl* control = nullptr;
  std::vector<double> wavelengths;
  std::vector<Solution> solutions;
  std::atomic<std::size_t> next{0};
  std::atomic<bool> stop{false};
  std::mutex mutex;
  std::condition_variable cv;
  std::size_t in_flight = 0;  // gerade rechnende Helfer (unter mutex)
  std::size_t done = 0;       // fertige Punkte (unter mutex)
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
      if (i >= wavelengths.size()) break;
      try {
        solutions[i] = circuit->solve(wavelengths[i]);
      } catch (...) {
        std::lock_guard lock(mutex);
        if (!error) error = std::current_exception();
        stop = true;
        break;
      }
      std::lock_guard lock(mutex);
      ++done;
      if (control) control->progress = static_cast<float>(done) / static_cast<float>(wavelengths.size());
    }
    {
      std::lock_guard lock(mutex);
      --in_flight;
    }
    cv.notify_all();
  }
};

}  // namespace

std::vector<Solution> sweep(const CompiledCircuit& circuit, const SweepSettings& settings, ThreadPool* pool,
                            JobControl* control) {
  auto state = std::make_shared<SweepState>();
  state->circuit = &circuit;
  state->control = control;
  state->wavelengths = sweep_wavelengths(settings);
  state->solutions.resize(state->wavelengths.size());
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
  // Ab hier dürfen Helfer weder Schaltung noch JobControl berühren (beides gehört dem Aufrufer).
  if (state->error) std::rethrow_exception(state->error);
  if (control && control->cancel_requested) return {};
  if (control) control->progress = 1.0f;
  return std::move(state->solutions);
}

}  // namespace photonlab::circuit
