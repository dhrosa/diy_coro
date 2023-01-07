#include <benchmark/benchmark.h>

#include "task.h"

constexpr std::int64_t kBatchSize = 100'000;

static void BM_TrivialFunction(benchmark::State& state) {
  auto task = []() -> int { return 3; };
  for (auto _ : state) {
    for (int i = 0; i < kBatchSize; ++i) {
      benchmark::DoNotOptimize(task());
    }
  }
  state.SetItemsProcessed(kBatchSize * state.iterations());
}

static void BM_TrivialTask(benchmark::State& state) {
  auto task = []() -> Task<int> { co_return 3; };
  for (auto _ : state) {
    for (int i = 0; i < kBatchSize; ++i) {
      benchmark::DoNotOptimize(task().Wait());
    }
  }
  state.SetItemsProcessed(kBatchSize * state.iterations());
}

BENCHMARK(BM_TrivialFunction);
BENCHMARK(BM_TrivialTask);
