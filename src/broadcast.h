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

  struct BeginSendOperation;
  struct EndSendOperation;

  struct BeginReceiveOperation;
  struct EndReceiveOperation;

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
  std::coroutine_handle<> waiting_sender;
  const T* value = nullptr;

  AsyncGenerator<std::reference_wrapper<const T>> Subscribe();

  std::coroutine_handle<> NextWaitingConsumer(Phase phase) {
    Subscriber* s = std::ranges::find_if(subscribers, [&](Subscriber& s) {
      return s.phase == phase && s.waiting_consumer != nullptr;
    });
    if (s == nullptr) {
      return std::noop_coroutine();
    }
    return std::exchange(s->waiting_consumer, nullptr);
  }
};

template <typename T>
struct Broadcast<T>::Subscriber {
  State& state;
  Subscriber* next = nullptr;
  std::coroutine_handle<> waiting_consumer;
  Phase phase = kReceivedValue;
};

template <typename T>
struct Broadcast<T>::BeginSendOperation : std::suspend_always {
  State& state;

  bool await_ready() {
    return std::ranges::all_of(state.subscribers, [](Subscriber& s) {
      return s.phase == kWaitingForValue;
    });
  }

  std::coroutine_handle<> await_suspend(std::coroutine_handle<> handle) {
    state.waiting_sender = handle;
    return state.NextWaitingConsumer(kReceivedValue);
  }
};

template <typename T>
struct Broadcast<T>::EndSendOperation : std::suspend_always {
  State& state;

  bool await_ready() {
    return std::ranges::all_of(state.subscribers, [](Subscriber& s) {
      return s.phase == kReceivedValue;
    });
  }

  std::coroutine_handle<> await_suspend(std::coroutine_handle<> handle) {
    state.waiting_sender = handle;
    return state.NextWaitingConsumer(kWaitingForValue);
  }
};

template <typename T>
struct Broadcast<T>::BeginReceiveOperation : std::suspend_always {
  Subscriber& subscriber;
  State& state = subscriber.state;

  bool await_ready() {
    return std::ranges::all_of(state.subscribers, [](Subscriber& s) {
      return s.phase == kWaitingForValue;
    });
  }

  std::coroutine_handle<> await_suspend(std::coroutine_handle<> handle) {
    state.waiting_sender = handle;
    return state.NextWaitingConsumer(kReceivedValue);
  }

  void await_resume() {
    if (state.waiting_sender && await_ready()) {
      state.waiting_sender.resume();
    }
  }
};

template <typename T>
struct Broadcast<T>::EndReceiveOperation : std::suspend_always {
  Subscriber& subscriber;
  State& state = subscriber.state;

  bool await_ready() {
    return std::ranges::all_of(state.subscribers, [](Subscriber& s) {
      return s.phase == kWaitingForValue;
    });
  }

  std::coroutine_handle<> await_suspend(std::coroutine_handle<> handle) {
    state.waiting_sender = handle;
    return state.NextWaitingConsumer(kWaitingForValue);
  }

  void await_resume() {
    if (state.waiting_sender && await_ready()) {
      state.waiting_sender.resume();
    }
  }
};

template <typename T>
Task<> Broadcast<T>::Send(const T& value) {
  co_await BeginSendOperation{state_};
  state_.value = &value;
  co_await EndSendOperation{state_};
}

template <typename T>
AsyncGenerator<std::reference_wrapper<const T>>
Broadcast<T>::State::Subscribe() {
  Subscriber subscriber{.state = *this};
  subscribers.push_back(subscriber);
  while (true) {
    subscriber.phase = kWaitingForValue;
    co_await BeginReceiveOperation{.subscriber = subscriber};
    co_yield std::cref(*value);
    subscriber.phase = kReceivedValue;
    co_await EndReceiveOperation{.subscriber = subscriber};
  };
  co_return;
}

template <typename T>
AsyncGenerator<std::reference_wrapper<const T>> Broadcast<T>::Subscribe() {
  return state_.Subscribe();
}
