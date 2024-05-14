#pragma once

#include "utils.hpp"

#include <poll.h>

#include <chrono>
#include <concepts>
#include <coroutine>
#include <exception>
#include <functional>
#include <span>
#include <stdexcept>
#include <vector>

namespace coro {

namespace detail {
template <typename Task, typename T, typename Initial> struct promise {
  using handle_type = std::coroutine_handle<promise>;
  auto get_return_object() -> Task {
    return Task{handle_type::from_promise(*this)};
  }

  auto initial_suspend() { return Initial{}; }
  auto final_suspend() noexcept {
    struct final_awaiter {
      bool await_ready() noexcept { return false; }
      auto await_suspend(handle_type handle) noexcept {
        // if our task is awaited, we want to resume the awaiting coroutine
        return handle.promise().continuation;
      }
      void await_resume() noexcept {}
    };

    return final_awaiter{};
  }

  template <std::convertible_to<T> U> void return_value(U &&value) {
    this->value = std::forward<U>(value);
  }
  void unhandled_exception() { exception = std::current_exception(); }

private:
  std::coroutine_handle<> continuation = std::noop_coroutine();
  T value;
  std::exception_ptr exception = nullptr;

  friend Task;
};

template <typename Task, typename Initial> struct promise<Task, void, Initial> {
  using handle_type = std::coroutine_handle<promise>;
  auto get_return_object() -> Task {
    return Task{handle_type::from_promise(*this)};
  }

  auto initial_suspend() { return Initial{}; }
  auto final_suspend() noexcept {
    struct final_awaiter {
      bool await_ready() noexcept { return false; }
      auto await_suspend(handle_type handle) noexcept {
        // if our task is awaited, we want to resume the awaiting coroutine
        return handle.promise().continuation;
      }
      void await_resume() noexcept {}
    };

    return final_awaiter{};
  }

  void return_void() {}
  void unhandled_exception() { exception = std::current_exception(); }

private:
  std::coroutine_handle<> continuation = std::noop_coroutine();
  std::exception_ptr exception = nullptr;

  friend Task;
};

template <typename Promise> struct UniqueHandle {
  using handle_type = std::coroutine_handle<Promise>;

  UniqueHandle(handle_type handle) : handle(handle) {}
  UniqueHandle(UniqueHandle &&other) : handle(other.handle) {
    other.handle = nullptr;
  }
  UniqueHandle &operator=(UniqueHandle &&other) {
    if (this != &other) {
      if (handle)
        handle.destroy();
      handle = other.handle;
      other.handle = nullptr;
    }
    return *this;
  }

  UniqueHandle(const UniqueHandle &) = delete;
  UniqueHandle &operator=(const UniqueHandle &) = delete;

  ~UniqueHandle() {
    if (handle)
      handle.destroy();
  }

  handle_type *operator->() { return &handle; }
  handle_type operator*() { return handle; }

  handle_type handle;
};
} // namespace detail

// fire-and-forget task (as it is not awaited, we cannot return any
// value/exception) behaves somewhat like detached std::thread (no return
// value/joining and exception means terminate)
struct task {
  struct promise_type {
    // as this is a fire-and-forget task we don't return any handle to the
    // coroutine
    auto get_return_object() -> task { return {}; }

    // as it is eagerly started, we don't need to do anything here (no one will
    // call co_await on it)
    std::suspend_never initial_suspend() { return {}; }

    // let the task clean itself up
    std::suspend_never final_suspend() noexcept { return {}; }

    // as the task is fire-and-forget we cannot forward the exception
    //  so (mirroring the behavior of std::jthread) we terminate the program
    [[noreturn]] void unhandled_exception() { std::terminate(); }

    void return_void() {}
  };
};

template <typename T = void> class lazy_task {
public:
  using promise_type = detail::promise<lazy_task<T>, T, std::suspend_always>;
  using handle_type = std::coroutine_handle<promise_type>;

  lazy_task(handle_type handle) : handle(handle) {}

  auto operator co_await() {
    struct awaiter {
      handle_type handle;

      bool await_ready() const { return handle.done(); }
      auto await_suspend(std::coroutine_handle<> continuation) {
        handle.promise().continuation = continuation;
        return handle;
      }
      T await_resume() {
        if (handle.promise().exception)
          std::rethrow_exception(handle.promise().exception);

        if constexpr (std::is_same_v<T, void>) {
          return;
        } else {
          return std::move(handle.promise().value);
        }
      }
    };

    return awaiter{*handle};
  }

private:
  detail::UniqueHandle<promise_type> handle;
};

template <typename T = void> class eager_task {
public:
  using promise_type = detail::promise<eager_task<T>, T, std::suspend_never>;
  using handle_type = std::coroutine_handle<promise_type>;

  eager_task(handle_type handle) : handle(handle) {}

  auto operator co_await() {
    struct awaiter {
      handle_type handle;

      bool await_ready() const { return handle.done(); }
      void await_suspend(std::coroutine_handle<> continuation) {
        handle.promise().continuation = continuation;
        // as it is eagerly started the task is already running (no need to
        // resume it)
      }
      T await_resume() {
        if (handle.promise().exception)
          std::rethrow_exception(handle.promise().exception);

        if constexpr (std::is_same_v<T, void>) {
          return;
        } else {
          return std::move(handle.promise().value);
        }
      }
    };

    return awaiter{*handle};
  }

private:
  detail::UniqueHandle<promise_type> handle;
};

template <typename T> class generator {
public:
  struct promise_type;
  using handle_type = std::coroutine_handle<promise_type>;

  struct promise_type {
    auto get_return_object() -> generator {
      return generator{handle_type::from_promise(*this)};
    }

    std::suspend_always initial_suspend() { return {}; }
    std::suspend_always final_suspend() noexcept { return {}; }

    template <std::convertible_to<T> U> auto yield_value(U &&value) {
      this->value = std::forward<U>(value);
      return std::suspend_always{};
    }

    void return_void() {}
    void unhandled_exception() { exception = std::current_exception(); }

    T value;
    std::exception_ptr exception = nullptr;
  };

  struct iterator {
    void operator++() { handle.resume(); }
    const T &operator*() const { return handle.promise().value; }
    bool operator==(std::default_sentinel_t) const {
      return !handle || handle.done();
    }

    handle_type handle;
  };

  generator(handle_type handle) : handle(handle) {}

  iterator begin() {
    if (*handle)
      handle->resume();
    return {*handle};
  }

  std::default_sentinel_t end() { return {}; }

private:
  detail::UniqueHandle<promise_type> handle;
};

template <typename T> class async_generator {
public:
  struct promise_type;
  using handle_type = std::coroutine_handle<promise_type>;

  struct promise_type {
    auto get_return_object() -> async_generator {
      return async_generator{handle_type::from_promise(*this)};
    }

    std::suspend_always initial_suspend() { return {}; }
    auto final_suspend() noexcept {
      struct final_awaiter {
        bool await_ready() const noexcept { return false; }
        auto
        await_suspend(std::coroutine_handle<promise_type> handle) noexcept {
          return handle.promise().continuation;
        }
        void await_resume() noexcept {}
      };

      return final_awaiter{};
    }

    template <std::convertible_to<T> U> auto yield_value(U &&value) {
      this->value = std::forward<U>(value);

      struct awaiter {
        bool await_ready() const noexcept { return false; }
        auto
        await_suspend(std::coroutine_handle<promise_type> handle) noexcept {
          return handle.promise().continuation;
        }
        void await_resume() noexcept { return; }
      };

      return awaiter{};
    }

    void return_void() {}
    void unhandled_exception() { exception = std::current_exception(); }

    std::coroutine_handle<> continuation = std::noop_coroutine();
    T value;
    std::exception_ptr exception = nullptr;
  };

  async_generator(handle_type handle) : handle(handle) {}

  struct iterator {
    auto operator++() {
      struct awaiter {
        handle_type handle;

        bool await_ready() const { return false; }
        auto await_suspend(std::coroutine_handle<> continuation) {
          handle.promise().continuation = continuation;
          return handle;
        }
        void await_resume() {
          if (handle.promise().exception)
            std::rethrow_exception(handle.promise().exception);
        }
      };

      return awaiter{handle};
    }
    const T &operator*() const { return handle.promise().value; }

    bool operator==(std::default_sentinel_t) const {
      return !handle || handle.done();
    }

    handle_type handle;
  };

  auto begin() {
    struct awaiter {
      bool await_ready() const { return handle.done(); }
      auto await_suspend(std::coroutine_handle<> continuation) {
        handle.promise().continuation = continuation;
        return handle;
      }
      auto await_resume() {
        if (handle.promise().exception)
          std::rethrow_exception(handle.promise().exception);

        return iterator{handle};
      }

      handle_type handle;
    };

    return awaiter{*handle};
  }

  std::default_sentinel_t end() { return {}; }

private:
  detail::UniqueHandle<promise_type> handle;
};

/*
class that supports awaiting on file descriptors (poll) with timeout
*/
class io_engine {
public:
  io_engine() = default;
  io_engine(const io_engine &) = delete;
  io_engine &operator=(const io_engine &) = delete;
  io_engine(io_engine &&) = delete;
  io_engine &operator=(io_engine &&) = delete;

  ~io_engine();

  // pull until no more ready events
  void pull();

  // pull all events (wait for the list to be empty)
  void pull_all();

  auto wait_until(std::chrono::time_point<std::chrono::steady_clock> timeout) {
    struct awaiter {
      io_engine &engine;
      operation op;

      bool await_ready() const {
        return std::chrono::steady_clock::now() >= op.timeout;
      }
      void await_suspend(std::coroutine_handle<> handle) {
        op.handle = handle;
        engine.add_operation(&op);
      }
      void await_resume() {
        if (op.exception)
          std::rethrow_exception(op.exception);
      }
    };

    return awaiter{*this, operation{nullptr, -1, 0, timeout}};
  }

  template <class Rep, class Period>
  auto wait_for(std::chrono::duration<Rep, Period> timeout_duration) {
    return wait_until(std::chrono::steady_clock::now() + timeout_duration);
  }

  auto poll_until(const utils::handle &fd, short events,
                  std::chrono::time_point<std::chrono::steady_clock> timeout) {
    struct awaiter {
      io_engine &engine;
      operation op;

      bool await_ready() const {
        return std::chrono::steady_clock::now() >= op.timeout;
      }
      void await_suspend(std::coroutine_handle<> handle) {
        op.handle = handle;
        engine.add_operation(&op);
      }
      short await_resume() {
        if (op.exception)
          std::rethrow_exception(op.exception);
        return op.revents;
      }
    };

    return awaiter{*this,
                   operation{nullptr, static_cast<int>(fd), events, timeout}};
  }

  template <class Rep, class Period>
  auto poll_for(const utils::handle &fd, short events,
                const std::chrono::duration<Rep, Period> &timeout_duration) {
    return poll_until(fd, events,
                      std::chrono::steady_clock::now() + timeout_duration);
  }

  auto poll(const utils::handle &fd, short events) {
    return poll_until(fd, events, std::chrono::steady_clock::time_point::max());
  }

  // get flags and return immediately
  auto poll_once(const utils::handle &fd) {
    struct awaiter {
      io_engine &engine;
      operation op;

      bool await_ready() const { return false; }
      void await_suspend(std::coroutine_handle<> handle) {
        op.handle = handle;
        engine.add_operation(&op);
      }
      short await_resume() {
        if (op.exception)
          std::rethrow_exception(op.exception);
        return op.revents;
      }
    };

    return awaiter{*this, operation{nullptr, static_cast<int>(fd), 0, {}}};
  }

  struct poll_error : std::runtime_error {
    poll_error(std::string what, int fd)
        : std::runtime_error(what + " on " + std::to_string(fd)), fd(fd) {}

    int fd;
  };
  struct pollerr_error : poll_error {
    pollerr_error(int fd) : poll_error("POLLERR", fd) {}
  };
  struct pollhup_error : poll_error {
    pollhup_error(int fd) : poll_error("POLLHUP", fd) {}
  };
  struct pollnval_error : poll_error {
    pollnval_error(int fd) : poll_error("POLLNVAL", fd) {}
  };

private:
  struct operation {
    std::coroutine_handle<> handle;
    int fd;
    short events;
    std::chrono::time_point<std::chrono::steady_clock> timeout;

    short revents = 0;
    std::exception_ptr exception = nullptr;
  };

  void add_operation(operation *op);
  void do_pull(std::function<int(std::span<pollfd>)> poll_fn);

  std::vector<operation *> operations;
};
} // namespace coro