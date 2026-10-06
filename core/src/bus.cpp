#include "gc/bus.hpp"

namespace gc {

const char* portStateName(PortState s) {
  switch (s) {
    case PortState::Empty: return "empty";
    case PortState::Checking: return "checking";
    case PortState::Ok: return "ok";
    case PortState::Fault: return "fault";
    case PortState::Rejected: return "rejected";
  }
  return "unknown";
}

std::optional<std::string> MemoryStorage::read(const std::string& name) {
  for (const auto& [n, d] : files_)
    if (n == name) return d;
  return std::nullopt;
}

bool MemoryStorage::write(const std::string& name, const std::string& data) {
  for (auto& [n, d] : files_)
    if (n == name) {
      d = data;
      return true;
    }
  files_.emplace_back(name, data);
  return true;
}

}  // namespace gc
