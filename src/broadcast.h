#pragma once

#include <absl/log/log.h>
#include <absl/synchronization/mutex.h>

#include <atomic>
#include <coroutine>
#include <iterator>
#include <list>
#include <mutex>
#include <queue>
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
  std::optional<typename AsyncGenerator<const T>::Yielder> yielder;
  std::coroutine_handle<> waiting;

  std::queue<std::shared_ptr<const T>> values;
};

template <typename T>
struct Broadcast<T>::State {
  AsyncGenerator<T> publisher;
  std::list<Subscriber> subscribers;

  std::mutex mutex;
  bool read_in_progress = false;

  State(AsyncGenerator<T> publisher) : publisher(std::move(publisher)) {}

  AsyncGenerator<const T> Subscription(Subscriber& subscriber) {
    subscriber.yielder = co_await AsyncGenerator<const T>::GetYielder();
    while (true) {
      co_await SubscriptionRound(subscriber);
    }
  }

  Task<bool> ConsumeAllCurrentValues(Subscriber& subscriber) {
    while (true) {
      std::shared_ptr<const T> value;
      {
        auto lock = std::lock_guard(mutex);
        if (subscriber.values.empty()) {
          if (not read_in_progress) {
            read_in_progress = true;
            co_return true;
          }
          co_return false;
        }
        value = std::move(subscriber.values.front());
        subscriber.values.pop();
      }
      if (value) {
        co_await subscriber.yielder->Yield(*value);
      } else {
        co_return false;
      }
    }
  }

  Task<> SubscriptionRound(Subscriber& subscriber) {
    const bool is_leader = co_await ConsumeAllCurrentValues(subscriber);
    if (is_leader) {
      T* const raw_value = co_await publisher;
      std::shared_ptr<const T> value =
          raw_value ? std::make_shared<const T>(std::move(*raw_value))
                    : nullptr;
      std::vector<std::coroutine_handle<>> to_wake;
      {
        auto lock = std::lock_guard(mutex);
        for (Subscriber& s : subscribers) {
          s.values.push(value);
          if (s.waiting) {
            to_wake.push_back(std::exchange(s.waiting, nullptr));
          }
        }
        read_in_progress = false;
      }
      for (std::coroutine_handle<> handle : to_wake) {
        handle.resume();
      }
    }
    co_await NextValue(subscriber);
  }

  auto NextValue(Subscriber& subscriber) {
    struct Awaiter {
      State& state;
      Subscriber& subscriber;

      bool await_ready() { return false; }

      std::coroutine_handle<> await_suspend(std::coroutine_handle<> handle) {
        auto lock = std::lock_guard(state.mutex);
        subscriber.waiting = handle;
        for (Subscriber& s : state.subscribers) {
          if (!s.values.empty() && s.waiting) {
            return std::exchange(s.waiting, nullptr);
          }
        }
        return std::noop_coroutine();
      }

      void await_resume() {}
    };

    return Awaiter(*this, subscriber);
  }
};

template <typename T>
AsyncGenerator<const T> Broadcast<T>::Subscribe() {
  state_.subscribers.emplace_back();
  return state_.Subscription(state_.subscribers.back());
}
