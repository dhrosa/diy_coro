#pragma once

#include <absl/log/log.h>
#include <absl/synchronization/mutex.h>

#include <atomic>
#include <coroutine>
#include <iterator>
#include <list>
#include <mutex>
#include <optional>
#include <queue>
#include <ranges>
#include <stdexcept>
#include <variant>

#include "diy/coro/async_generator.h"
#include "diy/coro/intrusive_linked_list.h"
#include "diy/coro/task.h"
#include "diy/coro/traits.h"

// Single-publisher multiple-subscriber message dispatching network.
//
// TODO(dhrosa): Exceptions raiesd by `publisher` currently work correctly for
// single subscribers, but not multiple.
template <typename T>
class Broadcast {
 public:
  // The values produced by `publisher` will be fanned out to all of the
  // subscribers.
  Broadcast(AsyncGenerator<T>&& publisher)
      : state_{.publisher = std::move(publisher)} {}

  // Registers a new subscriber with the broadcast; the values yielded by this
  // generator will be the same stream of values produced by `publisher`. All
  // call to Subscribe() must be done before awaiting on a value from any
  // previously created subscribers; i.e. subscribers can only be registered
  // around creation time.
  AsyncGenerator<const T> Subscribe();

 private:
  struct State;
  struct Subscriber;

  // Values can either be a shared reference to a value yielded by `publisher`,
  // an end-of-stream "Exhausted" signal, or an exception raised by `publisher`.
  struct Exhausted {};
  using Value =
      std::variant<std::shared_ptr<const T>, Exhausted, std::exception_ptr>;

  State state_;
};

template <typename T>
struct Broadcast<T>::Subscriber {
  // Yields to the subscriber's corresponding Subscribe() coroutine. This is set
  // once State::Subscription() starts.
  std::optional<typename AsyncGenerator<const T>::Yielder> yielder;

  // Coroutine waiting on NextValue().
  std::coroutine_handle<> waiting;

  // Values not yet consumed by this subscriber.
  std::queue<Value> values;
};

template <typename T>
struct Broadcast<T>::State {
  AsyncGenerator<T> publisher;
  // Synchronizes access to `read_in_progress` and elements of `subscribers`.
  std::mutex mutex;
  // Set when we're in the process of getting a new value from `publisher`.
  bool read_in_progress = false;
  // We use a list instead of vector for pointer-stability in Subscribe().
  std::list<Subscriber> subscribers;

  // Main loop for each subscriber.
  AsyncGenerator<const T> Subscription(Subscriber& subscriber) {
    subscriber.yielder = co_await AsyncGenerator<const T>::GetYielder();
    while (co_await SubscriptionRound(subscriber)) {
    }
  }

  enum ConsumeResult {
    // This task should fetch a new value from upstream to publish to the other
    // subscribers.
    kReadNewValue,
    // An upstream read is in-progess and this task should wait for another task
    // to complete the read.
    kWaitForNewValue,
    // No more values will arrive from upstream; generator should exit.
    kExhausted,
  };

  // Yield all pending values to the caller. Returns `true` if this subscriber
  // should fetch a new value upstream to publish to the other subscribers for
  // the next round.
  Task<ConsumeResult> ConsumeAllCurrentValues(Subscriber& subscriber) {
    while (true) {
      Value variant = Exhausted{};
      {
        auto lock = std::lock_guard(mutex);
        if (subscriber.values.empty()) {
          if (read_in_progress) {
            co_return kWaitForNewValue;
          }
          // This subscriber will fetch the value for the next round.
          read_in_progress = true;
          co_return kReadNewValue;
        }
        variant = std::move(subscriber.values.front());
        subscriber.values.pop();
      }
      if (std::holds_alternative<Exhausted>(variant)) {
        co_return kExhausted;
      } else if (auto* exception = std::get_if<std::exception_ptr>(&variant)) {
        std::rethrow_exception(*exception);
      } else if (auto* value =
                     std::get_if<std::shared_ptr<const T>>(&variant)) {
        co_await subscriber.yielder->Yield(**value);
      }
    }
  }

  // Read new value from publisher.
  Task<std::shared_ptr<const T>> ReadFromPublisher() {
    T* const value = co_await publisher;
    if (value) {
      co_return std::make_shared<const T>(std::move(*value));
    }
    co_return nullptr;
  }

  // Notify all subscribers of new upstream value.
  void PublishNewValue(std::shared_ptr<const T> value) {
    // Coroutines that were waiting for the upstream value read to complete
    // can now be awoken.
    std::vector<std::coroutine_handle<>> waiting_for_value;
    {
      auto lock = std::lock_guard(mutex);
      read_in_progress = false;
      for (Subscriber& s : subscribers) {
        s.values.push(value);
        if (s.waiting) {
          waiting_for_value.push_back(std::exchange(s.waiting, nullptr));
        }
      }
    }
    for (std::coroutine_handle<> handle : waiting_for_value) {
      handle.resume();
    }
  }

  // Single iteration of a subscription. Returns true if the generator should
  // continue afterwards, and false otherwise.
  Task<bool> SubscriptionRound(Subscriber& subscriber) {
    switch (co_await ConsumeAllCurrentValues(subscriber)) {
      case kReadNewValue: {
        // We're the designated reader for this round. Read a new value from
        // upstream.
        PublishNewValue(co_await ReadFromPublisher());
        [[fallthrough]];
      }
      case kWaitForNewValue: {
        co_await WaitForNewValue(subscriber);
        co_return true;
      }
      case kExhausted: {
        co_return false;
      }
    }
  }

  // Awaitable that waits for tje current in-progress read to finish.
  auto WaitForNewValue(Subscriber& subscriber) {
    struct Awaiter : std::suspend_always {
      State& state;
      Subscriber& subscriber;

      std::coroutine_handle<> await_suspend(std::coroutine_handle<> handle) {
        auto lock = std::lock_guard(state.mutex);
        subscriber.waiting = handle;
        // Find a subscriber to wake up that has pending values. Note that this
        // may also resume the current subscriber.
        for (Subscriber& s : state.subscribers) {
          if (!s.values.empty() && s.waiting) {
            return std::exchange(s.waiting, nullptr);
          }
        }
        // No pending subscribers; we're waiting on some external actor to
        // progress the system.
        //
        // TODO(dhrosa): Can this case actually be reached?
        return std::noop_coroutine();
      }
    };

    return Awaiter{.state = *this, .subscriber = subscriber};
  }
};

template <typename T>
AsyncGenerator<const T> Broadcast<T>::Subscribe() {
  state_.subscribers.emplace_back();
  return state_.Subscription(state_.subscribers.back());
}
