#include "diy/coro/executor.h"

#include <atomic>

namespace {
struct State {
  void* pending;
  std::uint64_t stop_requested;
};
}  // namespace

struct SerialExecutor::SharedState {
  std::atomic<State> state;

  std::atomic_flag pending_in_flight;

  SharedState() {
    state.store({.pending = nullptr, .stop_requested = 0},
                std::memory_order::seq_cst);
  }

  void AwaitSuspend(std::coroutine_handle<> pending) {
    State previous_state = state.load(std::memory_order::seq_cst);
    if (previous_state.stop_requested) {
      return;
    }
    if (state.compare_exchange_strong(
            previous_state, {.pending = pending.address(), .stop_requested = 0},
            std::memory_order::seq_cst)) {
      state.notify_one();
      pending_in_flight.test_and_set(std::memory_order_seq_cst);
      pending_in_flight.notify_one();
      pending_in_flight.wait(true, std::memory_order_seq_cst);
      return;
    }
    // Raced with a stop request.
    assert(previous_state.stop_requested);
  }

  void Run(std::stop_token stop_token) {
    const auto on_stop = std::stop_callback(stop_token, [this] {
      state.store({.pending = nullptr, .stop_requested = 1},
                  std::memory_order::seq_cst);
      state.notify_one();
    });

    while (true) {
      State previous_state = state.load(std::memory_order::seq_cst);
      if (previous_state.stop_requested) {
        return;
      }
      if (previous_state.pending == nullptr) {
        state.wait(previous_state, std::memory_order::seq_cst);
        continue;
      }
      if (state.compare_exchange_strong(
              previous_state, {.pending = nullptr, .stop_requested = 0},
              std::memory_order::seq_cst)) {
        pending_in_flight.wait(false, std::memory_order::seq_cst);
        pending_in_flight.clear(std::memory_order::seq_cst);
        pending_in_flight.notify_one();
        std::coroutine_handle<>::from_address(previous_state.pending).resume();
        continue;
      }
      // Racing stop request.
      assert(previous_state.stop_requested);
    }
  }
};

SerialExecutor::SerialExecutor()
    : state_(new SharedState),
      thread_(
          [](std::stop_token stop_token, std::shared_ptr<SharedState> state) {
            state->Run(stop_token);
          },
          state_) {}

SerialExecutor::~SerialExecutor() {
  // Let the scheduling thread finish up asynchronously. This allows
  // SerialExecutor instances to be constructed in a coroutine frame without
  // causing a deadlock when attempting to join the thread.
  thread_.request_stop();
  thread_.detach();
}

bool SerialExecutor::AwaitReady() const {
  // If we're already running on this thread, then we don't need to actually
  // do anything.
  return std::this_thread::get_id() == thread_.get_id();
}

void SerialExecutor::AwaitSuspend(std::coroutine_handle<> pending) {
  state_->AwaitSuspend(pending);
}
