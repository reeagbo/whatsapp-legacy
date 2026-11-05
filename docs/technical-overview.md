# WhatsApp Legacy — Technical Overview

**Validated baseline: 0.29-debug**  
**Audience:** developers and technically interested readers  
**Scope:** the Android interface, its contracts with the portable core, and the behaviour validated so far.

“Validated” refers to the working baseline tested during development. It is not a claim of production readiness or complete WhatsApp compatibility. The application is an unofficial linked-device client; it requires an existing WhatsApp account on a primary device.

## 1. Purpose and boundaries

WhatsApp Legacy aims to provide basic text conversations on older devices while keeping the protocol implementation reusable across platforms. Android is the first host platform. The longer-term objective is broader compatibility, so presentation and operating-system services are kept apart from protocol and cryptographic processing.

The current Android application targets Android 5.0 / API 21 and later. That is the configured minimum for this implementation, not evidence that every device at that level has been tested. Other operating systems would need their own host integration and user interface.

The working scope is individual text conversations, linked-device operation, local history, contact resolution, and conversation archive and deletion synchronization. Creating groups, bulk conversation selection, multimedia, and registering a new telephone number are outside this baseline.

## 2. The user interface

The application uses native Android Views constructed in Kotlin. It does not embed the WhatsApp website. Its dark interface centres on a conversation list and a message screen; connection and diagnostic controls sit behind the menu.

### Conversation list

Each row contains an initial-based avatar, the resolved conversation name, a last-message preview, and a timestamp. Conversations without locally stored messages are excluded from the list.

The **+** action starts a conversation using an international telephone number and an optional local name. Opening a row displays its messages. A long press opens the conversation actions:

- Archive or unarchive.
- Edit the local display name.
- Delete the conversation.

Archived conversations have a separate list accessible from the main menu. They can be opened and unarchived there. Selection currently operates on one conversation at a time.

Deletion takes effect locally immediately, without a confirmation dialog or a success announcement. Archive changes are also applied locally first. Both actions then attempt synchronization with the linked account. A remote failure does not reverse the local action.

### Message screen

Incoming and outgoing text use separate bubble positions and colours. Outgoing messages display their progress through pending, sent without confirmation, accepted, delivered, and read states. An uncertain result remains distinguishable from confirmed delivery.

The composer supports multiline text. Ordinary emoji travel as Unicode text; their visual appearance depends on the device's fonts. This does not imply support for stickers or multimedia messages.

Sending requires an active connection, and the interface allows one pending send at a time. A successful submission is not treated as proof that the recipient received or read the message. The application does not blindly resend an uncertain message, which could create duplicates.

### History navigation

A conversation initially displays a window of locally stored messages. Scrolling upwards expands that window. When local history is exhausted, the application can request older messages from the primary device. It attempts to preserve the visible position when earlier messages are inserted.

Local pagination and remote history acquisition are separate operations. A request for history does not guarantee that the primary device will supply every old message or every inactive conversation. Complete account restoration is not a validated capability of this baseline.

### Linking, contacts, and diagnostics

The linking screen requests a device-linking code for an existing account. Once an identity has been saved, the normal flow attempts to reconnect automatically.

Contact access is requested through Android's permission system when needed; user consent is still required. Display names combine local overrides, permitted address-book data, received identity information, and telephone-number fallbacks.

WhatsApp telephone-number addresses and linked identities are distinct. An unresolved linked identity is shown as pending rather than being presented as a fabricated telephone number. Resolved identities can be merged without discarding their stored messages.

The menu contains linking information and a diagnostic panel with manual connection controls and log export. These support investigation without making manual connection management part of normal messaging.

## 3. Software interfaces and responsibilities

The implementation has three principal boundaries:

| Layer | Responsibilities | Main interface |
|---|---|---|
| Android application | Views, permissions, contacts, lifecycle, SQLite conversations, downloads, network scheduling | Kotlin application classes |
| JNI bridge | Convert arguments, byte buffers, native handles, and events between Kotlin and C | Native methods and bridge functions |
| C99 core | WhatsApp framing, Noise, Signal processing, message events, history decoding, authenticated app-state operations | C headers and session APIs |

The separation is practical rather than absolute. Android-specific presentation stays outside the protocol core, but existing native persistence helpers include POSIX assumptions. A port must review those dependencies instead of assuming that every native file compiles unchanged on every platform.

### Session and transport contract

The session API in [pairing.h](../core/pairing.h) coordinates linking, connection processing, messages, and synchronization.

The host supplies received binary data to the native session, drains its outbound data, and advances its timers. On Android, the host manages the WebSocket connection and scheduling. The core processes the WhatsApp transport and encrypted protocol content.

This boundary allows another platform to provide a different network implementation while retaining the protocol machinery. It also makes connection lifetime a host responsibility: the core cannot keep an Android process alive by itself.

### Message and event contract

The messaging interface in [messaging.h](../core/messaging.h) exposes bounded message events containing identifiers, peer addresses, text, timestamps, and state. Text is represented as UTF-8, with a current native limit of 4,000 bytes.

Native event consumption and durable application storage are different acknowledgements. Reading an event from the native queue does not prove that its corresponding SQLite transaction succeeded. The host acknowledges durable storage separately.

Delivery receipts update message state without intentionally moving it backwards. Server acceptance, recipient delivery, and recipient read acknowledgement remain separate facts.

### Storage contract

The core's [storage interface](../core/storage.h) provides key-value access through host-supplied callbacks. Android separately owns the SQLite model used for conversation lists and message rendering.

These stores serve different purposes: cryptographic and protocol state must survive reconnects, while the application database provides searchable, ordered conversation data. A port needs equivalents for both.

## 4. Receiving and storing messages

Receiving a message crosses several reliability boundaries:

1. The native session processes and decrypts the incoming protocol data.
2. Supported plaintext is retained in the native durable inbox.
3. Android applies the event in a SQLite transaction.
4. After that transaction succeeds, Android acknowledges durable storage to the core.

The inbox bridges a failure between protocol processing and application storage. Database deduplication also prevents repeated delivery or history import from creating ordinary duplicate messages.

Historical messages and live messages converge on the same local model. Identity resolution preserves their association when a linked identity is mapped to a telephone-number address.

## 5. Conversation synchronization

Archive and deletion use authenticated app-state synchronization. They are not just local display flags or ordinary outgoing text messages.

Incoming synchronization downloads the relevant state, authenticates and decodes snapshots and patches, and applies supported changes to the local database. Some patch content arrives through separate encrypted downloads and must be obtained before that patch can be processed completely.

Outgoing archive and deletion first update local state. The native implementation then reconstructs the current authenticated collection state, including paginated patches and externally stored mutations, before building the outgoing change.

Reconstructing state for a write must not advance the normal incoming cursor: those reconstructed actions have not necessarily been applied to SQLite. Normal incoming processing remains responsible for committing its own progress.

Deletion records a local cutoff so older history does not immediately recreate deleted content. Later new messages can still establish conversation activity. Archive timestamps help prevent stale synchronization data from undoing a newer local choice.

In the 0.29 baseline, user testing confirmed archive, unarchive, and deletion in both directions between this application and another linked instance. That validation covers the exercised flows; it does not establish every offline or concurrent-edit case.

**Remaining reliability work:** outgoing conversation changes need durable retry handling across process closure and restart. Local success should therefore not be interpreted as a guarantee that an offline remote change will eventually complete. See [pending work](pending.md).

## 6. Connection recovery and diagnostics

The Android host restores saved sessions and schedules reconnect attempts. Short initial retries give way to longer intervals; network changes can bring a retry forward. A stable connection resets the fast-retry budget.

Recovery operates while Android allows the application process to run. This baseline does not guarantee uninterrupted background connectivity under every manufacturer's power-management policy.

Diagnostic logs record connection stages, synchronization versions, operation results, and failures. Address hints can contain a domain and a partial suffix for correlation. Logs should therefore still be handled as potentially identifying diagnostic material.

The intended logging boundary excludes message bodies, full cryptographic keys, and pairing codes. Exported logs help distinguish transport failures, decryption problems, database failures, and app-state synchronization errors.

## 7. Validation and next work

Automated checks cover native components, database behaviour, selected Android logic, and application build checks. Live account testing remains necessary to validate interoperability with the remote service.

The next planned areas are multiple selection for archive, unarchive, and deletion, followed by group functionality. Neither is presented here as implemented.

The architectural direction remains a reusable C99 protocol core with platform-specific presentation and operating-system integration. Android 5.0 is today's implementation boundary; reaching older or different platforms requires explicit engineering and testing.
