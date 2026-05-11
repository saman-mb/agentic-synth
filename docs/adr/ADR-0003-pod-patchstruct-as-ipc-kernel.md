# ADR-0003: POD PatchStruct as the IPC Kernel

## Status

Accepted

## Date

2026-05-11

## Deciders

- Agentic Synth maintainers

## Context and Problem Statement

`PatchStruct` is the unit of synth state that crosses thread boundaries via `SPSCQueue` and previously crossed process boundaries via the deleted WebSocket bridge. The audio thread snapshots it via `std::memcpy`, lerps between two instances, and applies it to the voice manager. Any layout with pointers, non-trivial constructors, or variable-size fields would either break lock-free transfer or force allocation on the audio thread.

## Decision Drivers

- Lock-free transfer over SPSC requires `std::is_trivially_copyable_v<T>`.
- Snapshot semantics (coherent view in one copy) must be cheap.
- ABI stability so bytes survive any future cross-process transport.
- Determinism: no surprise allocation, exceptions, or virtual dispatch.

## Considered Options

- Plain-old-data fixed-size struct
- Serialised JSON as the canonical hand-off
- Heap-allocated patch object via shared pointer
- Schema-versioned variable-length binary blob

## Decision Outcome

Chosen option: `Plain-old-data fixed-size struct`

`PatchStruct` in `src/engine/PatchStruct.h` is a flat aggregate of `float`, `uint8_t`, and `enum class : uint8_t` fields, with explicit `_pad` bytes to lock alignment. Sub-structures (`OscParams`, `EnvParams`, `FilterParams`, `LfoParams`, `ReverbParams`, `DelayParams`) each carry their own `sizeof` `static_assert`, and the top-level type asserts `std::is_trivially_copyable_v<PatchStruct>` at compile time. `SPSCQueue` enforces the same trait on its element type, so a future field that breaks the contract fails the build — not the audio thread.

A `kPatchStructVersion` header field is reserved for schema migration; a monotonic `patch_id` lets consumers detect skipped or replayed patches. Fixed bounds (`kMaxOscillators = 3`, `kMaxLfos = 2`) keep the struct compact. Variable-length descriptors (e.g. wavetable names) live in side tables keyed by integer index, not in the struct.

## Pros and Cons of the Options

### Plain-old-data fixed-size struct

- Pros: `memcpy`-safe; allocation-free; cheap to snapshot; ABI-stable; compile-time contract; trivially serialisable.
- Cons: No dynamic payloads — strings live elsewhere; widening touches every consumer; schema evolution needs the `version` field plus migration code.

### Serialised JSON

- Pros: Human-readable, extensible.
- Cons: Allocation and parsing per hand-off; unacceptable on the audio thread.

### Heap-allocated patch via shared pointer

- Pros: Variable-length fields allowed.
- Cons: Refcount traffic and destruction risk on the audio thread.

### Schema-versioned variable-length binary

- Pros: Forward-compatible by design.
- Cons: Needs a parser or staging copy on the consumer; over-engineered for present need.

## Links

- [src/engine/PatchStruct.h](../../src/engine/PatchStruct.h)
- [ADR-0002](./ADR-0002-spsc-patch-queue-contract.md)
