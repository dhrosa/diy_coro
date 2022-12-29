#pragma once

#include <iterator>
#include <ranges>

// Intrusive singly-linked list with ranges support.
template <typename T>
class IntrusiveLinkedList
    : public std::ranges::view_interface<IntrusiveLinkedList<T>> {
 public:
  class Iterator;
  using value_type = T;

  Iterator begin() const { return Iterator(head_, tail_); }

  Iterator end() const { return Iterator(nullptr, nullptr); }

  void push_back(T& item) {
    if (head_ == nullptr) {
      head_ = std::addressof(item);
      tail_ = head_;
    } else {
      tail_->next = std::addressof(item);
      tail_ = tail_->next;
    }
    ++size_;
  }

  std::size_t size() const { return size_; }

 private:
  T* head_ = nullptr;
  T* tail_ = nullptr;
  int size_ = 0;
};

template <typename T>
class IntrusiveLinkedList<T>::Iterator {
 public:
  using iterator_category = std::forward_iterator_tag;
  using value_type = T;
  using difference_type = std::ptrdiff_t;

  Iterator() = default;
  T& operator*() const { return *item_; }
  T* operator->() const { return item_; }
  operator T*() const { return item_; }

  // Pre-increment.
  Iterator& operator++() {
    item_ = item_->next;
    return *this;
  }

  // Post-increment.
  Iterator operator++(int) {
    Iterator previous = *this;
    item_ = item_->next;
    return previous;
  }

  bool operator==(Iterator other) const { return item_ == other.item_; }

 private:
  friend class IntrusiveLinkedList;
  Iterator(T* item, T* tail) : item_(item), tail_(tail) {}
  T* item_;
  T* tail_;
};

template <typename T>
inline constexpr bool
    std::ranges::enable_borrowed_range<IntrusiveLinkedList<T>> = true;
