#pragma once

#include "profile.h"

#include <QByteArray>
#include <QString>

namespace Protocol {

// Universal non-real-time device inquiry.
QByteArray inquiryMessage();

// F0 00 00 74 01 00 4D 43 01 00 02 00 00 F7
QByteArray readPresetMessage();

// F0 00 00 74 01 00 4D 43 01 00 01 46 01 + 198-byte payload + F7
QByteArray loadMessage(const Profile &profile);

// F0 00 00 74 01 00 4D 43 01 00 05 01 00 01 F7
QByteArray saveMessage();

// "F0 00 00 ..." (upper-case, space separated)
QString prettyHex(const QByteArray &bytes);

bool isIdentityReply(const QByteArray &message);
bool isVendorMessage(const QByteArray &message);
int vendorCommand(const QByteArray &message);

// True for the device's 0x41 acknowledgement of a load message.
bool isLoadAck(const QByteArray &message);

// 1 for success, 0 for failure, -1 if this is not a save result.
int saveResult(const QByteArray &message);

// 1..8 for a per-control update notification, 0 if this is not one.
int controlUpdateIndex(const QByteArray &message);

// Collects an incoming preset response. The firmware's chunking is not fully
// verified in the original reverse-engineering notes, so this accepts any
// chunking as long as complete SysEx frames are fed in. It looks for the read
// response header and then extracts the 198-byte payload.
class PresetAccumulator
{
public:
    void reset();
    void addMessage(const QByteArray &completeSysEx);
    QByteArray buffer() const { return m_data; }
    bool tryTake(Profile *out, QString *error = nullptr);

private:
    QByteArray m_data;
};

} // namespace Protocol
