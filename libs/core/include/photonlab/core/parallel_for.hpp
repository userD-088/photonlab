#pragma once
// Parallele Schleife für Jobs: verteilt die Indizes 0..count-1 auf den Threadpool, meldet Fortschritt
// und beachtet Abbruchwünsche. Der Aufrufer rechnet selbst mit und wartet nie auf wartende
// Pool-Aufgaben – die Schleife darf deshalb auch aus einem Pool-Thread heraus gestartet werden.
#include <cstddef>
#include <functional>

#include "photonlab/core/job.hpp"
#include "photonlab/core/thread_pool.hpp"

namespace photonlab {

// Ruft body(i) für jeden Index genau einmal auf (bei Abbruch oder Ausnahme nur für einen Teil).
// pool == nullptr rechnet im aufrufenden Thread. Rückgabe: false bei Abbruch über control.
// Die erste Ausnahme aus body wird im aufrufenden Thread erneut geworfen.
bool parallel_for(std::size_t count, const std::function<void(std::size_t)>& body, ThreadPool* pool = nullptr,
                  JobControl* control = nullptr);

}  // namespace photonlab
