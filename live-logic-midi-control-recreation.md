# Live Logic MIDI Control — Linux Recreation Specification

Reverse-engineered from the macOS app **Live Logic MIDI Control** (Blackstar Amplification Ltd).

- Bundle id: `com.blackstar.LiveLogicMIDI`
- Version: `1.0.0.0`
- Binary: `Live Logic MIDI Control` (Mach-O 64-bit x86_64)
- SHA-256: `a2824502aabee0e6c7d4b9390cb97a59b653789324d574c9e9ce22f9a0fc33b3`

> **Method & caveats.** All findings are from static analysis (Objective-C metadata,
> disassembly, and NIB keyed archives). No live MIDI traffic was captured. Items marked
> **[inferred]** are reasoned from code paths, not observed at runtime. The exact device
> firmware packetization (Section 7.6) needs verification against real hardware.

> **Language-agnostic.** This spec describes behavior and data structures only. "Dictionary"
> = map/key-value object; "array" = ordered list; "string" = text. No language/framework
> is assumed. For MIDI I/O on Linux, ALSA sequencer (`snd_seq`) or JACK/raw MIDI is the
> natural replacement for CoreMIDI.

---

## 1. Product overview

A desktop editor for the **Live Logic MIDI Controller** foot controller. It:

1. Detects the hardware by enumerating MIDI destinations and matching the display name
   `Live Logic MIDI Controller`.
2. Edits a **profile**: a name, mappings for **6 footswitches** and **2 expression pedals**,
   and **global settings** (patch-mode display, patch MIDI channel, MIDI thru, clock select).
3. Talks to the device exclusively over **MIDI System Exclusive (SysEx)**.
4. Can **read** the device's current preset, **send** the edited profile to the device
   (which commits it to flash), and **save/open** profiles as local files.

There is **no** live MIDI output of CC/PC/Note while editing — everything is a SysEx
upload/download.

---

## 2. UI layout

The main window is organized into four sections (section header labels use spaced letters).

### 2.1 "PROFILE" section
| Element | Type | Purpose |
|---|---|---|
| `NAME` label + text field | text | profile name (max 16 chars) |
| `OPEN` button | button | open a `.bstarmidi` file |
| `SAVE` button | button | save profile to file (also `Cmd+S`) |

### 2.2 "CUSTOM MIDI MAP" section
| Element | Type | Purpose |
|---|---|---|
| `SWITCH 1`…`SWITCH 6` labels + text fields | text | custom control names for switches (max 16 chars each) |
| `EXP 1`, `EXP 2` labels + text fields | text | custom control names for pedals (max 16 chars each) |

These 8 buttons (`btnSwitch1`…`btnSwitch6`, `btnEXP1`, `btnEXP2`) are clickable and select
which control is currently being edited. The selected switch shows an LED image
(`livelogic_led_on.png`).

### 2.3 "GLOBAL" section
| Element | Type | Items |
|---|---|---|
| `PATCH MODE DISPLAY` | popup | `STANDARD 0-127`, `OFFSET 1-128` |
| `PATCH MODE MIDI CH.` | popup | `OMNI(ALL)`, `1`…`16` |
| `MIDI THRU` | popup | `OFF`, `ON` |
| `SELECT CLOCK` | popup | `INTERNAL CLOCK`, `EXTERNAL CLOCK` |
| `SEND VALUES` | button | upload + commit profile to device |
| `READ VALUES` | button | read current preset from device |

### 2.4 "MESSAGE TYPE" section
A tabbed area with five radio buttons (`PC`, `CC`, `NOTE`, `CLOCK`, `NONE`). Selecting one
shows the corresponding editor fields (Section 5). A connected/status star indicator
(`imvStarMark`) shows `livelogic_star_on.png` / `livelogic_star_off.png`.

---

## 3. Data model

### 3.1 Profile = one profile name + 8 control records + 5 global fields

Each of the 8 controls (SW1..SW6, EXP1, EXP2) is a dictionary with these exact keys:

| Key | Meaning | Values |
|---|---|---|
| `ProfileName` | shared profile name | string (≤16 chars) |
| `ControlName` | control's custom name | string (≤16 chars) |
| `MessageType` | message type | `"0"`…`"4"` (see 5.1) |
| `MidiChannel` | MIDI channel | `"0"`=OMNI(ALL), `"1"`…`"16"` |
| `MessageNumber` | program/CC/number/note | `"0"`…`"127"` |
| `Mode` | operation | `"0"`=MOMENTARY, `"1"`=TOGGLE |
| `OFF/Min` | off (switch) or min (pedal) value | `"0"`…`"127"` |
| `ON/Max` | on (switch) or max (pedal) value | `"0"`…`"127"` |

All values are stored as **strings** (decimal), not numbers. `MessageNumber` is the raw
0-based MIDI number (0–127); the `OFFSET 1-128` interpretation is a device-side display
mode only — the app never adds/subtracts 1 to it.

### 3.2 Global fields

| Key / UI | Values |
|---|---|
| `PATCHMODEDISPLAY` | `STANDARD 0-127` = 0, `OFFSET 1-128` = 1 |
| `PATCHMIDICHANNEL` | `OMNI(ALL)`=0, `1`…`16` |
| (reserved) | always `00 00` |
| `MIDITHRU` | `OFF`=0, `ON`=1 |
| `CLOCKSELECT` | `INTERNAL CLOCK`=0, `EXTERNAL CLOCK`=1 |

These are stored only as popup selection indices (0-based).

### 3.3 Defaults (fresh app / `createExpAndSwitchDictionary`)

`ProfileName` = `"TEMPLATE"`; all numeric fields = `"0"`. `ControlName` defaults:

| Control | Default name |
|---|---|
| EXP 1 | `VOLUME` |
| EXP 2 | `WAH` |
| SWITCH 1 | `CHORUS` |
| SWITCH 2 | `DIST` |
| SWITCH 3 | `LOOP` |
| SWITCH 4 | `A3` |
| SWITCH 5 | `D3` |
| SWITCH 6 | `CLOCK` |

### 3.4 Selected-control index

A global "currently selected control" index drives almost every edit action:

| Control | index |
|---|---|
| EXP1 | 0 |
| EXP2 | 1 |
| SW1 | 2 |
| SW2 | 3 |
| SW3 | 4 |
| SW4 | 5 |
| SW5 | 6 |
| SW6 | 7 |

---

## 4. Combo-box contents (exact)

| Popup | Items (in order) |
|---|---|
| MIDI channel (PC/CC/Note/Patch) | `OMNI(ALL)`, `1`, `2`, … `16` (17 items) |
| Operation (CC/Note) | `MOMENTARY`, `TOGGLE` |
| Patch mode display | `STANDARD 0-127`, `OFFSET 1-128` |
| MIDI thru | `OFF`, `ON` |
| Clock select | `INTERNAL CLOCK`, `EXTERNAL CLOCK` |
| Octave | `-1`, `0`, `1`, … `9` (11 items) |
| Note (pitch class) | `C`, `C#`, `D`, `D#`, `E`, `F`, `F#`, `G`, `G#`, `A`, `A#`, `B` |

Full note names (used for display / parsing): `C-1`, `C#-1`, … `G9` (128 names, MIDI
0–127).

---

## 5. Message types & per-type editing

### 5.1 Message type values

| Message type | Stored `MessageType` | Tab |
|---|---|---|
| PC | `0` | 0 |
| CC | `1` | 1 |
| Note | `2` | 2 |
| Clock | `3` | 3 |
| None | `4` | 4 |

The five buttons act as a radio group (exactly one `state=1`). Selecting one stores its
integer into the current control's `MessageType` and switches the tab.

### 5.2 Per-type fields

| Type | Editor fields |
|---|---|
| PC | MIDI channel, PROGRAM NUMBER (0–127) |
| CC | MIDI channel, CC MESSAGE NUMBER (0–127), OPERATION (MOMENTARY/TOGGLE), OFF VALUE, ON VALUE |
| Note | MIDI channel, OPERATION, NOTE + OCTAVE (→ note number), OFF VALUE, ON VALUE |
| Clock | (no fields) |
| None | (no fields) |

### 5.3 Switch vs pedal

- **Switch** (SW1–SW6): all five message types available; value labels show `OFF VALUE` /
  `ON VALUE`; operation popup enabled.
- **Pedal** (EXP1/EXP2): only `CC` and `None` available (`PC`, `NOTE`, `CLOCK` disabled +
  dimmed); value labels show `MIN VALUE` / `MAX VALUE`; operation popup disabled/emptied.

### 5.4 Note number formula

- Note number `N` (0–127) → pitch class `noteIndex = N mod 12`, octave `octIndex = N div 12`
  (octave popup index where 0 = "-1", 1 = "0", … 10 = "9").
- Inverse: `N = noteIndex + 12 * octaveIndex`.
- Octave `9` (index 10) limits the note popup to the first 8 pitch classes (`C`–`G`, since
  MIDI note 127 = G9); otherwise all 12.

### 5.5 Operation (`Mode`)

`Mode` is per-control: `0` = MOMENTARY, `1` = TOGGLE. Stored under key `Mode`. Used for
switch CC/Note messages. Pedals ignore it (popup disabled).

---

## 6. Value editing & validation

- Numeric fields (`MessageNumber`, `OFF/Min`, `ON/Max`): valid range **0–127**. Up/down
  steppers clamp to 0–127, step ±1. Invalid text (negative or ≥128) reverts to the last
  stored value.
- Name fields (`ProfileName`, each `ControlName`): max **16 characters**. If 17+ chars are
  entered, show alert `WARNING` / `The length of the input name is out of range!` and
  truncate to 16.
- `ProfileName` edits propagate to **all 8** control dictionaries; `ControlName` edits
  propagate only to that control's dictionary.
- MIDI channel popup stores the raw 0-based index (0 = OMNI, 1–16).

---

## 7. MIDI protocol

### 7.1 Connection lifecycle

1. A background timer runs every **0.1 s**. It enumerates MIDI destinations and looks for a
   display name containing `Live Logic MIDI Controller`.
2. On match it records the destination index (used as `portIndex`) and, if the MIDI
   "source added" flag is set, sends a **device inquiry** and marks the device ready.
3. A CoreMIDI notification callback sets the `connectionMidiFlag` byte to 1 when a MIDI
   **Source** is added and 0 when removed (the app logs `已插入MIDI设备` / `已拔出MIDI设备`).
4. On disconnect: dispose MIDI ports, clear flags, show notification `Device disconnected`,
   show the "star off" image.

MIDI setup (CoreMIDI equivalents): client name `Client`, input port `mdInport`, output port
`mdOutport`; both input and output ports connect to the single source chosen by `portIndex`.

### 7.2 SysEx framing & hex encoding

The app builds **hex strings** and converts them to bytes before sending. Conventions:

- Names → UTF-8 bytes → lowercase `%x` per byte (2 hex digits/byte).
- Numeric values → uppercase hex, minimum 2 digits (0–15 padded with leading `0`).
- `convertHexStrToData` / `convertDataToHexStr` and `stringFromHexString` perform the
  hex↔bytes↔string conversions (accepting/emitting lowercase).

### 7.3 Commands (app → device)

All vendor messages use prefix `F0 00 00 74 01 00 4D 43 01 00`; command byte is at offset 10.

| Purpose | Bytes |
|---|---|
| Device inquiry (universal) | `F0 7E 7F 06 01 F7` |
| Read preset | `F0 00 00 74 01 00 4D 43 01 00 02 00 00 F7` |
| **Load values (SEND VALUES)** | `F0 00 00 74 01 00 4D 43 01 00 01 46 01` + 198-byte payload + `F7` |
| Save/commit to flash | `F0 00 00 74 01 00 4D 43 01 00 05 01 00 01 F7` |

### 7.4 Responses (device → app)

Prefixes are verified, then dispatched on the command byte at offset 10.

| Command | Meaning | Condition / action |
|---|---|---|
| Identity `F0 7E 7F 06 02 00 00 74 01 00 01 00 … F7` | device identified | mark connected; on first connect send **read preset** |
| `41` | ack to load | if `[11]==0,[12]==0,[13]==F7` → send **save** command |
| `45` | save result | if `[11]==1,[12]==0,[14]==F7`; `[13]==1` → success, else failure |
| `46` | per-control index | if `[11]==1,[12]==0,[14]==F7`; `[13]` = control index 1–8 → refresh that control's UI |
| (other) | preset data chunks | accumulated (see 7.5) |

Result strings: success → alert `Send values to flash successfully!` (title `MESSAGE`);
failure → `Send values to flash failed!` (title `WARNING`).

### 7.5 Preset data layout (198-byte payload)

The **canonical payload** is 198 bytes (used by load-command, read-response, and file
format). Offsets below are relative to the payload start:

| Offset | Field |
|---|---|
| `0x00–0x0F` (16) | `ProfileName` (space-padded to 16 chars, UTF-8) |
| `0x10–0x25` | SWITCH 1 record (22 bytes) |
| `0x26–0x3B` | SWITCH 2 |
| `0x3C–0x51` | SWITCH 3 |
| `0x52–0x67` | SWITCH 4 |
| `0x68–0x7D` | SWITCH 5 |
| `0x7E–0x93` | SWITCH 6 |
| `0x94–0xA9` | EXP 1 |
| `0xAA–0xBF` | EXP 2 |
| `0xC0` | PATCH MODE DISPLAY index |
| `0xC1` | PATCH MIDI CHANNEL index |
| `0xC2–0xC3` | reserved `00 00` |
| `0xC4` | MIDI THRU index |
| `0xC5` | CLOCK SELECT index |

Each **control record** (22 bytes):

| Offset | Field |
|---|---|
| `0x00–0x0F` (16) | `ControlName` (space-padded to 16 chars, UTF-8) |
| `0x10` | `MessageType` (0–4) |
| `0x11` | `MidiChannel` (0–16) |
| `0x12` | `MessageNumber` (0–127) |
| `0x13` | `Mode` (0–1) |
| `0x14` | `OFF/Min` (0–127) |
| `0x15` | `ON/Max` (0–127) |

Wrapping:

- **Send (load)** message = `F0 00 00 74 01 00 4D 43 01 00 01 46 01` (13 bytes) + 198-byte
  payload + `F7` (1 byte) = **212 bytes**.
- **Read response** = same 13-byte header + 198-byte payload + 2-byte trailer
  (`0xD3–0xD4`, reserved) = **213 bytes**.
- **File** = 198-byte payload only.

### 7.6 Preset read chunking [inferred]

The device streams the read preset as SysEx chunks of **48 bytes ×4 then 21 bytes ×1**
(total 213 = 4×48 + 21). The app appends each 48/21-byte message to a buffer until it
reaches 213 bytes, then parses the layout above. The exact framing (and the read-callback's
special handling of 1-byte → `subdata{11,15}` and 3-byte → `subdata{13,15}` packets) is
device-firmware-specific and should be verified against hardware before finalizing the
Linux receive loop.

---

## 8. SEND VALUES flow (detailed)

1. Click `SEND VALUES` → `btnSendValues_Click:`.
2. If device not ready → alert `Unable to load data to device without connecting device!`
   (title) + `WARNING` (body), abort. (Note: strings are swapped with READ VALUES — a
   quirk of the original.)
3. Build the 212-byte load message (Section 7.3/7.5) from the current UI/dictionaries.
4. Send via `sendData:portIndex:` → `MIDISend`.
5. On device `41` ack → send the save/commit command (`…05 01 00 01 F7`).
6. On device `45` result → show success/failure alert (Section 7.4).

## 9. READ VALUES flow

1. Click `READ VALUES` → `btnReadValues_Click:`.
2. If device not ready → alert `Unable to send values to device without connecting device!`
   + `WARNING`, abort.
3. Send read-preset command (`…02 00 00 F7`), clear the preset buffer and receive-lock.
4. Accumulate 48/21-byte chunks into a 213-byte buffer, then parse into all 8 dictionaries
   + global popups + name text fields.
5. Re-select SWITCH 1 (refresh UI) when done.

---

## 10. File format (`.bstarmidi`)

- Save panel: prompt `Choose the path to save the data file`, default name
  `Untitled.bstarmidi`. Open panel filters extension `bstarmidi`.
- **Content**: the 198-byte payload (Section 7.5) encoded as a **hex string with a space
  inserted between every 2 hex characters**, written as UTF-8 text (no header, no
  trailer, no plist/XML).
  - Example shape: `54 45 4d 50 …` (hex of the 16-byte padded profile name, then each
    control record, then the 6 global bytes).
- **Read**: read file as UTF-8 string, strip all spaces, hex-decode to 198 bytes, parse by
  fixed offsets into dictionaries + UI.
- A dead/legacy plist writer exists (`saveDictionaryData:` with top-level keys `EXP.1`,
  `EXP.2`, `SWITCH1`…`SWITCH6`, `PATCHMODEDISPLAY`, `PATCHMIDICHANNEL`, `MIDITHRU`,
  `CLOCKSELECT`) but is **not** wired to any button; ignore it for recreation.

---

## 11. Notifications & status

- OS notifications (title `MESSAGE`, subtitle = status text):
  - `Device connected` (on first successful handshake)
  - `Device disconnected`
  - `Send values to flash successfully!`
  - `Send values to flash failed!`
- In-app alerts use `OK` button, informational style, shown as a sheet. `alertForm:message:`
  sets **message text = 2nd argument**, **informative text = 1st argument**.
- The star indicator image toggles `livelogic_star_on.png` / `livelogic_star_off.png`.

---

## 12. Other behaviors

- **Keyboard**: `Cmd+S` (also Shift+S) triggers Save; keyDown/keyUp tracked via local event
  monitor.
- **Window scaling**: font scale = window width / 1400; fonts use family `Jost*` at
  `13×scale` / `14×scale`. Continuous, no discrete breakpoints.
- **Styling** (cosmetic): labels have 1.7 px border `#2D2D2D` on background `#4B4B4B`;
  text boxes background `#141414`; view background black at 80% alpha. Not essential for
  function.

---

## 13. Appendix — evidence & reproduction

Key addresses (for verification against the binary):

| Item | Address |
|---|---|
| `btnSendValues_Click:` | `0x10000d42b` |
| `btnReadValues_Click:` | `0x10000d462` |
| `sendLoadCommand` (builds payload) | `0x100010f54` |
| `sendSaveCommand` | `0x100013727` |
| `sendReadPresetCommand` | `0x1000137d7` |
| `sendDeviceInquiryCommand` | `0x10000d3b0` |
| `handleReceiveMessage:` (parse) | `0x100004725` |
| `sendData:portIndex:` (MIDISend) | `0x1000227fc` |
| `MIDIManager initWithClientName:...` | `0x10002249a` |
| MIDINotifyProc (connect flag) | `0x100022640` |
| MIDIReadProc (packet extract) | `0x100022698` |
| `createExpAndSwitchDictionary` (defaults) | `0x100004222` |
| `saveStringData:` (file write) | `0x10000acaf` |
| `readStringConfigurationFileData:` | `0x100013f19` |
| `ToHex:` / `hexStringFromString:` / `convertHexStrToData:` | `0x100016da1` / `0x100016a99` / `0x100016be1` |
| `anaylseNoteAndOctaveDictionary:` | `0x10000ee68` |

Key string constants (address → value):

```
0x100030080  "Live Logic MIDI Controller"
0x10002fde0  "ControlName"
0x10002fe00  "MessageType"
0x10002fe20  "MidiChannel"
0x10002fe40  "MessageNumber"
0x10002fe60  "Mode"
0x10002fe80  "OFF/Min"
0x10002fea0  "ON/Max"
0x10002fda0  "ProfileName"
0x10002fd80  "TEMPLATE"        (default profile name)
0x10002fdc0  "VOLUME"          (default EXP1 name)
0x100031420  "f000007401004d430100014601"   (load header)
0x10002ffe0  "Send values to flash successfully!"
0x100030020  "Send values to flash failed!"
0x100030040  "WARNING"
0x100030000  "MESSAGE"
0x100030160  "Choose the path to save the data file"
0x100030180  "Untitled.bstarmidi"
0x1000303c0  "bstarmidi"       (file extension)
0x10002e5a0  0.1               (timer interval, double)
0x100030140  "Jost*"           (font family)
```

Reproduction commands used:

```bash
cd "/Applications/Live Logic MIDI Control.app/Contents/MacOS"
otool -oV "Live Logic MIDI Control"            # Objective-C classes/methods/ivars
otool -Iv "Live Logic MIDI Control"            # symbol/selector/stub bindings
otool -s __TEXT __cstring ...                  # cstrings
objdump -d --start-address=0x… --stop-address=0x… "Live Logic MIDI Control"   # disassembly
plutil -p "…/Resources/Base.lproj/Main.storyboardc/XfG-lQ-9wD-view-m2S-Jp-Qdl.nib"  # UI/NIB
```

---

## 14. Open items for hardware verification

- Exact device SysEx packetization for the 213-byte read response (48/21 chunking and the
  1-byte/3-byte receive-packet extraction windows).
- Whether the device expects the same hex-based (not raw binary) byte stream on the wire
  for Linux (the app converts its hex string to bytes, so the wire format is raw bytes).
- Any checksum/serial fields in the 2-byte read-response trailer (`0xD3–0xD4`).
