#pragma once

#include <absl/log/log.h>
#include <absl/synchronization/mutex.h>

#include <atomic>
#include <coroutine>
#include <iterator>
#include <list>
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
  std::atomic_int phase;
  std::coroutine_handle<> waiting;
};

template <typename T>
struct Broadcast<T>::State {
  AsyncGenerator<T> publisher;
  std::list<Subscriber> subscribers;
  const T* value = nullptr;

  enum PublisherState : int { kNeedRead, kDeliveryInProgress };

  enum SubscriberPhase : int {
    kIdle,
    kNeedValue,
    kHaveValue,
    kValueSent,
  };

  std::atomic_int publisher_state;

  std::coroutine_handle<> leader;

  State(AsyncGenerator<T> publisher) : publisher(std::move(publisher)) {
    publisher_state.store(kNeedRead, std::memory_order::relaxed);
  }

  auto WaitForValue(Subscriber& subscriber) {
    struct Awaiter {
      State& state;
      Subscriber& subscriber;

      int previous_phase;

      bool await_ready() {
        previous_phase = subscriber.phase.load(std::memory_order::acquire);
        return previous_phase == kHaveValue;
      }

      bool await_suspend(std::coroutine_handle<> handle) {
        assert(previous_phase == kIdle);
        subscriber.waiting = handle;
        if (subscriber.phase.compare_exchange_strong(
                previous_phase, kNeedValue, std::memory_order::acq_rel)) {
          // kIdle -> kNeedValue transition.
          return true;
        }
        // Racing kIdle -> kHaveValue transition. Resume immediately.
        assert(previous_phase == kHaveValue);
        return false;
      }

      void await_resume() {
        subscriber.phase.store(kHaveValue, std::memory_order::release);
      }
    };
    return Awaiter{*this, subscriber};
  }

  auto NotifyValue(Subscriber& subscriber) { return std::suspend_always(); }

  auto WaitForAllValuesSent(Subscriber& leader) {
    struct Awaiter : std::suspend_always {
      State& state;
      Subscriber& leader;

      std::coroutine_handle<> await_suspend(std::coroutine_handle<> handle) {
        for (Subscriber& subscriber : state.subscribers) {
          int previous_phase =
              subscriber.phase.load(std::memory_order::acquire);
          if (previous_phase == kIdle) {
            if (subscriber.phase.compare_exchange_strong(
                    previous_phase, kHaveValue, std::memory_order::acq_rel)) {
              // kIdle -> kHaveValue transition.
              continue;
            } else {
              // Racing kIdle -> kNeedValue transition.
              assert(previous_phase == kNeedValue);
              return std::exchange(subscriber.waiting, nullptr);
            }
          } else if (previous_phase == kNeedValue) {
            return std::exchange(subscriber.waiting, nullptr);
          }
        }
        return std::noop_coroutine();
      }
    };

    return Awaiter{.state = *this, .leader = leader};
  }

  AsyncGenerator<const T> Subscription(Subscriber& subscriber) {
    while (true) {
      int previous_state = kNeedRead;
      if (publisher_state.compare_exchange_strong(previous_state,
                                                  kDeliveryInProgress,
                                                  std::memory_order::acq_rel)) {
        // Successful kNeedRead -> kReadInProgress transition; we've won
        // election to retrieve next publisher value.
        subscriber.phase.store(kNeedValue, std::memory_order::release);
        value = co_await publisher;
        subscriber.phase.store(kHaveValue, std::memory_order::release);
      } else {
        // Another subscriber raced against us; wait for it to give us the
        // value.
        co_await WaitForValue(subscriber);
      }
      if (value) {
        co_yield *value;
      }
      co_await WaitForAllValuesSent(subscriber);
      if (value == nullptr) {
        co_return;
      }
    }
  }
};

template <typename T>
AsyncGenerator<const T> Broadcast<T>::Subscribe() {
  state_.subscribers.emplace_back();
  return state_.Subscription(state_.subscribers.back());
}
