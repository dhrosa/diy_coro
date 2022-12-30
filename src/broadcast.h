#pragma once

#include <absl/container/flat_hash_map.h>
#include <absl/log/log.h>
#include <absl/synchronization/mutex.h>

#include <atomic>
#include <coroutine>
#include <iterator>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <vector>

#include "diy/coro/async_generator.h"
#include "diy/coro/intrusive_linked_list.h"
#include "diy/coro/task.h"
#include "diy/coro/traits.h"

template <typename T>
class Broadcast {
 public:
  Broadcast(AsyncGenerator<T>&& publisher) : state_(std::move(publisher)) {}
  AsyncGenerator<const T> Subscribe();

  Task<> Publish(const T& value);

 private:
  struct State;
  struct Subscriber;

  enum Phase {
    kWaitingForValue,
    kReceivedValue,
  };
  State state_;
};

template <typename T>
struct Broadcast<T>::Subscriber {
  State& state;
  Subscriber* next = nullptr;
  std::coroutine_handle<> waiting;
};

template <typename T>
struct Broadcast<T>::State {
  AsyncGenerator<T> publisher;
  IntrusiveLinkedList<Subscriber> subscribers;
  Phase desired_phase;
  absl::flat_hash_map<Subscriber*, Phase> subscriber_phases;
  std::coroutine_handle<> waiting_publisher;
  const T* value = nullptr;

  bool AllSubscribersInDesiredPhase() const {
    return std::ranges::all_of(std::views::values(subscriber_phases),
                               [&](Phase p) { return p == desired_phase; });
  }

  std::coroutine_handle<> NextUntransitionedHandle() {
    for (auto& [subscriber, phase] : subscriber_phases) {
      if (phase != desired_phase && subscriber->waiting) {
        return std::exchange(subscriber->waiting, nullptr);
      }
    }
    return std::noop_coroutine();
  }

  // Awaitable thats waits for all subscribers to transition to the current
  // desired phase.
  //
  // Null if the parent coroutine is the sender, othewise the subsriber
  // corresponding to the parent coroutine.
  auto PhaseTransitionComplete(Subscriber* subscriber = nullptr) {
    struct Awaiter : std::suspend_always {
      State& state;
      Subscriber* subscriber = nullptr;

      bool await_ready() { return state.AllSubscribersInDesiredPhase(); }

      std::coroutine_handle<> await_suspend(std::coroutine_handle<> handle) {
        if (subscriber == nullptr) {
          state.waiting_publisher = handle;
        } else {
          subscriber->waiting = handle;
        }
        return state.NextUntransitionedHandle();
      }

      void await_resume() {
        if (subscriber == nullptr) {
          return;
        }
        if (state.waiting_publisher && state.AllSubscribersInDesiredPhase()) {
          // We’re the final subscriber to transition states; resume the
          // publisher.
          std::exchange(state.waiting_publisher, nullptr).resume();
        }
      }
    };
    return Awaiter{.state = *this, .subscriber = subscriber};
  }
};

template <typename T>
Task<> Broadcast<T>::Publish(const T& value) {
  state_.desired_phase = kWaitingForValue;
  co_await state_.PhaseTransitionComplete();
  state_.value = &value;
  state_.desired_phase = kReceivedValue;
  co_await state_.PhaseTransitionComplete();
}

template <typename T>
AsyncGenerator<const T> Broadcast<T>::Subscribe() {
  Subscriber subscriber{.state = state_};
  state_.subscribers.push_back(subscriber);
  while (true) {
    state_.subscriber_phases[&subscriber] = kWaitingForValue;
    co_await state_.PhaseTransitionComplete();
    co_yield *state_.value;
    state_.subscriber_phases[&subscriber] = kReceivedValue;
    co_await state_.PhaseTransitionComplete();
  }
}
