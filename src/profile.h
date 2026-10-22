#pragma once

#include <QByteArray>
#include <QString>

#include <array>

inline constexpr int kControlCount = 8;
inline constexpr int kPayloadSize = 198;

// Message type values used by the device. These are ints (not an enum class)
// because they are stored directly in combo boxes and in the SysEx payload.
namespace MessageTypes {
inline constexpr int Pc = 0;
inline constexpr int Cc = 1;
inline constexpr int Note = 2;
inline constexpr int Clock = 3;
inline constexpr int None = 4;
} // namespace MessageTypes

// Selection indices match the original application:
//   0 = EXP 1, 1 = EXP 2, 2..7 = SWITCH 1..6
inline constexpr int kExp1Index = 0;
inline constexpr int kExp2Index = 1;
inline constexpr int kSwitch1Index = 2;
inline constexpr int kSwitch6Index = 7;
inline constexpr int kDefaultSelectedIndex = kSwitch1Index;

struct Control
{
    QString name;
    int messageType = MessageTypes::Pc;
    int midiChannel = 0;     // 0 = OMNI(ALL), 1..16
    int messageNumber = 0;   // program / CC number / MIDI note, 0..127
    int mode = 0;            // 0 = momentary, 1 = toggle
    int offMin = 0;          // off value (switch) / minimum (pedal), 0..127
    int onMax = 0;           // on value (switch) / maximum (pedal), 0..127
};

struct Profile
{
    QString name = QStringLiteral("TEMPLATE");
    std::array<Control, kControlCount> controls;
    int patchModeDisplay = 0;   // 0 = standard 0-127, 1 = offset 1-128
    int patchMidiChannel = 0;   // 0 = OMNI, 1..16
    int midiThru = 0;           // 0 = off, 1 = on
    int clockSelect = 0;        // 0 = internal, 1 = external

    static Profile defaults();

    // Clamps all values into the ranges accepted by the device, cleans names,
    // and makes sure expression pedals only use CC or NONE.
    void sanitize();

    // The canonical 198-byte device payload. Always returns exactly 198 bytes.
    QByteArray toPayload() const;

    // Parses exactly 198 bytes. Optionally reports a human-readable error.
    static bool fromPayload(const QByteArray &payload, Profile *out,
                            QString *error = nullptr);

    // The .bstarmidi file body: lowercase hex, one space between every byte.
    QByteArray toFileContents() const;

    // Accepts whitespace-separated hex (upper- or lower-case).
    static bool fromFileContents(const QByteArray &contents, Profile *out,
                                 QString *error = nullptr);
};

bool isPedalIndex(int index);
bool isSwitchIndex(int index);

// "EXP 1", "EXP 2", "SWITCH 1" ... "SWITCH 6"
QString controlKindName(int index);

// Short form used in the device graphic: "EXP 1", "SW 1"
QString controlShortName(int index);

QString defaultControlName(int index);

// Clips a string so that its UTF-8 encoding is at most maxBytes long without
// splitting a character. Also removes control characters and trims.
QString cleanName(const QString &name, int maxBytes = 16);

// "C-1" ... "G9"
QString midiNoteName(int note);

QString channelDisplayName(int channel);
QString messageTypeName(int messageType);

// Payload order is SWITCH 1..6, EXP 1, EXP 2. These helpers translate between
// that order and the UI selection indices above.
int payloadIndexOfSelection(int selectionIndex);
int selectionIndexForPayload(int payloadIndex);
