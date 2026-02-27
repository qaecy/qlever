#pragma once

#include <fstream>
#include <iostream>
#include <mutex>
#include <unordered_set>

namespace cli_utils {

// RAII guard that redirects std::cerr and std::clog to /dev/null for the
// duration of its lifetime and unconditionally restores them on destruction
// (even under exceptions).
//
// Thread-safety: a process-wide mutex serializes all rdbuf swaps.  A static
// set tracks every live devNull rdbuf so that a destructor running on one
// thread never restores cerr/clog to a buffer owned by a concurrently-live
// (or already-freed) instance on another thread.  Correct same-thread LIFO
// nesting is preserved because saved pointers that point outside the devNull
// set are always valid original buffers.
class SuppressStreams {
 public:
  SuppressStreams() : devNull_("/dev/null") {
    std::lock_guard<std::mutex> lock(mutex_);
    savedCerr_ = std::cerr.rdbuf();
    savedClog_ = std::clog.rdbuf();
    if (devNull_.is_open()) {
      devNullBufs_.insert(devNull_.rdbuf());
      std::cerr.rdbuf(devNull_.rdbuf());
      std::clog.rdbuf(devNull_.rdbuf());
    }
    if (refCount_++ == 0) {
      originalCerr_ = savedCerr_;
      originalClog_ = savedClog_;
    }
  }

  ~SuppressStreams() {
    std::lock_guard<std::mutex> lock(mutex_);
    devNullBufs_.erase(devNull_.rdbuf());
    allDevNullBufs_.insert(devNull_.rdbuf());
    if (--refCount_ == 0) {
      std::cerr.rdbuf(originalCerr_);
      std::clog.rdbuf(originalClog_);
      allDevNullBufs_.clear();
    } else if (allDevNullBufs_.count(savedCerr_) == 0) {
      // savedCerr_ was never a devNull buffer — it is a valid original.
      std::cerr.rdbuf(savedCerr_);
      std::clog.rdbuf(savedClog_);
    } else if (devNullBufs_.count(savedCerr_) != 0) {
      // savedCerr_ is a devNull that is still alive (we hold the mutex).
      std::cerr.rdbuf(savedCerr_);
      std::clog.rdbuf(savedClog_);
    }
    // else: savedCerr_ was a devNull that has already been freed; leave cerr
    // on whatever is currently active (will be restored by last survivor).
  }

  // Non-copyable, non-movable
  SuppressStreams(const SuppressStreams&) = delete;
  SuppressStreams& operator=(const SuppressStreams&) = delete;
  SuppressStreams(SuppressStreams&&) = delete;
  SuppressStreams& operator=(SuppressStreams&&) = delete;

 private:
  std::ofstream devNull_;
  std::streambuf* savedCerr_;
  std::streambuf* savedClog_;

  static std::mutex mutex_;
  static int refCount_;
  static std::streambuf* originalCerr_;
  static std::streambuf* originalClog_;
  static std::unordered_set<std::streambuf*> devNullBufs_;
  static std::unordered_set<std::streambuf*> allDevNullBufs_;
};

}  // namespace cli_utils
