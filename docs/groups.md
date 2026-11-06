# Groups and contact selection — 0.34

## Local contact selection

The new-conversation form offers **Buscar en la agenda**. Android queries its
ContactsProvider locally by name or number, normalizes a selected telephone
number to E.164, and fills the existing number/name form. Search results are
bounded to 100; a more specific query narrows the results. Android permission
is requested if needed. Contacts with ambiguous/unconvertible local numbers
are excluded rather than assigned a guessed country prefix.

The same picker adds numbers to the group creation form. No address-book batch
is uploaded; only deliberately submitted recipients enter protocol requests.

## Group protocol operations

All operations below are C99 binary nodes inside the existing Noise channel.
The Kotlin/JNI boundary passes operation arguments and application events.

| Operation | IQ destination/type | Namespace and content |
|---|---|---|
| Participating groups | `@g.us`, `get` | `w:g2`, `participating` summary (no full rosters at startup) |
| Group metadata | group JID, `get` | `w:g2`, `query request="interactive"` |
| Create | `@g.us`, `set` | `w:g2`, `create subject="…" key="…"`, participant children |
| Leave | `@g.us`, `set` | `w:g2`, `leave/group id="…@g.us"` |
| Membership changes | incoming notification | `type="w:gp2"`; ACK and refresh participating groups |

Requests are correlated by IQ ID and expire after 45 seconds. Failed requests
are not reported as completed. Creation opens a local group only after a
successful metadata response. Self-removal and successful leave disable sending.

## Sending group text

1. Query current metadata; check our membership and announcement permissions.
2. Discover the current participants' devices through `usync`.
3. Obtain and verify missing pairwise Signal sessions through `encrypt/key` IQs.
4. Create a sender-key session for `(group, sender identity domain, device)`.
5. Encode text as `Message.conversation` (field 1), pad, and encrypt with the
   Signal group cipher.
6. Encode `Message.senderKeyDistributionMessage` (field 2): group ID in field 1,
   serialized distribution in field 2. Encrypt it separately to each device.
7. Send one group `message` containing `participants/to/enc` (`pkmsg`/`msg`) and
   a top-level `enc v="2" type="skmsg"` with the group ciphertext.

This initial implementation rotates our sender key for every outgoing message
and distributes it to all selected devices. This trades overhead for simple,
safe behaviour after membership changes. It does not reuse a key known to a
removed member. Missing devices/keys or a size bound fail the entire send.

The initial sending bounds are 64 members and 96 selected devices. Community
containers and hosted devices are not implemented. Announcement groups require
our account to have an administrator role. These are explicit limits, not
silent truncation.

## Receiving and persistence

Pairwise-encrypted distributions are processed before `skmsg`, even when their
order in a stanza is reversed. The group ID must match the enclosing group.
Signal validates the group message signature and ratchet. Supported plaintext
and its author are written to the durable inbox before ratchet commit. SQLite
commit is acknowledged separately as for individual messages.

Inbox format 3 adds the author and still reads formats 1 and 2. SQLite schema 7
adds group metadata and a separate author table, preserving existing message
rows. History import accepts group JIDs and the participant field of message keys.

Incoming bubbles show the author. A delivered/read receipt reflects at least one
participant; the UI does not claim that every member has received/read the text.

Deleting a group conversation removes local history and attempts the existing
app-state delete synchronization. **It does not leave the group.** Leaving is a
separate action in group information; it takes effect after server confirmation.
Archive/unarchive use the existing app-state path with group JIDs.

Participating-group metadata alone does not populate the conversation list with
empty groups. A newly created group is deliberately visible until deleted;
other groups become visible with locally stored messages.

## Validation and remaining work

Synthetic C99 tests cover create/metadata/leave IQs, membership discovery,
pairwise distribution plus group ciphertext, decoding, author durability,
tamper/duplicate rejection, and retry after a failed pre-commit storage callback.
Android smoke tests cover the picker, group names, authors, participant/admin
display, create validation, and conversation deletion. No automated test creates
or leaves a real group.

Live interoperability still needs user testing. Existing history availability
and missing sender-key recovery remain subject to the primary device/service.
Adding/removing members, changing the remote subject, invitations, communities,
and comprehensive administrator tools are not exposed in this initial UI.

Protocol shapes were checked against the Baileys source, especially
`src/Socket/groups.ts`, `src/Socket/messages-send.ts`, and
`src/Signal/libsignal.ts`. The implementation uses the bundled
libsignal-protocol-c group builder/cipher rather than reimplementing its crypto.

## Connection regression fixed in 0.34

The original binary-node budget (256 nodes, 16 attributes) was too small for
real group-list replies. Its rejection closed the authenticated connection.
The parser now allocates fixed-address chunks as needed, with bounded maxima
of 8,192 nodes and 32 attributes. Frame/decompression limits and authentication
checks remain enforced. Small replies do not allocate the maximum node table.

Startup requests group summaries. Full metadata is fetched when opening or
sending to a particular group. Large summary lists drain through the bounded
application event queue without silently dropping the remainder. User create,
leave and metadata requests can supersede a background refresh; its late reply
cannot complete the newer request. Create keys use random 32-character hex IDs.

Regression tests include a 320-participant encrypted reply followed by another
frame on the same session, a 100-group summary across queue boundaries, and a
create request overtaking a background refresh. These reproduce the code defect;
the user's exact disconnect cause still requires a current diagnostic log.
