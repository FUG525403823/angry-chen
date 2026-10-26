// Explicit ADR-019 export: reuse the fixture world's setup and projection helpers.
// This executable is not a test mode and cannot change test expectations while running tests.
#include "../tests/fixture_test.cpp"

int main(int argc, char** argv) {
  if (argc != 2) return 2;
  ac::test::Fixture fixture;
  std::string error;
  if (!ac::test::loadFixtureFile(argv[1], fixture, error)) {
    std::fprintf(stderr, "%s\n", error.c_str());
    return 1;
  }
  ac::test::DiffSink diff;
  auto world = createFixtureWorld(fixture, diff);
  if (diff.has || fixture.configHash != ac::test::hashHex(ac::test::configHash())) return 1;
  const int32_t playerCount = static_cast<int32_t>(ascendingPlayerIds(*world).size());
  auto director = ac::waves::createDirectorState();
  if (fixture.startWave != 0u) ac::waves::planWave(director, static_cast<int32_t>(fixture.startWave), playerCount);
  std::vector<ac::test::FixtureCommand> commands;
  std::vector<ac::test::FixtureEntity> entities;
  std::vector<ac::test::FixtureEvent> events;
  uint64_t chain = ac::test::kFnvOffsetBasis;
  for (uint32_t tick = 1; tick <= fixture.ticks; ++tick) {
    if (!fixture.commandAt(tick, commands)) return 1;
    const auto players = ascendingPlayerIds(*world);
    if (commands.size() > players.size()) return 1;
    std::vector<Command> slots;
    for (std::size_t i = 0; i < commands.size(); ++i) {
      const auto& source = commands[i];
      if (source.id != players[i]) return 1;
      Command command{};
      command.moveX = source.moveX; command.moveY = source.moveY;
      command.yaw = source.yaw; command.pitch = source.pitch;
      command.buttons = source.buttons; command.switchTo = source.switchTo;
      command.seq = source.seq; command.clientTick = source.clientTick;
      slots.push_back(command);
    }
    ac::sim::stepWorld(*world, slots.empty() ? nullptr : slots.data(), static_cast<uint32_t>(slots.size()), ac::test::kFixtureDtMs);
    if (fixture.startWave != 0u) {
      uint16_t ids[ac::sim::kMaxEntities];
      const auto count = ac::sim::collectPlayerIds(*world, ids);
      ac::waves::updateDirector(*world, director, playerCount, world->rng.spawn, ids, count);
    }
    if (world->tick != tick || world->timeMs != tick * ac::config::kStepDtMs) return 1;
    projectEntities(*world, entities);
    projectEvents(*world, events);
    const ac::test::FixtureRng rng{world->rng.ai.a, world->rng.spawn.a, world->rng.fx.a};
    chain = ac::test::fnv1a64Text(ac::test::projectionText(tick, commands, entities, events, rng), chain);
    std::printf("{\"tick\":%u,\"hash\":\"%s\",\"entities\":[", tick, ac::test::hash64Hex(chain).c_str());
    bool comma = false;
    for (const auto& e : entities) {
      std::printf("%s{\"id\":%u,\"kind\":\"%s\",\"pos\":[%.17g,%.17g,%.17g],\"yaw\":%.17g,\"pitch\":%.17g,\"hp\":%.17g,\"flags\":%u}", comma ? "," : "", e.id, e.kind.c_str(), e.posX,e.posY,e.posZ,e.yaw,e.pitch,e.hp,e.flags);
      comma = true;
    }
    std::printf("],\"events\":["); comma = false;
    for (const auto& e : events) {
      std::printf("%s{\"tick\":%u,\"type\":\"%s\",\"flags\":%u,\"subjectId\":%u,\"targetId\":%u,\"x\":%.17g,\"y\":%.17g,\"z\":%.17g,\"value\":%.17g", comma ? "," : "",e.tick,e.type.c_str(),e.flags,e.subjectId,e.targetId,e.x,e.y,e.z,e.value);
      if (ac::test::eventCarriesKind(e.type)) std::printf(",\"kind\":%u", e.kind);
      std::printf("}"); comma = true;
    }
    std::printf("],\"rngState\":{\"ai\":%u,\"spawn\":%u,\"fx\":%u}",rng.ai,rng.spawn,rng.fx);
    for (const auto& entry : fixture.snapshot) {
      if (entry.tick != tick) continue;
      ac::net::EntityRecord records[ac::net::kMaxEntityRecordsPerFrame];
      const auto count = ac::replication::projectWorld(*world, records, ac::net::kMaxEntityRecordsPerFrame);
      uint8_t bytes[ac::net::kMaxSnapshotBytes];
      ac::net::PacketHeader header{};
      header.version = ac::net::kProtocolVersion;
      header.type = static_cast<uint8_t>(ac::net::PacketType::kSnapshot);
      ac::net::SnapshotFrame frame{};
      frame.tick = tick; frame.serverTimeMs = world->timeMs;
      frame.records = records; frame.recordCount = count;
      const auto encoded = ac::net::encodeSnapshot(header, frame, bytes, sizeof bytes);
      if (!encoded.isOk) return 1;
      const auto decoded = ac::net::decodeSnapshot(bytes, encoded.bytes, nullptr);
      if (!decoded.isOk) return 1;
      std::vector<ac::test::SnapshotRecordFields> fields;
      for (const auto& r : decoded.value.records) {
        fields.push_back({r.id,r.kind(),r.flags(),r.xCm,r.yCm,r.zCm,r.yawUnits,r.pitchUnits,r.hpRatioUnits,r.state});
      }
      const auto encodeHash = ac::test::fnv1a64(bytes + ac::net::kSnapshotHeadBytes + 1u, count * ac::test::kSnapshotRecordBytes, ac::test::kFnvOffsetBasis);
      const auto decodeHash = ac::test::fnv1a64Text(ac::test::snapshotDecodeText(fields), ac::test::kFnvOffsetBasis);
      std::printf(",\"snapshot\":{\"tick\":%u,\"records\":%zu,\"encodeHash\":\"%s\",\"decodeHash\":\"%s\"}", tick,count,ac::test::hash64Hex(encodeHash).c_str(),ac::test::hash64Hex(decodeHash).c_str());
    }
    std::printf("}\n");
  }
  return 0;
}
