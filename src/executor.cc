#include "diy/coro/executor.h"

#include <atomic>

namespace {
// Internal state is is either a coroutine handle address, or one of the below
// special values.
enum : std::uintptr_t {
  // No pending waiter.
  kIdle = 0,
  // Run() thread is shutting down.
  kStopRequested = 1,
};
}  // namespace

struct SerialExecutor::SharedState {
  std::atomic_uintptr_t state;

  SharedState() { state.store(kIdle, std::memory_order::relaxed); }

  void AwaitSuspend(std::coroutine_handle<> pending) {
    std::uintptr_t previous_state = state.load(std::memory_order::relaxed);
    if (previous_state == kStopRequested) {
      return;
    }
    assert(previous_state == kIdle);
    // Attempt kIdle -> pending transition.
    if (state.compare_exchange_strong(
            previous_state, reinterpret_cast<std::uintptr_t>(pending.address()),
            std::memory_order::acq_rel)) {
      state.notify_one();
      return;
    }
    // Raced with a stop request.
    assert(previous_state == kStopRequested);
  }

  void Run(std::stop_token stop_token) {
    const auto on_stop = std::stop_callback(stop_token, [this] {
      state.store(kStopRequested, std::memory_order::release);
      state.notify_one();
    });
    while (true) {
      // Wait for kIdle -> ?? transition.
      state.wait(kIdle, std::memory_order::relaxed);
      std::uintptr_t previous_state = state.load(std::memory_order::relaxed);
      if (previous_state == kStopRequested) {
        return;
      }
      assert(previous_state != kIdle);
      // State contains a coroutine handle address. Attempt pending -> kIdle
      // transition.
      if (state.compare_exchange_strong(previous_state, kIdle,
                                        std::memory_order::acq_rel)) {
        std::coroutine_handle<>::from_address(
            reinterpret_cast<void*>(previous_state))
            .resume();
        continue;
      }
      // Racing stop request.
      assert(previous_state == kStopRequested);
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
