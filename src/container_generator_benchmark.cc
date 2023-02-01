#include <benchmark/benchmark.h>

#include "diy/coro/container_generator.h"

namespace {
std::vector<int> VectorRoutine(int n) {
  std::vector<int> values;
  for (int i = 0; i < n; ++i) {
    values.push_back(i);
  }
  return values;
}

VectorGenerator<int> VectorCoroutine(int n) {
  for (int i = 0; i < n; ++i) {
    co_yield i;
  }
}

constexpr int kVectorSize = 1'000;
}  // namespace

static void BM_VectorRoutine(benchmark::State& state) {
  for (auto _ : state) {
    benchmark::DoNotOptimize(VectorRoutine(kVectorSize));
  }
  state.SetItemsProcessed(kVectorSize * state.iterations());
}

static void BM_VectorCoroutine(benchmark::State& state) {
  for (auto _ : state) {
    benchmark::DoNotOptimize(std::vector<int>(VectorCoroutine(kVectorSize)));
  }
  state.SetItemsProcessed(kVectorSize * state.iterations());
}

BENCHMARK(BM_VectorRoutine);
BENCHMARK(BM_VectorCoroutine);
