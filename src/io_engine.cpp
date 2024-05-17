#include "io_engine.hpp"

#include <poll.h>

#include <algorithm>
#include <cassert>
#include <exception>
#include <stdexcept>
#include <vector>

using namespace coro;

io_engine::~io_engine() {
  std::exception_ptr eptr =
      std::make_exception_ptr(std::runtime_error("io_engine destroyed"));

  while (!operations.empty()) {
    auto *op = operations.back();
    operations.pop_back();
    op->exception = eptr;
    op->handle.resume();
  }
}

void io_engine::do_pull(std::function<int(std::span<pollfd>)> poll_fn) {
  int ret;

  {
    std::vector<pollfd> fds;
    fds.reserve(operations.size());

    for (auto *op : operations) {
      pollfd pfd;
      pfd.fd = op->fd;
      pfd.events = op->events;
      pfd.revents = 0;
      fds.push_back(pfd);
    }

    ret = poll_fn(fds);

    for (size_t i = 0; i < fds.size(); ++i)
      operations[i]->revents = fds[i].revents;
  }

  std::vector<operation *> to_resume;

  auto now = std::chrono::steady_clock::now();

  if (ret == -1) {
    std::exception_ptr eptr = utils::make_sys_error("poll");

    // throw error on all waiting tasks (only those that use file descriptors)
    // for timeout tasks, add them too

    auto to_remove_pred = [&](operation *op) {
      return (op->fd != -1) || (now >= op->timeout);
    };

    std::ranges::copy_if(operations, std::back_inserter(to_resume),
                         to_remove_pred);
    std::erase_if(operations, to_remove_pred);

    for (auto *op : to_resume)
      if (op->fd != -1)
        op->exception = eptr;

  } else {
    // add all events that happened

    auto to_remove_pred = [&](operation *op) {
      return (op->fd != -1 &&
              op->revents & (POLLERR | POLLHUP | POLLNVAL | op->events)) ||
             (now >= op->timeout);
    };

    std::ranges::copy_if(operations, std::back_inserter(to_resume),
                         to_remove_pred);
    std::erase_if(operations, to_remove_pred);

    for (auto *op : to_resume) {
      if (op->fd != -1) {
        if (op->revents & POLLERR)
          op->exception = std::make_exception_ptr(pollerr_error(op->fd));
        else if (op->revents & POLLHUP)
          op->exception = std::make_exception_ptr(pollhup_error(op->fd));
        else if (op->revents & POLLNVAL)
          op->exception = std::make_exception_ptr(pollnval_error(op->fd));
      }
    }
  }

  for (auto *op : to_resume)
    op->handle.resume();
}

void io_engine::pull() {
  do_pull([](std::span<pollfd> fds) {
    while (true) {
      // non-blocking poll
      int ret = ::poll(fds.data(), fds.size(), 0);

      if (ret == -1 && errno == EINTR)
        continue;

      return ret;
    }
  });
}

void io_engine::pull_all() {
  while (!operations.empty()) {
    do_pull([&](std::span<pollfd> fds) {
      auto min_timeout = std::chrono::steady_clock::time_point::max();

      for (auto *op : operations)
        if (op->timeout < min_timeout)
          min_timeout = op->timeout;

      while (true) {
        auto now = std::chrono::steady_clock::now();
        auto timeout = std::chrono::duration_cast<std::chrono::milliseconds>(
            min_timeout - now);
        if (timeout.count() < 0)
          return 0;

        int ret = ::poll(fds.data(), fds.size(), timeout.count());
        if (ret == -1) {
          if (errno == EINTR)
            continue;

          return ret;
        }

        if (ret > 0)
          return ret;
      }
    });
  }
}

void io_engine::add_operation(operation *op) {
  assert(op->handle);
  operations.push_back(op);
}
