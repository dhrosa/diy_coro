#pragma once

#include <atomic>
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

class SharedHandle {
 public:
  SharedHandle() = default;
  SharedHandle(std::coroutine_handle<> handle, std::atomic_size_t* count)
      : handle_(handle), count_(count) {
    Increment();
  }

  SharedHandle& operator=(const SharedHandle& other) {
    if (&other == this) {
      return *this;
    }
    Decrement();
    handle_ = other.handle_;
    count_ = other.count_;
    count_->fetch_add(1);
    return *this;
  }

  SharedHandle& operator=(SharedHandle&& other) {
    if (&other == this) {
      return *this;
    }
    Decrement();
    handle_ = std::exchange(other.handle_, nullptr);
    count_ = std::exchange(other.count_, nullptr);
    return *this;
  }

  SharedHandle(const SharedHandle& other) {
    handle_ = other.handle_;
    count_ = other.count_;
    count_->fetch_add(1);
  }

  SharedHandle(SharedHandle&& other) {
    handle_ = std::exchange(other.handle_, nullptr);
    count_ = std::exchange(other.count_, nullptr);
  }

  ~SharedHandle() { Decrement(); }

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
  void Increment() {
    if (count_ != nullptr) {
      count_->fetch_add(1, std::memory_order::acq_rel);
    }
  }
  void Decrement() {
    if (count_ != nullptr &&
        count_->fetch_sub(1, std::memory_order::acq_rel) == 1) {
      handle_.destroy();
      count_ = nullptr;
    }
  }

  std::coroutine_handle<> handle_;
  std::atomic_size_t* count_ = nullptr;
};
