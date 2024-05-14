#include "utils.hpp"

#include <cerrno>
#include <exception>
#include <stdexcept>
#include <system_error>

using namespace utils;

[[noreturn]] void utils::throw_sys_error(std::string msg) {
  throw std::system_error(errno, std::system_category(), std::move(msg));
}

std::exception_ptr utils::make_sys_error(std::string msg) {
  return std::make_exception_ptr(
      std::system_error(errno, std::system_category(), std::move(msg)));
}