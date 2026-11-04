# P1 network follow-ups

## Full-hint sequence watermark after a long healthy interval

Status: identified by static review; deferred from the current P1 ten-minute acceptance run. No product change is included in this report.

The expedition stale-input recovery path keeps `snapshotHintSeq` and compares a candidate command sequence using unsigned 16-bit distance. A candidate is newer only when its distance is nonzero and less than `0x8000`. The watermark currently advances only when a full hint is requested; accepted commands during a healthy interval do not advance it.

For example, after a hint at sequence 100, a later legitimate command at sequence 32868 has distance 32768 and is rejected as an old hint. Subsequent sequences in the same half of the sequence space also cannot request a full snapshot. This can occur after at least 32768 new command sequences between hints: about 18 minutes 12 seconds at 30Hz, or 9 minutes 6 seconds at 60Hz. The condition depends on sequence advancement, rather than wall time alone.

The current P1 acceptance driver sends approximately 30Hz commands, so its ten-minute run does not reach this half-range boundary. This scope conclusion does not establish that the implementation is safe for longer sessions or higher command rates, and does not establish that the current end-to-end acceptance run has passed.

The existing periodic forced-full mechanism remains enabled: a full snapshot becomes due after 40 simulation ticks (nominally two seconds), subject to the snapshot send schedule and successful delivery. The defect affects the additional stale-command recovery hint; it does not remove this fallback. Packet loss can also lose a periodic full, so the fallback is not an unconditional two-second delivery guarantee.

Implementation references:

- [Runtime hint comparison and watermark update](../../server/src/server/runtime.cpp#L489-L501)
- [Per-client hint state](../../server/src/server/runtime.hpp#L138-L142)
- [Periodic full interval and baseline semantics](../../server/src/replication/baseline.hpp#L4-L14)
- [Periodic full due check](../../server/src/replication/baseline.cpp#L27-L30)

## Suggested future regression

Extend the public Runtime UDP recovery test without launching a player. Trigger an initial hint, advance the command sequence through a healthy interval with valid accepted commands, then send an over-window stale command at sequence distances 32767, 32768, and 32769. Assert that legitimate forward progression can still request recovery after the healthy interval, while duplicate and genuinely reordered hints remain suppressed. Include a 65535-to-0 wrap case and retain the current cooldown, endpoint, future-tick, Classic, and rejected-input checks.

Any future fix should preserve accepted-command deduplication and the bounded full-hint rate. Possible approaches include tracking a current accepted-command watermark or explicitly managing the hint watermark's sequence epoch. The choice requires review and a failing regression before implementation.
