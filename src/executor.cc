#include "diy/coro/executor.h"

#include <atomic>

namespace {
enum : int {
  // No pending waiter.
  kIdle,
  kPending,
  kStopRequested,
};
}  // namespace

struct SerialExecutor::SharedState {
  std::atomic_int state;
  std::coroutine_handle<> pending;

  SharedState() { state.store(kIdle, std::memory_order::relaxed); }

  void AwaitSuspend(std::coroutine_handle<> handle) {
    pending = handle;
    int previous_state = state.load(std::memory_order::acquire);
    if (previous_state == kStopRequested) {
      return;
    }
    assert(previous_state == kIdle);
    // Attempt kIdle -> kPending transition.
    if (state.compare_exchange_strong(previous_state, kPending,
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
      int previous_state = state.load(std::memory_order::acquire);
      if (previous_state == kStopRequested) {
        return;
      }
      assert(previous_state == kPending);
      // Attempt kPending -> kIdle transition.
      if (state.compare_exchange_strong(previous_state, kIdle,
                                        std::memory_order::acq_rel)) {
        std::exchange(pending, nullptr).resume();
        continue;
      }
      // Racing stop request.
      assert(previous_state == kStopRequested);
      return;
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
