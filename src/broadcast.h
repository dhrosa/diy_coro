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
  struct PhaseTransition;

  enum Phase {
    kWaitingForValue,
    kReceivedValue,
  };
  State state_;
};

template <typename T>
struct Broadcast<T>::State {
  AsyncGenerator<T> publisher;
  IntrusiveLinkedList<Subscriber> subscribers;
  Phase desired_phase;
  std::coroutine_handle<> waiting_publisher;
  const T* value = nullptr;

  bool PhaseTransitionComplete() const {
    return std::ranges::all_of(
        subscribers, [&](Subscriber& s) { return s.phase == desired_phase; });
  }

  std::coroutine_handle<> NextUntransitionedConsumer() {
    Subscriber* s = std::ranges::find_if(subscribers, [&](Subscriber& s) {
      return s.phase != desired_phase && s.waiting != nullptr;
    });
    if (s == nullptr) {
      return std::noop_coroutine();
    }
    return std::exchange(s->waiting, nullptr);
  }

  AsyncGenerator<std::reference_wrapper<const T>> Subscribe();
};

template <typename T>
struct Broadcast<T>::Subscriber {
  State& state;
  Subscriber* next = nullptr;
  std::coroutine_handle<> waiting;
  Phase phase = kReceivedValue;
};

// Awaitable that waits for all subscribers to reach the desired state.
template <typename T>
struct Broadcast<T>::PhaseTransition : std::suspend_always {
  State& state;

  // Null if the parent coroutine is the sender, othewise the subsriber
  // corresponding to the parent coroutine.
  Subscriber* subscriber = nullptr;

  bool await_ready() { return state.PhaseTransitionComplete(); }

  std::coroutine_handle<> await_suspend(std::coroutine_handle<> handle) {
    if (subscriber == nullptr) {
      state.waiting_publisher = handle;
    } else {
      subscriber->waiting = handle;
    }
    return state.NextUntransitionedConsumer();
  }

  void await_resume() {
    if (subscriber == nullptr) {
      return;
    }
    if (state.waiting_publisher && state.PhaseTransitionComplete()) {
      // We’re the final subscriber to transition states; resume the publisher.
      std::exchange(state.waiting_publisher, nullptr).resume();
    }
  }
};

template <typename T>
Task<> Broadcast<T>::Send(const T& value) {
  PhaseTransition transition{.state = state_};
  state_.desired_subscriber_phase = kWaitingForValue;
  co_await transition;
  state_.value = &value;
  state_.desired_subscriber_phase = kReceivedValue;
  co_await transition;
}

template <typename T>
AsyncGenerator<std::reference_wrapper<const T>>
Broadcast<T>::State::Subscribe() {
  Subscriber subscriber{.state = *this};
  subscribers.push_back(subscriber);
  PhaseTransition transition{.state = *this, .subscriber = &subscriber};
  while (true) {
    subscriber.phase = kWaitingForValue;
    co_await transition;
    co_yield std::cref(*value);
    subscriber.phase = kReceivedValue;
    co_await transition;
  }
}

template <typename T>
AsyncGenerator<std::reference_wrapper<const T>> Broadcast<T>::Subscribe() {
  return state_.Subscribe();
}
