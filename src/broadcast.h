#pragma once

#include <absl/container/flat_hash_set.h>

#include <coroutine>
#include <functional>
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
  AsyncGenerator<std::reference_wrapper<const T>> Subscribe();

  Task<> Send(const T& value);

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
  Phase phase = kReceivedValue;
};

template <typename T>
struct Broadcast<T>::State {
  AsyncGenerator<T> publisher;
  IntrusiveLinkedList<Subscriber> subscribers;
  Phase desired_phase;
  std::coroutine_handle<> waiting_publisher;
  const T* value = nullptr;

  bool AllSubscribersInDesiredPhase() const {
    return std::ranges::all_of(
        subscribers, [&](Subscriber& s) { return s.phase == desired_phase; });
  }

  std::coroutine_handle<> NextUntransitionedHandle() {
    Subscriber* s = std::ranges::find_if(subscribers, [&](Subscriber& s) {
      return s.phase != desired_phase && s.waiting != nullptr;
    });
    if (s == nullptr) {
      return std::noop_coroutine();
    }
    return std::exchange(s->waiting, nullptr);
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

      bool await_ready() {
        if (subscriber == nullptr && state.subscribers.empty()) {
          // Publishing wihtout any subscribers; nothing to do.
          return true;
        }
        return state.AllSubscribersInDesiredPhase();
      }

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

  AsyncGenerator<std::reference_wrapper<const T>> Subscribe();
};

template <typename T>
Task<> Broadcast<T>::Send(const T& value) {
  state_.desired_phase = kWaitingForValue;
  co_await state_.PhaseTransitionComplete();
  state_.value = &value;
  state_.desired_phase = kReceivedValue;
  co_await state_.PhaseTransitionComplete();
}

template <typename T>
AsyncGenerator<std::reference_wrapper<const T>>
Broadcast<T>::State::Subscribe() {
  Subscriber subscriber{.state = *this};
  subscribers.push_back(subscriber);
  while (true) {
    subscriber.phase = kWaitingForValue;
    co_await PhaseTransitionComplete();
    co_yield std::cref(*value);
    subscriber.phase = kReceivedValue;
    co_await PhaseTransitionComplete();
  }
}

template <typename T>
AsyncGenerator<std::reference_wrapper<const T>> Broadcast<T>::Subscribe() {
  return state_.Subscribe();
}
