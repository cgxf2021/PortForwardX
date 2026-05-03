#include "portforwardx/version.hpp"

#include <sstream>

namespace portforwardx {

std::string version_string() {
  std::ostringstream oss;
  oss << PORTFORWARDX_VERSION_MAJOR << '.' << PORTFORWARDX_VERSION_MINOR << '.'
      << PORTFORWARDX_VERSION_PATCH;
  return oss.str();
}

}  // namespace portforwardx
