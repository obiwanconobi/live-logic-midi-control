#include "protocol.h"

namespace {

const QByteArray kVendorPrefix = QByteArray::fromHex("F000007401004D430100");
const QByteArray kIdentityPrefix = QByteArray::fromHex("F07E7F0602");

// Header bytes after the leading F0. The first form is documented as
// inferred in the protocol notes; the second is what the actual Live Logic
// returns (observed on hardware). Accept both.
const QByteArray kReadHeaderBodies[] = {
    QByteArray::fromHex("00007401004D430100020000"),
    QByteArray::fromHex("00007401004D430100424701"),
};

bool isCompleteSysEx(const QByteArray &message)
{
    return message.size() >= 2 && static_cast<unsigned char>(message.front()) == 0xF0
           && static_cast<unsigned char>(message.back()) == 0xF7;
}

} // namespace

namespace Protocol {

QByteArray inquiryMessage()
{
    return QByteArray::fromHex("F07E7F0601F7");
}

QByteArray readPresetMessage()
{
    return QByteArray::fromHex("F000007401004D430100020000F7");
}

QByteArray loadMessage(const Profile &profile)
{
    const QByteArray header = QByteArray::fromHex("F000007401004D430100014601");
    QByteArray message;
    message.reserve(header.size() + kPayloadSize + 1);
    message.append(header);
    message.append(profile.toPayload());
    message.append(static_cast<char>(0xF7));
    return message;
}

QByteArray saveMessage()
{
    return QByteArray::fromHex("F000007401004D43010005010001F7");
}

QString prettyHex(const QByteArray &bytes)
{
    return QString::fromLatin1(bytes.toHex(' ').toUpper());
}

bool isIdentityReply(const QByteArray &message)
{
    return message.size() >= kIdentityPrefix.size()
           && message.startsWith(kIdentityPrefix)
           && static_cast<unsigned char>(message.back()) == 0xF7;
}

bool isVendorMessage(const QByteArray &message)
{
    return message.size() >= kVendorPrefix.size()
           && message.startsWith(kVendorPrefix)
           && static_cast<unsigned char>(message.back()) == 0xF7;
}

int vendorCommand(const QByteArray &message)
{
    if (!isVendorMessage(message) || message.size() <= 10)
        return -1;
    return static_cast<unsigned char>(message.at(10));
}

bool isLoadAck(const QByteArray &message)
{
    return vendorCommand(message) == 0x41 && message.size() == 14
           && message.at(11) == 0 && message.at(12) == 0
           && static_cast<unsigned char>(message.at(13)) == 0xF7;
}

int saveResult(const QByteArray &message)
{
    if (vendorCommand(message) != 0x45 || message.size() != 15)
        return -1;
    if (message.at(11) != 1 || message.at(12) != 0
        || static_cast<unsigned char>(message.at(14)) != 0xF7) {
        return -1;
    }
    return message.at(13) == 1 ? 1 : 0;
}

int controlUpdateIndex(const QByteArray &message)
{
    if (vendorCommand(message) != 0x46 || message.size() != 15)
        return 0;
    if (message.at(11) != 1 || message.at(12) != 0
        || static_cast<unsigned char>(message.at(14)) != 0xF7) {
        return 0;
    }
    const int index = static_cast<unsigned char>(message.at(13));
    return index >= 1 && index <= 8 ? index : 0;
}

void PresetAccumulator::reset()
{
    m_data.clear();
}

void PresetAccumulator::addMessage(const QByteArray &completeSysEx)
{
    if (!isCompleteSysEx(completeSysEx))
        return;

    // Never let unrelated traffic pollute a pending read.
    if (isIdentityReply(completeSysEx))
        return;

    const int command = vendorCommand(completeSysEx);
    if (command == 0x41 || command == 0x45 || command == 0x46)
        return;

    // Remove only the framing bytes. Firmware variants seen in the original
    // app either stream one long frame or wrap each chunk separately; both
    // forms become the same byte stream here.
    m_data.append(completeSysEx.constData() + 1, completeSysEx.size() - 2);

    // Guard against an endless stream of unrelated data.
    if (m_data.size() > 8192)
        m_data = m_data.right(1024);
}

bool PresetAccumulator::tryTake(Profile *out, QString *error)
{
    if (!out)
        return false;

    // Different firmware revisions use different response command bytes.
    // Search all known variants and use the latest complete candidate. This
    // also tolerates a device that echoes the request before its response.
    int payloadStart = -1;
    for (const QByteArray &header : kReadHeaderBodies) {
        int position = -1;
        while ((position = m_data.indexOf(header, position + 1)) >= 0) {
            const int start = position + header.size();
            if (m_data.size() >= start + kPayloadSize)
                payloadStart = qMax(payloadStart, start);
        }
    }

    // Also accept a hypothetical complete frame that was appended with its F0.
    if (payloadStart < 0) {
        for (const QByteArray &header : kReadHeaderBodies) {
            QByteArray headerWithF0;
            headerWithF0.append(static_cast<char>(0xF0));
            headerWithF0.append(header);
            int position = -1;
            while ((position = m_data.indexOf(headerWithF0, position + 1)) >= 0) {
                const int start = position + headerWithF0.size();
                if (m_data.size() >= start + kPayloadSize)
                    payloadStart = qMax(payloadStart, start);
            }
        }
    }

    if (payloadStart >= 0) {
        if (m_data.size() < payloadStart + kPayloadSize)
            return false;
        const QByteArray payload = m_data.mid(payloadStart, kPayloadSize);
        QString parseError;
        if (!Profile::fromPayload(payload, out, &parseError)) {
            if (error)
                *error = parseError;
            return false;
        }
        m_data.clear();
        return true;
    }

    // A few firmware revisions may return just the 198 payload bytes.
    if (m_data.size() == kPayloadSize) {
        QString parseError;
        if (!Profile::fromPayload(m_data, out, &parseError)) {
            if (error)
                *error = parseError;
            return false;
        }
        m_data.clear();
        return true;
    }

    return false;
}

} // namespace Protocol
