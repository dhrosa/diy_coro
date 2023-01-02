#include "diy/coro/executor.h"

#include <atomic>

namespace {

struct State {
  // Handle to the coroutine waiting to be scheduled.
  void* pending;
  // 0 under normal operation, 1 when a stop has been requested.
  std::uint64_t stop_requested;
};
}  // namespace

struct SerialExecutor::SharedState {
  std::atomic<State> state;

  // Set by the producer after sending a pending waiter, and cleared by the
  // consumer after consuming the pending waiter. This is needed to release the
  // writes up to coroutine suspension to the consumer thread. For some reason
  // `state` by itself does not produce a sufficient barrier.
  std::atomic_flag pending_in_flight;

  SharedState() {
    assert(state.is_lock_free());
    state.store({.pending = nullptr, .stop_requested = 0},
                std::memory_order::relaxed);
  }

  void AwaitSuspend(std::coroutine_handle<> pending) {
    State previous_state = state.load(std::memory_order::relaxed);
    pending_in_flight.clear(std::memory_order::relaxed);
    if (previous_state.stop_requested) {
      return;
    }
    assert(previous_state.pending != nullptr);
    if (state.compare_exchange_strong(
            previous_state, {.pending = pending.address(), .stop_requested = 0},
            std::memory_order::acq_rel)) {
      state.notify_one();
      pending_in_flight.test_and_set(std::memory_order::release);
      pending_in_flight.notify_one();
      return;
    }
    // Raced with a stop request.
    assert(previous_state.stop_requested);
  }

  void Run(std::stop_token stop_token) {
    const auto on_stop = std::stop_callback(stop_token, [this] {
      state.store({.pending = nullptr, .stop_requested = 1},
                  std::memory_order::release);
      state.notify_one();
    });

    while (true) {
      State previous_state = state.load(std::memory_order::relaxed);
      if (previous_state.stop_requested) {
        return;
      }
      if (previous_state.pending == nullptr) {
        state.wait(previous_state, std::memory_order::relaxed);
        continue;
      }
      if (state.compare_exchange_strong(
              previous_state, {.pending = nullptr, .stop_requested = 0},
              std::memory_order::acq_rel)) {
        pending_in_flight.wait(false, std::memory_order::acquire);
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
  // This await might unblock some event that causes us to be destructed. So the
  // destruction may race with our access to state_.
  std::shared_ptr<SharedState> state = state_;
  state->AwaitSuspend(pending);
}
