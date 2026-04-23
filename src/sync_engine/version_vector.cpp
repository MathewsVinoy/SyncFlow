#include "syncflow/types.hpp"
#include <algorithm>
#include <sstream>

namespace syncflow {

// ============================================================================
// VersionVector Implementation
// ============================================================================

VersionVector::VersionVector(const std::map<DeviceId, uint64_t>& clocks)
    : clocks_(clocks) {}

void VersionVector::update(const DeviceId& device_id, uint64_t clock) {
  clocks_[device_id] = std::max(clocks_[device_id], clock);
}

void VersionVector::increment(const DeviceId& device_id) {
  clocks_[device_id]++;
}

uint64_t VersionVector::get(const DeviceId& device_id) const {
  auto it = clocks_.find(device_id);
  return (it != clocks_.end()) ? it->second : 0;
}

bool VersionVector::happens_before(const VersionVector& other) const {
  // This version happens before other if:
  // - For all devices: this.clocks[d] <= other.clocks[d]
  // - For at least one device: this.clocks[d] < other.clocks[d]
  
  bool all_le = true;
  bool some_lt = false;
  
  // Check all devices in this version
  for (const auto& [device_id, clock_val] : clocks_) {
    uint64_t other_val = other.get(device_id);
    if (clock_val > other_val) {
      all_le = false;
      break;
    }
    if (clock_val < other_val) {
      some_lt = true;
    }
  }
  
  // Check if other has devices this doesn't have
  for (const auto& [device_id, clock_val] : other.clocks_) {
    if (clocks_.find(device_id) == clocks_.end() && clock_val > 0) {
      some_lt = true;
    }
  }
  
  return all_le && some_lt;
}

bool VersionVector::concurrent_with(const VersionVector& other) const {
  // Two versions are concurrent if neither happens before the other
  return !happens_before(other) && !other.happens_before(*this);
}

bool VersionVector::equal_to(const VersionVector& other) const {
  return clocks_ == other.clocks_;
}

std::string VersionVector::to_string() const {
  std::stringstream ss;
  ss << "{";
  bool first = true;
  for (const auto& [device_id, clock_val] : clocks_) {
    if (!first) ss << ",";
    ss << "\"" << device_id << "\":" << clock_val;
    first = false;
  }
  ss << "}";
  return ss.str();
}

VersionVector VersionVector::from_string(const std::string& str [[maybe_unused]]) {
  // TODO: Implement JSON parsing
  return VersionVector();
}

}  // namespace syncflow
