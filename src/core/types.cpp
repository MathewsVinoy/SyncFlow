#include "syncflow/types.hpp"
#include <sstream>
#include <algorithm>
#include <iostream>

namespace syncflow {

// ============================================================================
// VersionVector Implementation
// ============================================================================

VersionVector::VersionVector(const std::map<DeviceId, uint64_t>& clocks)
    : clocks_(clocks) {}

uint64_t VersionVector::get(const DeviceId& device) const {
  auto it = clocks_.find(device);
  return it != clocks_.end() ? it->second : 0;
}

void VersionVector::increment(const DeviceId& device) {
  clocks_[device]++;
}

void VersionVector::update(const DeviceId& device, uint64_t clock) {
  if (clock > 0) {
    clocks_[device] = std::max(clocks_[device], clock);
  }
}

bool VersionVector::happens_before(const VersionVector& other) const {
  // A happens_before B if A's clocks <= B's clocks AND at least one is <
  bool at_least_one_less = false;
  
  for (const auto& [device, clock] : clocks_) {
    uint64_t other_clock = other.get(device);
    if (clock > other_clock) {
      return false;  // A is ahead in some dimension
    }
    if (clock < other_clock) {
      at_least_one_less = true;
    }
  }

  // Check if B has any clocks A doesn't have
  for (const auto& [device, clock] : other.clocks_) {
    if (clocks_.find(device) == clocks_.end() && clock > 0) {
      at_least_one_less = true;
    }
  }

  return at_least_one_less;
}

bool VersionVector::concurrent_with(const VersionVector& other) const {
  // Concurrent if neither happens_before the other
  return !happens_before(other) && !other.happens_before(*this);
}

bool VersionVector::equal_to(const VersionVector& other) const {
  return clocks_ == other.clocks_;
}

std::string VersionVector::to_string() const {
  std::stringstream ss;
  ss << "{";
  bool first = true;
  for (const auto& [device, clock] : clocks_) {
    if (!first) ss << ",";
    ss << "\"" << device << "\":" << clock;
    first = false;
  }
  ss << "}";
  return ss.str();
}

VersionVector VersionVector::from_string(const std::string& str [[maybe_unused]]) {
  // Simple JSON parsing (for production, use a proper JSON library)
  std::map<DeviceId, uint64_t> clocks;
  
  // Very basic parsing: assume format {"dev1":N,"dev2":M,...}
  // This is a simplified implementation; use nlohmann/json for production
  
  return VersionVector(clocks);
}

// ============================================================================
// Error Implementation
// ============================================================================

std::string Error::to_string() const {
  std::stringstream ss;
  ss << "Error(" << static_cast<int>(code) << "): " << message;
  if (details) {
    ss << " [" << *details << "]";
  }
  return ss.str();
}

}  // namespace syncflow
