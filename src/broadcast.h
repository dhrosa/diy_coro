#pragma once

#include <absl/log/log.h>
#include <absl/synchronization/mutex.h>

#include <atomic>
#include <coroutine>
#include <iterator>
#include <list>
#include <mutex>
#include <optional>
#include <ranges>
#include <stdexcept>

#include "diy/coro/async_generator.h"
#include "diy/coro/intrusive_linked_list.h"
#include "diy/coro/task.h"
#include "diy/coro/traits.h"

template <typename T>
class Broadcast {
 public:
  Broadcast(AsyncGenerator<T>&& publisher) : state_(std::move(publisher)) {}
  AsyncGenerator<const T> Subscribe();

 private:
  struct State;
  struct Subscriber;

  State state_;
};

template <typename T>
struct Broadcast<T>::Subscriber {
  std::coroutine_handle<> waiting;
  bool consumed_value = false;
};

template <typename T>
struct Broadcast<T>::State {
  AsyncGenerator<T> publisher;
  std::list<Subscriber> subscribers;

  std::mutex mutex;

  enum ValuePhase {
    kNeedValue,
    kReadingValue,
    kHaveValue,
  };
  ValuePhase value_phase = kNeedValue;
  Subscriber* leader = nullptr;
  const T* value;

  State(AsyncGenerator<T> publisher) : publisher(std::move(publisher)) {}

  AsyncGenerator<const T> Subscription(Subscriber& subscriber) {
    Subscriber* current_leader;
    {
      auto lock = std::lock_guard(mutex);
      if (leader == nullptr) {
        leader = &subscriber;
        value_phase = kReadingValue;
      }
      current_leader = leader;
    }
    const bool is_leader = current_leader == &subscriber;
    if (is_leader) {
      value = co_await publisher;
      auto lock = std::lock_guard(mutex);
      value_phase = kHaveValue;
    } else {
      // Wait for leader to read value.
      co_await WaitForReadComplete(subscriber);
    }
    if (value) {
      co_yield *value;
    }
    co_await WaitForAllConsumed(subscriber);
  }

  auto WaitForReadComplete(Subscriber& subscriber) {
    struct Awaiter {
      State& state;
      Subscriber& subscriber;

      bool await_ready() { return false; }

      std::coroutine_handle<> await_suspend(std::coroutine_handle<> handle) {
        auto lock = std::lock_guard(state.mutex);
        if (state.value_phase == kHaveValue) {
          return handle;
        }
        subscriber.waiting = handle;
        return std::noop_coroutine();
      }

      void await_resume() {}
    };

    return Awaiter(*this, subscriber);
  }

  auto WaitForAllConsumed(Subscriber& subscriber) {
    struct Awaiter {
      State& state;
      Subscriber& subscriber;

      bool await_ready() { return false; }

      std::coroutine_handle<> await_suspend(std::coroutine_handle<> handle) {
        auto lock = std::lock_guard(state.mutex);
        subscriber.waiting = handle;
        bool all_consumed = true;
        // Find a subscriber that is waiting to consume the new value.
        for (Subscriber& s : state.subscribers) {
          if (s.consumed_value) {
            continue;
          }
          all_consumed = false;
          if (s.waiting) {
            return std::exchange(s.waiting, nullptr);
          }
          // No waiting coroutine means the co_yield call has not yet returned
          // control back to the subscriber. Try another subscriber.
        }
        if (all_consumed) {
          return std::exchange(state.leader->waiting, nullptr);
        }
        // There's at least one subscriber whose co_yield is still in-progress.
        return std::noop_coroutine();
      }

      void await_resume() {}
    };

    return Awaiter{*this, subscriber};
  }
};

template <typename T>
AsyncGenerator<const T> Broadcast<T>::Subscribe() {
  state_.subscribers.emplace_back();
  return state_.Subscription(state_.subscribers.back());
}
