#pragma once
#include <cstdint>
#include "net/expedition_codec.hpp"
namespace ac::room {
struct Room;
struct Session;
struct RoomDeps;
// Requests are authenticated by the owning Session, never by wire-provided identity.
bool roomExpeditionRequest(Room&, Session&, const ac::net::ExpeditionRequest&, const RoomDeps&) noexcept;
bool sendExpeditionState(Room&, Session&, const RoomDeps&) noexcept;
bool broadcastExpeditionState(Room&, const RoomDeps&) noexcept;
void beginRoomExpedition(Room&) noexcept;
void resetRoomExpedition(Room&) noexcept;
void tickRoomExpedition(Room&, uint32_t dtMs) noexcept;
void stepRoomEncounter(Room&, uint64_t nowMs, uint32_t dtMs) noexcept;
void executeExpeditionTools(Room&) noexcept;
}
