#include <benchmark/benchmark.h>

#include "async_generator.h"
#include "task.h"

constexpr int kBatchSize = 100'000;

AsyncGenerator<int> TrivialGenerator() {
  for (int i = 0; i < kBatchSize; ++i) {
    co_yield i;
  }
}

Task<> TrivialFunctionTask() {
  struct Awaitable : std::suspend_never {
    int i = 0;

    int await_resume() noexcept { return ++i; }
  };
  Awaitable awaitable;
  for (int i = 0; i < kBatchSize; ++i) {
    benchmark::DoNotOptimize(co_await awaitable);
  }
}

Task<> TrivialGeneratorTask() {
  auto gen = TrivialGenerator();
  while (auto* value = co_await gen) {
    benchmark::DoNotOptimize(*value);
  }
}

static void BM_TrivialFunction(benchmark::State& state) {
  for (auto _ : state) {
    TrivialFunctionTask().Wait();
  }
  state.SetItemsProcessed(kBatchSize * state.iterations());
}

static void BM_TrivialGenerator(benchmark::State& state) {
  for (auto _ : state) {
    TrivialGeneratorTask().Wait();
  }
  state.SetItemsProcessed(kBatchSize * state.iterations());
}

BENCHMARK(BM_TrivialFunction);
BENCHMARK(BM_TrivialGenerator);
