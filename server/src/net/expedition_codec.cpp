#include "net/expedition_codec.hpp"
namespace ac::net {
namespace {
bool validRequest(const ExpeditionRequest& v) noexcept {
  if (!v.requestId || v.reserved || v.op < 1 || v.op > 7) return false;
  if ((v.op == 1 || v.op == 6) && (v.runId || v.offerId)) return false;
  if (v.op == 5 && v.offerId) return false;
  if (v.op == 2) return v.choiceId <= 6;
  return v.choiceId == (v.op == 5 ? 1 : 0);
}
bool validHeader(const PacketHeader& h, uint8_t type) noexcept {
  return h.version == kCurrentProtocolVersion && h.type == type && h.flags == kFlagReliable;
}
}
EncodeResult encodeExpeditionRequest(const PacketHeader& h, const ReliableExt& ext, const ExpeditionRequest& v, uint8_t* out, std::size_t cap) noexcept {
  if (!out || cap < 38 || !validHeader(h,13) || !validRequest(v)) return encodeFail();
  ByteWriter w(out,cap); writeHeader(w,h); writeReliableExt(w,ext);
  w.writeU32(v.runId); w.writeU32(v.offerId); w.writeU32(v.requestId); w.writeU8(v.op); w.writeU8(v.choiceId); w.writeU32(v.reserved);
  return encodeOk(w.size());
}
DecodeResult<ExpeditionRequest> decodeExpeditionRequest(const uint8_t* bytes, std::size_t size) noexcept {
  const auto p = decodePacket(bytes,size);
  if (!p.isOk) return decodedFail<ExpeditionRequest>(p.failure);
  if (!validHeader(p.value.header,13)) return decodedFail<ExpeditionRequest>(DecodeFailure::kBadType);
  if (p.value.payloadBytes != 18) return decodedFail<ExpeditionRequest>(DecodeFailure::kBadLength);
  ByteReader r(bytes+p.value.payloadOffset,18); ExpeditionRequest v{};
  v.runId=r.readU32(); v.offerId=r.readU32(); v.requestId=r.readU32(); v.op=r.readU8(); v.choiceId=r.readU8(); v.reserved=r.readU32();
  return validRequest(v) ? decodedOk(v) : decodedFail<ExpeditionRequest>(DecodeFailure::kBadValue);
}
namespace {
bool validState(const ExpeditionState& v) noexcept {
  if(v.schema!=1 || v.mode>1 || v.stage>3 || v.node>3 || v.result>4 || v.layoutId!=1 || v.objectiveId!=1 || v.toolId!=1 || v.resolved>1 || v.rerollsLeft>1 || v.lastResult>9 || v.playerCount>4 || (v.offerCount!=0 && v.offerCount!=3)) return false;
  bool local=v.localPid==0 && v.playerCount==0;
  for(unsigned i=0;i<3;++i) {
    if(v.choices[i]>6 || (!v.offerCount && v.choices[i])) return false;
    for(unsigned j=0;j<i;++j) if(v.choices[i] && v.choices[i]==v.choices[j]) return false;
  }
  if((v.offerCount==0)!=(v.offerId==0)) return false;
  for(unsigned i=0;i<v.playerCount;++i) {
    if(!v.players[i].pid || v.players[i].pid>kEntityIdMax || (v.players[i].cardMask&0xc0)) return false;
    local=local || v.players[i].pid==v.localPid;
    for(unsigned j=0;j<i;++j) if(v.players[i].pid==v.players[j].pid) return false;
  }
  return local;
}
void writeState(ByteWriter& w,const ExpeditionState& v) noexcept {
  w.writeU8(v.schema);w.writeU8(v.mode);w.writeU32(v.runId);w.writeU32(v.revision);w.writeU32(v.clockMs);
  w.writeU8(v.stage);w.writeU8(v.node);w.writeU8(v.result);w.writeU8(v.layoutId);w.writeU8(v.objectiveId);w.writeU8(v.toolId);w.writeU16(v.objectiveHp);
  w.writeU32(v.progressMs);w.writeU32(v.combatLeftMs);w.writeU32(v.rewardDeadlineMs);w.writeU16(v.localPid);w.writeU32(v.offerId);w.writeU8(v.offerCount);
  for(auto c:v.choices)w.writeU8(c);
  w.writeU8(v.resolved);w.writeU8(v.rerollsLeft);w.writeU32(v.lastRequestId);w.writeU8(v.lastResult);w.writeU8(v.playerCount);
  for(unsigned i=0;i<v.playerCount;++i){const auto& p=v.players[i];w.writeU16(p.pid);w.writeU8(p.cardMask);w.writeU32(p.toolReadyAtMs);w.writeU32(p.triggerCount);}
}
}
EncodeResult encodeExpeditionState(const PacketHeader& h,const ReliableExt& ext,const ExpeditionState& v,uint8_t* out,std::size_t cap) noexcept {
  const auto size=20+kExpeditionStateFixedBytes+11*v.playerCount;
  if(!out || cap<size || !validHeader(h,14) || !validState(v))return encodeFail();
  ByteWriter w(out,cap);writeHeader(w,h);writeReliableExt(w,ext);writeState(w,v);return encodeOk(w.size());
}
DecodeResult<ExpeditionState> decodeExpeditionState(const uint8_t* b,std::size_t size) noexcept {
  const auto p=decodePacket(b,size);if(!p.isOk)return decodedFail<ExpeditionState>(p.failure);
  if(!validHeader(p.value.header,14))return decodedFail<ExpeditionState>(DecodeFailure::kBadType);
  if(p.value.payloadBytes<52 || p.value.payloadBytes>96)return decodedFail<ExpeditionState>(DecodeFailure::kBadLength);
  ByteReader r(b+p.value.payloadOffset,p.value.payloadBytes);ExpeditionState v{};
  v.schema=r.readU8();v.mode=r.readU8();v.runId=r.readU32();v.revision=r.readU32();v.clockMs=r.readU32();
  v.stage=r.readU8();v.node=r.readU8();v.result=r.readU8();v.layoutId=r.readU8();v.objectiveId=r.readU8();v.toolId=r.readU8();v.objectiveHp=r.readU16();
  v.progressMs=r.readU32();v.combatLeftMs=r.readU32();v.rewardDeadlineMs=r.readU32();v.localPid=r.readU16();v.offerId=r.readU32();v.offerCount=r.readU8();
  for(auto& c:v.choices)c=r.readU8();
  v.resolved=r.readU8();v.rerollsLeft=r.readU8();v.lastRequestId=r.readU32();v.lastResult=r.readU8();v.playerCount=r.readU8();
  if(v.playerCount>4 || p.value.payloadBytes!=52+11u*v.playerCount)return decodedFail<ExpeditionState>(DecodeFailure::kBadLength);
  for(unsigned i=0;i<v.playerCount;++i){auto& s=v.players[i];s.pid=r.readU16();s.cardMask=r.readU8();s.toolReadyAtMs=r.readU32();s.triggerCount=r.readU32();}
  return validState(v)?decodedOk(v):decodedFail<ExpeditionState>(DecodeFailure::kBadValue);
}
} // namespace ac::net
