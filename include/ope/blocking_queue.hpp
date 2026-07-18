#pragma once

#include <condition_variable>
#include <mutex>
#include <optional>
#include <queue>
#include <utility>

namespace ope {

// A simple multi-producer / multi-consumer blocking queue.
//
// Producers push work items; consumers call pop() which blocks until an item is
// available or the queue is closed and drained. Once close() is called, pop()
// keeps returning the remaining buffered items and only then reports "empty".
template <typename T>
class BlockingQueue {
 public:
  void push(T value) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      queue_.push(std::move(value));
    }
    cv_.notify_one();
  }

  // Blocks until an item can be returned. Returns std::nullopt only when the
  // queue is closed AND empty, which is the signal for a worker to stop.
  std::optional<T> pop() {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this] { return closed_ || !queue_.empty(); });
    if (queue_.empty()) {
      return std::nullopt;  // closed and drained
    }
    T value = std::move(queue_.front());
    queue_.pop();
    return value;
  }

  // Signals that no more items will be pushed. Wakes all waiting consumers.
  void close() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      closed_ = true;
    }
    cv_.notify_all();
  }

  std::size_t size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
  }

 private:
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::queue<T> queue_;
  bool closed_ = false;
};

}  // namespace ope
