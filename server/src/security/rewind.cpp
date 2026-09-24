#include "security/rewind.hpp"

namespace ac::security {

uint32_t rewindMs(uint32_t rttMs) noexcept {
  const uint32_t half = rttMs / 2u;
  return half > kRewindLimitMs ? kRewindLimitMs : half;
}

RewindOutcome sampleRewindPose(const ac::sim::PoseHistory& history, uint32_t rttMs, uint16_t entityId,
                               ac::metrics::CounterRegistry* counters) noexcept {
  RewindOutcome out{};
  if (isRewindOverLimit(rttMs)) {
    out.isClamped = true;
    ac::metrics::bumpCounter(counters, ac::metrics::CounterId::kRewindClamped);
    return out;
  }
  const bool sampled = ac::sim::samplePoseAgo(history, rewindMs(rttMs), entityId, out.pose);
  out.isFound = out.pose.found;
  out.isOk = sampled && out.pose.isOk;
  if (!out.isOk) {
    out.isClamped = true;
    ac::metrics::bumpCounter(counters, ac::metrics::CounterId::kRewindClamped);
  }
  return out;
}

}  // namespace ac::security
