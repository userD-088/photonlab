#pragma once
// Fortschritt und Abbruch für lange Rechnungen; die GUI liest nur, der Job schreibt.
#include <atomic>

namespace photonlab {

struct JobControl {
  std::atomic<float> progress{0.0f};  // 0..1
  std::atomic<bool> cancel_requested{false};
  std::atomic<bool> finished{false};
};

}  // namespace photonlab
