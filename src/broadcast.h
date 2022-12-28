#pragma once

#include <absl/container/flat_hash_set.h>

#include <coroutine>
#include <functional>
#include <optional>
#include <stdexcept>
#include <vector>

#include "diy/coro/async_generator.h"
#include "diy/coro/task.h"

template <typename T>
class Broadcast {
 public:
  Broadcast(AsyncGenerator<T>&& publisher) : publisher_(std::move(publisher)) {}
  AsyncGenerator<std::reference_wrapper<const T>> Subscribe();

 private:
  struct Subscriber;

  AsyncGenerator<T> publisher_;
  absl::flat_hash_set<Subscriber*> subscribers_;
};

template <typename T>
struct Broadcast<T>::Subscriber : std::suspend_always {
  Broadcast& broadcast;
  std::coroutine_handle<> waiting;

  Subscriber(Broadcast& broadcast) : broadcast(broadcast) {
    broadcast.subscribers_.insert(this);
  }

  ~Subscriber() { broadcast.subscribers_.erase(this); }
};

template <typename T>
AsyncGenerator<std::reference_wrapper<const T>> Broadcast<T>::Subscribe() {
  Subscriber subscriber{*this};
  co_return;
}
