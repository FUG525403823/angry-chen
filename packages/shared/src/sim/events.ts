import { createSimEvent, type EntityId, type SimEvent, type World } from '../world.ts';

/**
 * 统一事件入队：对象从 world.eventPool 取，池满则丢弃并累计 world.stats.eventsDropped。
 *
 * 「仅当 tick 内有效」：stepWorld 每 tick 复位 eventCursor 并清空 events 的引用，
 * 池对象在下一次入队时被覆写，因此消费方必须在同一 tick 内完成读取/编码（room.ts 广播即如此）。
 */
export function pushEvent(
  world: World,
  type: SimEvent['type'],
  flags: number,
  subjectId: EntityId,
  targetId: EntityId,
  x: number,
  y: number,
  z: number,
  value: number,
): void {
  const cursor = world.eventCursor;
  if (cursor >= world.eventPool.length) {
    world.stats.eventsDropped += 1;
    return;
  }
  const event = world.eventPool[cursor] ?? createSimEvent();
  world.eventPool[cursor] = event;
  event.type = type;
  event.tick = world.tick;
  event.flags = flags;
  event.subjectId = subjectId;
  event.targetId = targetId;
  event.x = x;
  event.y = y;
  event.z = z;
  event.value = value;
  world.events.push(event);
  world.eventCursor = cursor + 1;
}
