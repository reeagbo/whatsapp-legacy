# WhatsApp Legacy 0.29 — Protocol Dialogues by Operation

This document describes the exchanges implemented in the **0.29-debug baseline**: user trigger, protocol requests, payloads, replies, and local effects. It is an implementation reference, not a general description of WhatsApp.

**Notation:** C = this client; S = WhatsApp service; P = primary phone. XML below is a readable rendering of binary nodes, **not XML transmitted over HTTP**. Braces denote runtime values; binary contents are abbreviated. Protobuf field numbers below come from the current encoder/decoder. Optional fields and unrelated server children are omitted.

## 1. Common transport

**Implementation:** [MainActivity.kt](../app/src/main/java/org/reeagbo/whatsapplegacy/MainActivity.kt), [pairing.c](../core/pairing.c), [noise.c](../core/noise.c), [handshake.c](../core/handshake.c), [node.c](../core/node.c).

| Layer | Representation used |
|---|---|
| Connection | TLS WebSocket to `wss://web.whatsapp.com/ws/chat`; Origin `https://web.whatsapp.com` |
| Initial handshake | Protobuf Noise handshake messages |
| Noise suite | `Noise_XX_25519_AESGCM_SHA256` |
| WA framing | Initial header `57 41 06 03`; frames have a three-byte big-endian length |
| Authenticated transport | Binary node encoding encrypted with Noise transport keys |
| End-to-end content | Signal ciphertext inside `message/enc` |
| Downloaded history/state | Separate HTTPS download plus application-level integrity checks and decryption |

Every connection begins with:

| Step | Direction | Message / action |
|---|---|---|
| 1 | C → S | WebSocket opens; `wa_pair_start()` queues `ClientHello` with the ephemeral public key |
| 2 | S → C | `ServerHello`: ephemeral key, encrypted static key, encrypted certificate payload |
| 3 | C locally | Parse response; process Noise DH steps and validate server certificate |
| 4 | C → S | `ClientFinish`: encrypted client static material and `ClientPayload` |
| 5 | Both | Activate separate send/receive transport keys and counters |
| 6 | S → C | Pairing dialogue for a new companion, or `success` for an authenticated saved identity |

No client registration/login payload is sent if server authentication fails.

The host passes incoming WebSocket bytes to `wa_pair_feed()`. It drains `wa_pair_output()` and calls `wa_pair_output_done()` after handing off each buffer. These are transport operations, not delivery acknowledgements for user messages.

## 2. “Registration”: linking an existing account

**UI trigger:** enter the existing account's international number and request a linking code.

**Important distinction:** this implements companion-device enrollment. It does **not** implement registering a new number by SMS or voice call. The internal `wa_registration_payload()` name refers to the initial companion payload, not standalone account registration.

**Implementation:** `request_code()`, `handle_link()`, `pair_success()`, `handle_node()`, and `frame()` in [pairing.c](../core/pairing.c); [register.c](../core/register.c).

### 2.1 Initial companion payload

Without a saved device JID, `ClientFinish` contains the initial registration payload built from local registration ID, Signal identity public key, signed prekey public key/signature, and platform metadata.

After the service sends an IQ containing `pair-device`, the client answers that IQ and prepares the code-linking request.

### 2.2 Code exchange

The client generates an eight-character code **locally**, using five random bytes and the alphabet `0123456789ABCDEFGHJKMNPQRSTVWXYZ`. It also creates a temporary pairing key pair and wraps its public key using the code.

C → S:

```xml
<iq type="set" id="legacy-pair-1" xmlns="md" ...>
  <link_code_companion_reg jid="{number}@s.whatsapp.net"
      stage="companion_hello" should_show_push_notification="true">
    <link_code_pairing_wrapped_companion_ephemeral_pub>{80 bytes}</link_code_pairing_wrapped_companion_ephemeral_pub>
    <companion_server_auth_key_pub>{32-byte Noise public key}</companion_server_auth_key_pub>
    <companion_platform_id>1</companion_platform_id>
    <companion_platform_display>Chrome (Linux)</companion_platform_display>
    <link_code_pairing_nonce>0</link_code_pairing_nonce>
  </link_code_companion_reg>
</iq>
```

The UI exposes the generated code after the WebSocket has accepted the queued frame. An IQ `result` for `legacy-pair-1` is optional; it is not what generates the code and is not proof of successful linking.

The user enters the code on P through WhatsApp's linked-device flow. S then relays a notification containing:

- `link_code_pairing_ref`.
- `primary_identity_pub` — 32 bytes.
- `link_code_pairing_wrapped_primary_ephemeral_pub` — 80 bytes.

C unwraps and processes this material, derives the pairing result, and sends:

```xml
<iq type="set" id="legacy-finish-1" xmlns="md" ...>
  <link_code_companion_reg jid="{number}@s.whatsapp.net"
      stage="companion_finish">
    <link_code_pairing_wrapped_key_bundle>{156 bytes}</link_code_pairing_wrapped_key_bundle>
    <companion_identity_public>{32-byte Signal identity key}</companion_identity_public>
    <link_code_pairing_ref>{received reference}</link_code_pairing_ref>
  </link_code_companion_reg>
</iq>
```

The derived ADV secret is saved before this finish request is sent. Notifications are acknowledged with `ack class="notification"`, preserving their ID and type.

### 2.3 Account/device proof

S → C: IQ containing `pair-success/device-identity` and `pair-success/device@jid`.

C performs these checks before accepting the account:

1. Validate envelope and supported account/device types.
2. Verify account HMAC with the derived ADV secret.
3. Verify the primary account signature over the device details and companion identity.
4. Create the companion's device signature.
5. Save the verified device JID and account certificate.

C → S:

```xml
<iq type="result" id="{pair-success IQ id}" ...>
  <pair-device-sign>
    <device-identity key-index="{index}">{signed identity protobuf}</device-identity>
  </pair-device-sign>
</iq>
```

A subsequent `stream:error code="515"` is treated as the expected restart **only after the pairing proof has been produced**. The client reconnects using the saved identity. Linking is not considered an authenticated session until the new connection receives `success`.

The “Chrome (Linux)” label seen on the primary device comes directly from the companion metadata above.

## 3. Saved-session login and messaging activation

**Trigger:** application startup, reconnect, or restart after linking.

The common Noise handshake runs again. This time `login_payload()` encodes the saved account user/device identifiers and login flags alongside UserAgent/WebInfo metadata.

S → C: `success`, optionally including the account's LID.

The client opens the persistent Signal store and starts the following exchange:

| Step | C → S | Expected S → C / next action |
|---|---|---|
| 1 | IQ `get`, xmlns `encrypt`, ID `legacy-prekeys-count`, child `count` | IQ `result` with `count@value` |
| 2, when count ≤ 16 | IQ `set`, xmlns `encrypt`, ID `legacy-prekeys-upload` | IQ `result` |
| 3 | IQ `set`, xmlns `passive`, ID `legacy-active`, child `active` | IQ `result`; emit native event `ready` |

The upload contains registration ID, key type 5, identity public key, a list of **32 one-time prekeys**, and signed prekey `skey`. Generated private keys and the next-prekey counter are persisted locally.

Receiving `success` authenticates the transport session; receiving the activation result makes the messenger ready to send. Bootstrap failure or timeout must not be reported as messaging readiness.

## 4. Creating or opening a conversation

**UI:** `+` → “Nueva conversación” → international number and optional name → “Abrir”.

**Implementation:** `newConversation()`, `showChat()` in [MainActivity.kt](../app/src/main/java/org/reeagbo/whatsapplegacy/MainActivity.kt).

1. Strip supported formatting and validate 8–15 digits with a nonzero first digit.
2. Construct `{number}@s.whatsapp.net`.
3. Call `store.addChat(jid, name)`.
4. Open the local message screen.

**No create-conversation network message is sent.** A conversation is a local view of a peer. Device discovery and key establishment happen when sending. Empty conversation records are excluded from the normal list.

Contact permission and Android address-book lookup are local Android operations, not WhatsApp protocol requests.

## 5. Sending a text message

**Entry points:** `sendText()` → JNI → `wa_pair_send_text()` → `wa_messenger_send()`.

**Implementation:** `discover()`, `prepare_devices()`, `bundles()`, `transmit()` in [messaging.c](../core/messaging.c).

Preconditions include messenger readiness, no other pending send, valid peer, valid hexadecimal message ID, and 1–4,000 bytes of valid UTF-8.

### 5.1 Device discovery

C → S:

```xml
<iq type="get" id="{message-id}-usync" xmlns="usync" ...>
  <usync context="message" mode="query" sid="{message-id}-usync"
         last="true" index="0">
    <query><devices version="2"/><lid/></query>
    <list>
      <user jid="{peer PN JID}"/>
      <user jid="{own PN JID}"/>
    </list>
  </usync>
</iq>
```

For self-messaging the duplicate own-user entry is omitted.

S → C: matching IQ `result`, with `usync/list/user` entries, optional `lid@val`, and `devices/device-list/device` records.

C saves verified-format PN↔LID associations, selects recipient devices and own companion devices, and excludes its current device. The current implementation has a 32-device bound and rejects hosted devices.

### 5.2 Signal sessions

For devices without an existing Signal session, C sends:

```xml
<iq type="get" id="{message-id}-keys" xmlns="encrypt" ...>
  <key>
    <user jid="{device JID}"/>
  </key>
</iq>
```

S returns `list/user` bundles containing `registration`, `identity`, `skey` (ID, value, signature), and an optional one-time `key`.

C validates the bundle and establishes/persists the Signal session. A changed untrusted identity stops sending. If all sessions already exist, this IQ is skipped.

### 5.3 Message payload and envelope

For the recipient, the plaintext Protobuf is:

```text
Message
  field 1: conversation = UTF-8 text
```

For the sender's other devices:

```text
Message
  field 31: DeviceSentMessage
    field 1: destinationJid = peer PN JID
    field 2: message = the Message above
```

Each device payload receives random-length padding (1–16 repeated padding bytes) and is encrypted with that device's Signal session.

C → S:

```xml
<message id="{message-id}" to="{resolved route}" type="text">
  <participants>
    <to jid="{device JID}">
      <enc v="2" type="{pkmsg or msg}">{Signal ciphertext}</enc>
    </to>
  </participants>
  <device-identity>{account certificate, when required}</device-identity>
</message>
```

`pkmsg` carries a Signal prekey message; `msg` uses an established session. The device identity is attached when a prekey payload requires it. Failure to encrypt for every selected device prevents sending the stanza.

### 5.4 Acknowledgements and states

| Received / local condition | Native status |
|---|---|
| Discovery started | 0, pending |
| Encrypted stanza queued | 1, sent without server confirmation |
| `ack class="message" id="…"` without error | 2, server accepted |
| `receipt` with no type or type `delivery` | 3, delivered |
| `receipt type="read"` | 4, read |
| Server rejection | -1 |
| 45-second timeout after transmission | -2, uncertain outcome |

Receipts may include additional IDs under `list/item`. C sends `ack class="receipt"`. A `read-self` receipt is not interpreted as the recipient reading our message.

A `retry` receipt currently emits an error/status; automatic re-encryption retry remains pending. Timeout after sending is not proof of failure, so the client does not automatically duplicate the text.

## 6. Receiving text and own-device copies

**Implementation:** `incoming()`, `save_before_ratchet()`, `decode_text()` in [messaging.c](../core/messaging.c).

S → C: `message` with `from`, `id`, timestamp `t`, address metadata, and `enc type="pkmsg"` or `enc type="msg"`.

Processing:

1. Select the cryptographic sender, using `participant` when present.
2. Learn optional PN/LID associations from envelope metadata without overwriting conflicting mappings.
3. Decrypt through the sender-device Signal session.
4. Validate/remove padding and decode supported Protobuf content.
5. Save supported text to the durable inbox **inside the pre-ratchet-commit callback**.
6. Emit `message` or authenticated own-account `sent-copy`.
7. Android commits to SQLite, then calls `wa_pair_stored()` to release the inbox record.

Text decoding supports `Message.conversation` (field 1), `extendedTextMessage.text` (field 6 → 1), and own-device `DeviceSentMessage` (field 31).

C acknowledges the incoming stanza with `ack class="message"`. For successfully processed text it additionally emits `receipt to="{from}" id="{id}"`. History uses `type="hist_sync"`; app-state key sharing can use `type="peer_msg"`.

A stanza ACK alone is **not proof of supported content or successful decryption**: unsupported payloads can be acknowledged without a delivery receipt. Durable inbox/storage failure instead stops processing to avoid confirming lost text.

Received `notify` names are display metadata, learned after supported authenticated content; they are not proof of a telephone-number mapping.

## 7. Requesting and importing history

**Entry:** scrolling beyond local messages, or the application's history scheduling. Local pagination itself sends nothing.

**Implementation:** `wa_messenger_history()` in [messaging.c](../core/messaging.c); [history.c](../core/history.c); Android history worker.

### 7.1 Request to the primary

The request is a **Signal-encrypted peer message to our own primary device**, not an app-state IQ.

For one conversation, the encoder constructs:

```text
Message.field12 = ProtocolMessage
  field2 = 16                 // peer data operation request
  field16 = request
    field1 = 3                // history-on-demand operation
    field4 = history request
      field1 = conversation JID
      field2 = anchor message ID
      field3 = anchor fromMe
      field4 = 100            // requested count
      field5 = anchor timestamp supplied by host
```

A full-history request instead uses operation 6 and field 6, with request metadata and a configuration message. It is only a request: the primary may reject it.

Device discovery runs as for sending, but history mode selects only **our device 0**. The envelope is:

```xml
<message to="{own JID}" id="{request-id}" type="text"
         category="peer" push_priority="high_force">
  <enc v="2" type="{pkmsg or msg}">{encrypted ProtocolMessage}</enc>
  <meta appdata="default"/>
</message>
```

The receiver decodes the primary's operation response and emits `history-status`. Acceptance does not mean history has arrived; rejection code 4 is surfaced as unsupported full-history acquisition.

### 7.2 History delivery

An authenticated own-account `ProtocolMessage` carrying field 6 provides the history notification. The native code saves the download task before allowing Signal state progression.

Android obtains the relative path and downloads from `https://mmg.whatsapp.net{path}`, with Origin `https://web.whatsapp.com`.

The core derives media keys with HKDF-SHA256 and context `WhatsApp History Keys`, verifies applicable integrity metadata/MAC, decrypts AES-CBC content, and decompresses/decodes the history. Android imports events transactionally and only then acknowledges task completion.

Full-history availability, inactive-conversation recovery, and amount of returned data remain controlled by the primary/service.

## 8. Receiving archive, unarchive, and deletion from another instance

**Implementation:** `wa_messenger_node()`, `wa_messenger_tick()`, `wa_state_collect()`, `add_action()`, and history import.

S → C:

```xml
<notification type="server_sync" from="{server}" id="{id}">
  <collection name="{regular_low or regular_high}" .../>
</notification>
```

C acknowledges the notification and schedules the named collection. It also polls collections on a two-minute schedule.

C → S:

```xml
<iq to="s.whatsapp.net" type="set" id="legacy-state-{sequence}"
    xmlns="w:sync:app:state">
  <sync>
    <collection name="{collection}" version="{committed version}"
                return_snapshot="{true if initial/recovery, otherwise false}"/>
  </sync>
</iq>
```

S returns a collection with snapshot reference and/or patches. Additional pages are fetched when needed. Snapshots and external mutation references can require separate encrypted CDN downloads.

| Collection | Index / action decoded | Local result |
|---|---|---|
| `regular_low` | `["archive", jid]`; action field 17, operation field 1 boolean | Archive or unarchive |
| `regular_high` | `["deleteChat", jid, "1"]`; action field 22 | Delete conversation up to supplied range |
| `regular_high` | `["clearChat", jid, "1"]`; action field 21 | Clear stored messages in range |
| `regular_high` | `["deleteMessageForMe", jid, id, fromMe]`; action field 18 | Delete the identified local message |

App-state key material arrives in authenticated own-account `ProtocolMessage` field 7. If a referenced key is missing, `wa_messenger_sync_key()` sends a peer ProtocolMessage with type 7 and field 8 containing the requested key ID.

Records use HKDF context `WhatsApp Mutation Keys`, AES-256-CBC, index/value MACs, snapshot/patch MACs, and the 128-byte accumulated integrity hash. External state blobs use `WhatsApp App State Keys`. An unknown action is not permission to skip cryptographic integrity verification.

Only after successful decoding and SQLite application is the normal receive cursor committed. Removing app-state metadata is not itself interpreted as deleting a conversation.

## 9. Archiving, unarchiving, or deleting in this app

**UI:** long press or conversation options. Delete has no confirmation dialog.

**Entry:** `archiveChat()` / `deleteChat()` → queued `ChatChange` → JNI `nativeDeleteChat` → `wa_messenger_delete_chat()`. The historical “delete” function name handles all three operations.

| Host change value | Operation | Collection |
|---|---|---|
| 0 | Delete | `regular_high` |
| 1 | Archive | `regular_low` |
| 2 | Unarchive | `regular_low` |

The local database is updated immediately. The remote request requires a stored remote-message anchor: peer, message ID, direction, and time, plus the change timestamp. Without an anchor, only the local change happens.

The outgoing dialogue is:

1. C sends a forced snapshot query at version 0, ID `legacy-snap-{random token}`.
2. S returns snapshot and patches. C downloads external blobs, verifies integrity, and reconstructs the complete current collection.
3. If there are more pages, C saves the reconstructed write base and requests subsequent versions.
4. C builds one authenticated mutation and sends IQ ID `legacy-delete-{token}`, xmlns `w:sync:app:state`, containing `sync/collection/patch`.
5. S returns the matching IQ result or error.
6. C emits `delete-result` with operation name and result; accepted writes schedule normal incoming synchronization.

Mutation contents:

| Element | Archive/unarchive | Delete |
|---|---|---|
| Index | `["archive", jid]` | `["deleteChat", jid, "1"]` |
| SyncActionValue | Timestamp field 1; operation field 17 | Timestamp field 1; operation field 22 |
| Operation | Boolean field 1; message range field 2 | Message range field 1 |
| SyncActionData version field 4 | 3 | 6 |

The message range includes the anchor key (peer, fromMe, ID) and second-based timestamp. The action timestamp is supplied separately.

The writer encrypts the action, computes index/value MACs, replaces any prior value's contribution in the integrity hash, then computes the next snapshot and patch MACs. The collection attribute supplies the base version; integrity calculations use the next version.

**Write replay never commits the receive cursor.** Reconstructing a write base does not mean those incoming actions have been applied to SQLite.

The result is accepted only for a matching IQ `result` without top-level or collection errors. Snapshot/write processing has a 90-second deadline, renewed for pagination. Local changes are not rolled back on remote failure. Durable retries across application restart remain [pending](pending.md).

## 10. Message edits and revocations received

These are distinct from deleting a conversation.

In authenticated decrypted `Message.field12`:

| ProtocolMessage | Relevant fields | Local task |
|---|---|---|
| Type 0 | Target key in field 1 | Delete/revoke identified message |
| Type 14 | Target key field 1; replacement message field 14; timestamp field 15 | Edit identified message |

The callback validates the target/account relationship, saves a mutation task, and lets the history/import path apply it to the local store. This documents the receive path; it does not assert that an outgoing edit/revoke UI exists.

## 11. Keepalive, correlation, and failure boundaries

`wa_pair_ping()` sends IQ `get`, xmlns `w:p`, child `ping`, with ID `legacy-ping-{counter}`.

| Exchange | Correlation / timeout |
|---|---|
| Code linking | `legacy-pair-1`, `legacy-finish-1`, then server IQ IDs |
| Message discovery / bundles | `{message-id}-usync`, `{message-id}-keys`; 45-second pending stages |
| User message acceptance | Original message ID |
| Incoming state query | `legacy-state-{sequence}`; 30-second query timeout |
| Outgoing snapshot / patch | Shared random token in `legacy-snap-…` / `legacy-delete-…`; 90-second operation deadline |

JNI/native return success means an operation was accepted or queued at that boundary. It does not replace the corresponding server result, receipt, database commit, or task acknowledgement.

New-number SMS/voice registration and group-creation dialogues are **not implemented in this baseline** and therefore have no invented exchanges documented here.


### App-state projection recovery (0.39)

A durable `regular_low` receive version does not prove that the SQLite archive
and pin projections contain all records: older clients may have advanced the
version while ignoring unsupported actions. If `state-low-projection-v1` is
absent, the messenger requests version `0`, `return_snapshot=true`. Responses
are grouped using the authenticated snapshot/delta reconstruction path, with
request `legacy-rebuild-low`, dedicated slot 12 and `rebuild-base-low`. Each page
verifies mutation MACs, contiguous versions, patch MACs and the complete record
map hash. Intermediate pages emit no actions. The complete map must reach at
least the durable receive cursor before archive/pin actions are emitted; the
SQLite timestamp guards still apply. Only after SQLite commits does the native
acknowledgement persist the completion marker and the monotonic receive cursor.
A failed or interrupted recovery can restart without a new pairing or losing
the outgoing operations' separate snapshots.
