#pragma once

#include <unistd.h>

#include <exception>
#include <string_view>
#include <utility>

namespace utils {

[[noreturn]] void throw_sys_error(std::string msg);
std::exception_ptr make_sys_error(std::string msg);

// RAII wrapper for file descriptor
class handle {
public:
  handle() : h(-1) {}
  explicit handle(int h) : h(h) {}

  handle(const handle &) = delete;
  handle(handle &&o) : h(std::exchange(o.h, -1)) {}

  handle &operator=(handle o) {
    std::swap(h, o.h);
    return *this;
  }

  ~handle() {
    if (h != -1)
      ::close(h);
  }

  explicit operator bool() const { return h != -1; }
  explicit operator int() const { return h; }

private:
  int h;
};

} // namespace utils
