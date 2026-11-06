# Conversation and group consistency review — 2026-11-06

Baseline: dff27f963ba878bd9016bda7850c0b0d9f850876 (0.35). Reviewed Android event application, SQLite schema and mutations, live message decoding, group metadata/notifications, Sender Keys/device discovery, history import, authenticated app-state reads/writes and request lifetimes.

## Corrections in 0.36

1. Group summaries used state=1 despite not inspecting participants. Android consequently reset left_group=0, including after a confirmed departure. Summaries now have an explicit unknown membership state (2); they update the subject and preserve membership. Full metadata remains authoritative.
2. Group notifications only scheduled a summary refresh. Cached participants/admins survived add/remove/promote/demote changes. Notifications now invalidate members and enqueue a detailed metadata refresh on Android. Busy queries defer the refresh; completion advances the queue, and reconnect requeues interrupted refreshes. An older metadata read for the same group is discarded before notification acknowledgement. Membership changes during a send stop the pending operation; delivery can be uncertain if its message was already transmitted.
3. The group information dialog rendered cached data once, before its network request completed. It now re-renders while open when group events arrive, retaining its close/leave behavior.
4. HistorySync Conversation.archived (field 16) was ignored. History now emits an optional history-archive event for explicit true and false. Android applies this bootstrap value only when archived_changed=0, so delayed history does not undo timestamped app-state or local archive/unarchive decisions. Missing fields do not mean unarchive.

## Confirmed remaining differences and risks

- Outgoing archive/delete operations live in an Android in-memory queue. Process death loses uncompleted requests. Local changes remain committed. The queue also removes requests that cannot start and failed operations have no automatic retry. Durable operation identity, restart recovery and authenticated reconciliation are a separate change; never claim guaranteed remote synchronization.
- Empty/newly-created groups have no remote message anchor. Archive/delete is committed locally but no remote request is queued. The patch builder currently requires an anchor even for archive. Supporting empty message ranges needs dedicated compatibility tests.
- Full metadata is limited to 64 participants, including information-only queries; the application cannot accurately show larger memberships. The decoder accepts large responses, but the group application layer still rejects them. Sending also has a 96-device limit. Increasing only decoder limits did not remove these restrictions.
- Read/unread state, pinning, mute settings and the primary client's unarchive-on-new-message preference are not mirrored. The UI does not expose these states; incoming text does not automatically unarchive a chat. This is an unsupported synchronization scope, not a validated match with the primary client.
- Adding/removing members, editing the remote subject, invitations and advanced group administration have no local action API. Remote changes trigger metadata refresh; these operations cannot yet be initiated here.
- Message deduplication, authors and mutation tombstones use (peer,id,outgoing), without participant in their key. This assumes message IDs are unique within a group. Group revoke/edit authorization and collision handling require dedicated adversarial tests before claiming parity.
- Group Sender Keys are rotated and redistributed for each send; synthetic roundtrips do not establish live service interoperability. PN/LID routing, linked-device copies, actual group delivery and receipts still require a real account test. Receipts currently mean at least one participant, not everyone.
- Group notification refresh queues are not persisted. Reconnect obtains summaries and opening a group requests current metadata, but complete offline event replay is not independently guaranteed. Summary absence is deliberately not treated as a departure or deletion.
- Full history recovery is constrained by what the primary sends. History name metadata has no independent freshness timestamp. Edited/deleted-message rendering and incoming unsupported payloads need further real-account validation.

## Regression coverage

C99: optional history archive presence/true/false; 100 summaries drained across the bounded event queue with unknown membership; group notification invalidation, own removal and ignoring an obsolete metadata IQ; existing encrypted group roundtrip and durable inbox tests.
Android instrumentation: a summary cannot undo departure; historical unarchive cannot overwrite a timestamped archive; bootstrap archive and full-membership confirmation. Existing creation success/error/timeout/disconnect regression remains enabled.
SQLite migration/merge/delete/archive checks run unchanged. Android compilation, lint, unit tests and emulator instrumentation run in GitHub Actions.

Protocol cross-check: https://github.com/WhiskeySockets/Baileys/blob/master/WAProto/WAProto.proto (Conversation.archived=16, GroupParticipant, SyncActionValue). This is an independent implementation/schema, not an official WhatsApp compatibility guarantee.
