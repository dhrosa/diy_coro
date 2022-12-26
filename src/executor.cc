#include "diy/coro/executor.h"

struct SerialExecutor::State {
  absl::Mutex mutex;
  std::coroutine_handle<> pending ABSL_GUARDED_BY(mutex);
  bool stop_requested ABSL_GUARDED_BY(mutex) = false;

  void AwaitSuspend(std::coroutine_handle<> new_pending) {
    const auto idle_or_stopping =
        [this]() ABSL_SHARED_LOCKS_REQUIRED(mutex) -> bool {
      return pending == nullptr || stop_requested;
    };

    absl::MutexLock lock(&mutex);
    mutex.Await(absl::Condition(&idle_or_stopping));
    pending = new_pending;
  }

  void Run(std::stop_token stop_token) {
    const auto on_stop = std::stop_callback(stop_token, [this] {
      absl::MutexLock lock(&mutex);
      stop_requested = true;
    });

    const auto pending_or_stopping =
        [this]() ABSL_SHARED_LOCKS_REQUIRED(mutex) -> bool {
      return pending || stop_requested;
    };
    while (true) {
      std::coroutine_handle unlocked_pending;
      {
        absl::MutexLock lock(&mutex);
        mutex.Await(absl::Condition(&pending_or_stopping));
        if (stop_requested) {
          return;
        }
        std::swap(unlocked_pending, pending);
      }
      unlocked_pending.resume();
    }
  }
};

SerialExecutor::SerialExecutor()
    : state_(new State),
      thread_([](std::stop_token stop_token,
                 std::shared_ptr<State> state) { state->Run(stop_token); },
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
