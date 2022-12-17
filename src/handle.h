#pragma once

#include <coroutine>
#include <utility>

class Handle {
 public:
  Handle() = default;
  explicit Handle(std::coroutine_handle<> handle) : handle_(handle) {}
  Handle(Handle&& other) noexcept { *this = std::move(other); }

  Handle& operator=(Handle&& other) noexcept {
    handle_ = std::exchange(other.handle_, nullptr);
    return *this;
  }

  ~Handle() {
    if (handle_) {
      handle_.destroy();
    }
  }

  template <typename P>
  decltype(auto) promise() const noexcept {
    return std::coroutine_handle<P>::from_address(handle_.address()).promise();
  }

  std::coroutine_handle<> get() const noexcept { return handle_; }

  std::coroutine_handle<>* operator->() noexcept { return &handle_; }
  const std::coroutine_handle<>* operator->() const noexcept {
    return &handle_;
  }

 private:
  std::coroutine_handle<> handle_;
};
