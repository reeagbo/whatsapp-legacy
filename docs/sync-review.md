# Conversation state review — 2026-11-05

Reviewed receive notifications, bounded delta queues, snapshot/delta authentication,
write-base replay, cursor persistence, local archive/deletion handling, JNI task
handoff and outgoing operation lifecycle.

## Corrections in 0.26–0.27

- Process every collection in a server_sync notification, not only its first child.
- Reject collection-level errors instead of reporting successful synchronization.
- Give outgoing write snapshots separate durable slots from incoming recovery
  snapshots, preventing an outstanding read response from replacing a write base.
- Remove orphaned write-base tasks after their attempt expires or its session ends.
  Surface storage failure if their removal cannot be persisted.

## Verification

Regression checks cover multi-collection notices, expired write identifiers,
collection errors and simultaneous incoming/outgoing snapshot storage. Existing
checks cover stack lifetime during encoding, snapshot/patch MACs, corrupted
records, optional REMOVE values, large records, bounded pagination, reconstruction
of write bases, outgoing archive/unarchive decoding and receive-cursor isolation.
The full C99 suite and SQLite migration/behavior checks pass locally; GitHub Actions
runs the full suite, Android unit tests, lint and emulator UI checks for this build.

## Remaining limits

These tests do not establish acceptance by the live WhatsApp service. Outgoing
write-base reconstruction now continues across server pages using sealed checkpoints.
Responses referencing external mutations still fail closed; no partial base is sent. Local UI
changes survive restart, but the outgoing Android request queue itself is in memory;
interrupted outgoing operations are recorded in diagnostics rather than guaranteed
to retry after process death. These require separate implementation work and must
not be described as verified functionality.
