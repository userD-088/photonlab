#include <doctest/doctest.h>

#include <numeric>
#include <vector>

#include "photonlab/core/thread_pool.hpp"

TEST_CASE("Threadpool rechnet alle Jobs") {
  photonlab::ThreadPool pool(4);
  std::vector<std::future<int>> results;
  for (int i = 0; i < 100; ++i) results.push_back(pool.submit([i] { return i * i; }));
  int sum = 0;
  for (auto& r : results) sum += r.get();
  CHECK(sum == 328350);  // Summe der Quadrate 0..99
}
