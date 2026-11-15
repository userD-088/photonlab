#pragma once
// Feste Gruppe von Threads für kurze, sich oft wiederholende Rechenschritte (z. B. ein FDTD-Zeitschritt in
// Zeilenbändern). Anders als der Threadpool hält die Gruppe ihre Threads bereit: Sie warten kurz aktiv und
// legen sich erst dann schlafen, damit ein Halbschritt von wenigen Mikrosekunden nicht vom Aufwecken
// aufgefressen wird. Der aufrufende Thread rechnet als Mitglied 0 mit.
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

// Zusicherung für den Compiler, dass sich Zeiger nicht überlappen (erleichtert das Vektorisieren der
// Gitterschleifen)
#if defined(_MSC_VER)
#define PHOTONLAB_RESTRICT __restrict
#else
#define PHOTONLAB_RESTRICT __restrict__
#endif

namespace photonlab {

class StepTeam {
 public:
  // threads = Anzahl der Mitglieder einschließlich des Aufrufers (mindestens 1)
  explicit StepTeam(int threads);
  ~StepTeam();
  StepTeam(const StepTeam&) = delete;
  StepTeam& operator=(const StepTeam&) = delete;

  int size() const { return size_; }
  // Ruft body(t) für t = 0..size()-1 je genau einmal auf (t = 0 im aufrufenden Thread) und kehrt zurück,
  // wenn alle fertig sind. Die erste Ausnahme wird im Aufrufer erneut geworfen. Nicht verschachteln.
  void run(const std::function<void(int)>& body);

  // Sinnvolle Zahl von Mitgliedern für work Einheiten, wenn jedes mindestens min_work bekommen soll
  // (begrenzt durch die Kerne des Rechners).
  static int suggested_size(std::size_t work, std::size_t min_work);
  // Threads je Rechnung, wenn tasks Rechnungen gleichzeitig auf einem Pool mit pool_threads Helfern (plus
  // Aufrufer) laufen: die Kerne werden aufgeteilt; 0 = automatisch (kein Pool oder nur eine Rechnung).
  static int share_for(std::size_t tasks, std::size_t pool_threads);

 private:
  void worker(int member);

  int size_ = 1;
  std::vector<std::thread> threads_;
  std::mutex mutex_;
  std::condition_variable start_cv_, done_cv_;
  std::atomic<std::uint64_t> generation_{0};
  std::atomic<int> pending_{0};
  const std::function<void(int)>* body_ = nullptr;
  std::atomic<bool> stopping_{false};
  std::exception_ptr error_;
};

// Teilt [0, count) in parts möglichst gleiche Stücke; liefert Anfang und Ende des Stücks part.
inline void split_range(int count, int parts, int part, int* begin, int* end) {
  const long long c = count, p = parts;
  *begin = static_cast<int>(c * part / p);
  *end = static_cast<int>(c * (part + 1) / p);
}

}  // namespace photonlab
