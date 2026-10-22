#include "profile.h"

#include <QStringList>

#include <algorithm>
#include <cctype>

namespace {

constexpr int kNameLength = 16;
constexpr int kControlRecordSize = 22;
constexpr int kProfilesStart = 16;
constexpr int kGlobalStart = 0xC0;

// Payload block order: SWITCH 1..6, EXP 1, EXP 2.
constexpr int kPayloadOrder[kControlCount] = {2, 3, 4, 5, 6, 7, 0, 1};
constexpr int kSelectionOrder[kControlCount] = {6, 7, 0, 1, 2, 3, 4, 5};

const char *const kDefaultNames[kControlCount] = {
    "VOLUME", // EXP 1
    "WAH",    // EXP 2
    "CHORUS", // SWITCH 1
    "DIST",   // SWITCH 2
    "LOOP",   // SWITCH 3
    "A3",     // SWITCH 4
    "D3",     // SWITCH 5
    "CLOCK",  // SWITCH 6
};

const char *const kPitchClasses[12] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B",
};

int clampInt(int value, int lo, int hi)
{
    return std::clamp(value, lo, hi);
}

QByteArray paddedName(const QString &name)
{
    QByteArray bytes = cleanName(name, kNameLength).toUtf8();
    bytes.truncate(kNameLength);
    bytes.resize(kNameLength, ' ');
    return bytes;
}

QString nameFromField(const QByteArray &field)
{
    QByteArray raw = field.left(kNameLength);
    const int nul = raw.indexOf('\0');
    if (nul >= 0)
        raw.truncate(nul);
    return cleanName(QString::fromUtf8(raw), kNameLength);
}

bool fail(QString *error, const QString &message)
{
    if (error)
        *error = message;
    return false;
}

} // namespace

bool isPedalIndex(int index)
{
    return index == kExp1Index || index == kExp2Index;
}

bool isSwitchIndex(int index)
{
    return index >= kSwitch1Index && index <= kSwitch6Index;
}

QString controlKindName(int index)
{
    if (index == kExp1Index)
        return QStringLiteral("EXP 1");
    if (index == kExp2Index)
        return QStringLiteral("EXP 2");
    if (index >= kSwitch1Index && index <= kSwitch6Index)
        return QStringLiteral("SWITCH %1").arg(index - kSwitch1Index + 1);
    return QStringLiteral("CONTROL");
}

QString controlShortName(int index)
{
    if (index == kExp1Index)
        return QStringLiteral("EXP 1");
    if (index == kExp2Index)
        return QStringLiteral("EXP 2");
    if (index >= kSwitch1Index && index <= kSwitch6Index)
        return QStringLiteral("SW %1").arg(index - kSwitch1Index + 1);
    return QStringLiteral("CTRL");
}

QString defaultControlName(int index)
{
    if (index < 0 || index >= kControlCount)
        return {};
    return QString::fromLatin1(kDefaultNames[index]);
}

int payloadIndexOfSelection(int selectionIndex)
{
    if (selectionIndex < 0 || selectionIndex >= kControlCount)
        return -1;
    return kSelectionOrder[selectionIndex];
}

int selectionIndexForPayload(int payloadIndex)
{
    if (payloadIndex < 0 || payloadIndex >= kControlCount)
        return -1;
    return kPayloadOrder[payloadIndex];
}

QString cleanName(const QString &name, int maxBytes)
{
    QString cleaned;
    cleaned.reserve(name.size());
    for (const QChar ch : name) {
        const ushort code = ch.unicode();
        if (code < 0x20 || code == 0x7f)
            continue;
        cleaned.append(ch);
    }
    cleaned = cleaned.trimmed();

    if (maxBytes <= 0)
        return {};

    QByteArray encoded;
    QString result;
    for (int i = 0; i < cleaned.size();) {
        int length = 1;
        if (cleaned.at(i).isHighSurrogate() && i + 1 < cleaned.size()
            && cleaned.at(i + 1).isLowSurrogate()) {
            length = 2;
        }
        const QByteArray chunk = cleaned.mid(i, length).toUtf8();
        if (encoded.size() + chunk.size() > maxBytes)
            break;
        encoded.append(chunk);
        result.append(cleaned.mid(i, length));
        i += length;
    }
    return result;
}

QString midiNoteName(int note)
{
    note = clampInt(note, 0, 127);
    const int pitch = note % 12;
    const int octave = note / 12 - 1;
    return QString::fromLatin1(kPitchClasses[pitch]) + QString::number(octave);
}

QString channelDisplayName(int channel)
{
    if (channel <= 0)
        return QStringLiteral("OMNI(ALL)");
    return QString::number(clampInt(channel, 1, 16));
}

QString messageTypeName(int messageType)
{
    switch (messageType) {
    case MessageTypes::Pc:
        return QStringLiteral("PC");
    case MessageTypes::Cc:
        return QStringLiteral("CC");
    case MessageTypes::Note:
        return QStringLiteral("NOTE");
    case MessageTypes::Clock:
        return QStringLiteral("CLOCK");
    case MessageTypes::None:
        return QStringLiteral("NONE");
    default:
        return QStringLiteral("?");
    }
}

Profile Profile::defaults()
{
    Profile profile;
    profile.name = QStringLiteral("TEMPLATE");

    for (int i = 0; i < kControlCount; ++i) {
        Control &control = profile.controls[i];
        control = Control{};
        control.name = defaultControlName(i);
        control.messageType = MessageTypes::Pc;
        control.midiChannel = 0;
        control.messageNumber = 0;
        control.mode = 0;
        control.offMin = 0;
        control.onMax = 0;
    }

    profile.patchModeDisplay = 0;
    profile.patchMidiChannel = 0;
    profile.midiThru = 0;
    profile.clockSelect = 0;

    // The original stores 0 (PC) for every numeric default, but its UI only
    // offers CC/NONE for expression pedals. Normalise those to CC so the
    // profile is always representable and sendable.
    profile.sanitize();
    return profile;
}

void Profile::sanitize()
{
    name = cleanName(name, kNameLength);
    if (name.isEmpty())
        name = QStringLiteral("TEMPLATE");

    for (int i = 0; i < kControlCount; ++i) {
        Control &control = controls[i];
        control.name = cleanName(control.name, kNameLength);
        control.messageType = clampInt(control.messageType, MessageTypes::Pc,
                                       MessageTypes::None);

        if (isPedalIndex(i) && control.messageType != MessageTypes::Cc
            && control.messageType != MessageTypes::None) {
            control.messageType = MessageTypes::Cc;
        }

        control.midiChannel = clampInt(control.midiChannel, 0, 16);
        control.messageNumber = clampInt(control.messageNumber, 0, 127);
        control.mode = clampInt(control.mode, 0, 1);
        control.offMin = clampInt(control.offMin, 0, 127);
        control.onMax = clampInt(control.onMax, 0, 127);
    }

    patchModeDisplay = clampInt(patchModeDisplay, 0, 1);
    patchMidiChannel = clampInt(patchMidiChannel, 0, 16);
    midiThru = clampInt(midiThru, 0, 1);
    clockSelect = clampInt(clockSelect, 0, 1);
}

QByteArray Profile::toPayload() const
{
    QByteArray payload(kPayloadSize, '\0');

    const QByteArray profileName = paddedName(name);
    payload.replace(0, kNameLength, profileName);

    for (int block = 0; block < kControlCount; ++block) {
        const int selection = selectionIndexForPayload(block);
        const Control &control = controls[selection];
        const int offset = kProfilesStart + block * kControlRecordSize;

        payload.replace(offset, kNameLength, paddedName(control.name));
        payload[offset + 0x10] = static_cast<char>(
            clampInt(control.messageType, MessageTypes::Pc, MessageTypes::None));
        payload[offset + 0x11] = static_cast<char>(clampInt(control.midiChannel, 0, 16));
        payload[offset + 0x12] = static_cast<char>(clampInt(control.messageNumber, 0, 127));
        payload[offset + 0x13] = static_cast<char>(clampInt(control.mode, 0, 1));
        payload[offset + 0x14] = static_cast<char>(clampInt(control.offMin, 0, 127));
        payload[offset + 0x15] = static_cast<char>(clampInt(control.onMax, 0, 127));
    }

    payload[kGlobalStart + 0] = static_cast<char>(clampInt(patchModeDisplay, 0, 1));
    payload[kGlobalStart + 1] = static_cast<char>(clampInt(patchMidiChannel, 0, 16));
    payload[kGlobalStart + 2] = 0;
    payload[kGlobalStart + 3] = 0;
    payload[kGlobalStart + 4] = static_cast<char>(clampInt(midiThru, 0, 1));
    payload[kGlobalStart + 5] = static_cast<char>(clampInt(clockSelect, 0, 1));

    return payload;
}

bool Profile::fromPayload(const QByteArray &payload, Profile *out, QString *error)
{
    if (payload.size() != kPayloadSize) {
        return fail(error,
                    QStringLiteral("Expected a %1-byte profile, got %2 bytes.")
                        .arg(kPayloadSize)
                        .arg(payload.size()));
    }
    if (!out)
        return fail(error, QStringLiteral("No destination profile supplied."));

    Profile parsed;
    parsed.name = nameFromField(payload.left(kNameLength));

    for (int block = 0; block < kControlCount; ++block) {
        const int selection = selectionIndexForPayload(block);
        const int offset = kProfilesStart + block * kControlRecordSize;
        Control &control = parsed.controls[selection];
        control.name = nameFromField(payload.mid(offset, kNameLength));
        control.messageType = static_cast<unsigned char>(payload.at(offset + 0x10));
        control.midiChannel = static_cast<unsigned char>(payload.at(offset + 0x11));
        control.messageNumber = static_cast<unsigned char>(payload.at(offset + 0x12));
        control.mode = static_cast<unsigned char>(payload.at(offset + 0x13));
        control.offMin = static_cast<unsigned char>(payload.at(offset + 0x14));
        control.onMax = static_cast<unsigned char>(payload.at(offset + 0x15));
    }

    parsed.patchModeDisplay = static_cast<unsigned char>(payload.at(kGlobalStart + 0));
    parsed.patchMidiChannel = static_cast<unsigned char>(payload.at(kGlobalStart + 1));
    parsed.midiThru = static_cast<unsigned char>(payload.at(kGlobalStart + 4));
    parsed.clockSelect = static_cast<unsigned char>(payload.at(kGlobalStart + 5));

    parsed.sanitize();
    *out = parsed;
    return true;
}

QByteArray Profile::toFileContents() const
{
    return toPayload().toHex(' ');
}

bool Profile::fromFileContents(const QByteArray &contents, Profile *out,
                               QString *error)
{
    QByteArray text = contents;
    if (text.startsWith("\xEF\xBB\xBF"))
        text.remove(0, 3);

    QByteArray compact;
    compact.reserve(text.size());
    for (const char ch : text) {
        const unsigned char byte = static_cast<unsigned char>(ch);
        if (std::isspace(byte))
            continue;
        if (!std::isxdigit(byte)) {
            return fail(error,
                        QStringLiteral("The file contains a non-hex character: '%1'.")
                            .arg(QString(QChar(byte))));
        }
        compact.append(ch);
    }

    if (compact.size() != kPayloadSize * 2) {
        return fail(error,
                    QStringLiteral("Expected %1 hex characters (198 bytes), got %2.")
                        .arg(kPayloadSize * 2)
                        .arg(compact.size()));
    }

    const QByteArray payload = QByteArray::fromHex(compact);
    if (payload.size() != kPayloadSize) {
        return fail(error, QStringLiteral("The profile payload is invalid."));
    }

    return fromPayload(payload, out, error);
}
