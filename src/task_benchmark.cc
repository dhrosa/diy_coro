#include <benchmark/benchmark.h>

#include "task.h"

static void BM_TrivialFunction(benchmark::State& state) {
  auto task = []() -> int { return 3; };
  for (auto _ : state) {
    benchmark::DoNotOptimize(task());
  }
  state.SetItemsProcessed(state.iterations());
}

static void BM_TrivialTask(benchmark::State& state) {
  auto task = []() -> Task<int> { co_return 3; };
  for (auto _ : state) {
    benchmark::DoNotOptimize(task().Wait());
  }
  state.SetItemsProcessed(state.iterations());
}

BENCHMARK(BM_TrivialFunction);
BENCHMARK(BM_TrivialTask);
