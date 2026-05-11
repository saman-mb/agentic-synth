# ADR-0002: SPSC Patch Queue as the Agent-to-Audio Hand-Off

## Status

Accepted

## Date

2026-05-11

## Deciders

- Agentic Synth maintainers

## Context and Problem Statement

The agent thread produces `PatchStruct` snapshots (heuristic parse, LLM refinement, knob tweaks). The audio thread must consume them without ever blocking, allocating, or taking a lock — otherwise the host's real-time callback misses its deadline. We need a hand-off that is wait-free on the consumer, bounded in memory, and safe across two specific threads.

## Decision Drivers

- Real-time safety: audio thread cannot lock, allocate, or syscall.
- Bounded memory: no unbounded growth if producer outruns consumer.
- Lowest per-block overhead on the consumer.

## Considered Options

- Single-producer / single-consumer ring buffer (SPSC)
- Multi-producer / single-consumer queue (MPSC)
- Lock-based queue (`std::mutex` + `std::deque`)
- Mailbox pattern (single `std::atomic<PatchStruct*>`)

## Decision Outcome

Chosen option: `SPSC ring buffer`

`SPSCQueue<T, Capacity>` in `src/engine/SPSCQueue.h` is wait-free, allocation-free, with power-of-two capacity (enforced by `static_assert`) and `std::atomic` head/tail using acquire/release ordering. The canonical alias `PatchQueue = SPSCQueue<PatchStruct, 256>` is the only hand-off used by `PrePatchPipeline`. Head and tail live on separate 64-byte cache lines to avoid false sharing.

The audio thread is the sole consumer (`pop`, `drain_latest`). The agent thread is the sole *logical* producer, but because `submit`, `refinePatch`, and `injectPatch` may be invoked from different threads, `PrePatchPipeline` serialises producer calls with `producerMutex_` — preserving the SPSC contract without burdening the audio thread.

One slot is reserved to disambiguate full from empty (head == tail), so the queue holds at most `Capacity - 1` items. Push failure increments `droppedPatches_`; the queue never blocks or grows.

## Pros and Cons of the Options

### SPSC ring buffer

- Pros: Wait-free both sides; allocation-free; bounded latency; fixed footprint; false-sharing avoided.
- Cons: Single-producer contract needs an external mutex when multiple producers exist; one slot sacrificed; capacity must be sized for worst-case bursts.

### MPSC queue

- Pros: Native multi-producer support.
- Cons: More expensive producer-side atomics; no real benefit when producers are rare.

### Lock-based queue

- Pros: Trivial.
- Cons: Mutex on the audio thread is a real-time violation; priority inversion risk.

### Mailbox pattern

- Pros: Simplest possible.
- Cons: Loses intermediate patches (the 5 lerp steps in `refinePatch`); no FIFO.

## Links

- [src/engine/SPSCQueue.h](../../src/engine/SPSCQueue.h)
- [src/agent/PrePatchPipeline.h](../../src/agent/PrePatchPipeline.h)
