#pragma once

#include <absl/container/flat_hash_set.h>

#include <coroutine>
#include <functional>
#include <optional>
#include <stdexcept>

#include "diy/coro/async_generator.h"
#include "diy/coro/task.h"

template <typename T>
class Broadcast {
 public:
  AsyncGenerator<std::reference_wrapper<const T>> Subscribe();

  Task<> Publish(AsyncGenerator<T> publisher);

 private:
  struct Subscriber;
  struct DeliveryRound;

  std::optional<DeliveryRound> current_delivery_round_;
  std::coroutine_handle<> publisher_handle_;
  absl::flat_hash_set<Subscriber*> subscribers_;
};

template <typename T>
struct Broadcast<T>::DeliveryRound {
  const T* value = nullptr;
};

template <typename T>
struct Broadcast<T>::Subscriber {
  Broadcast& broadcast;
  std::coroutine_handle<> waiting;

  Subscriber(Broadcast& broadcast) : broadcast(broadcast) {
    broadcast.subscribers_.insert(this);
  }

  ~Subscriber() { broadcast.subscribers_.erase(this); }

  // If there's already an ongoing delivery, we don't need to suspend.
  bool await_ready() { return broadcast.current_delivery_round_.has_value(); }

  std::coroutine_handle<> await_suspend(std::coroutine_handle<> handle) {
    waiting = handle;
    // We're first in the current delivery round, so we need to wake the
    // producer to start a new round.
    std::coroutine_handle<> publisher_handle =
        std::exchange(broadcast.publisher_handle_, nullptr);
    if (publisher_handle == nullptr) {
      return std::noop_coroutine();
    }
    return publisher_handle;
  }

  const T* await_resume() { return broadcast.current_delivery_round_->value; }
};

template <typename T>
Task<> Broadcast<T>::Publish(AsyncGenerator<T> publisher) {
  // Waits for the first subscriber to request data.
  struct RoundStartOperation {
    Broadcast& broadcast;

    bool await_ready() { return false; }

    void await_suspend(std::coroutine_handle<> handle) {
      broadcast.publisher_handle_ = handle;
    }

    void await_resume() {}
  };
  // Wakes a subscriber to notify it of new data.
  struct SendValueOperation {
    Broadcast& broadcast;
    Subscriber* subscriber;

    bool await_ready() { return false; }

    std::coroutine_handle<> await_suspend(std::coroutine_handle<> handle) {
      broadcast.publisher_handle_ = handle;
      return std::exchange(subscriber->waiting, nullptr);
    }

    void await_resume() {}
  };
  while (true) {
    // Wait for a subscriber to want data.
    co_await RoundStartOperation{*this};

    // Fetch next element.
    const T* value = co_await publisher;
    DeliveryRound& round = current_delivery_round_.emplace(value);
    absl::flat_hash_set<Subscriber*> waiting_subscribers = subscribers_;
    while (!waiting_subscribers.empty()) {
      Subscriber* subscriber =
          *waiting_subscribers.extract(waiting_subscribers.begin());
      co_await SendValueOperation{*this, subscriber};
    }
    if (value == nullptr) {
      // Publisher is exhausted.
      co_return;
    }
  }
}

template <typename T>
AsyncGenerator<std::reference_wrapper<const T>> Broadcast<T>::Subscribe() {
  Subscriber subscriber{*this};
  while (const T* value = co_await subscriber) {
    co_yield std::cref(*value);
  }
}
