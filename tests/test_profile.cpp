#include "profile.h"
#include "protocol.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDebug>
#include <QList>
#include <QString>

#include <cstdio>

namespace {

int failures = 0;

void check(bool condition, const char *what)
{
    if (condition) {
        std::printf("ok   %s\n", what);
    } else {
        std::printf("FAIL %s\n", what);
        ++failures;
    }
}

QByteArray framed(const QByteArray &body)
{
    QByteArray message;
    message.append(static_cast<char>(0xF0));
    message.append(body);
    message.append(static_cast<char>(0xF7));
    return message;
}

void testDefaults()
{
    const Profile profile = Profile::defaults();
    check(profile.name == "TEMPLATE", "default profile name");
    check(profile.controls[0].name == "VOLUME", "default EXP 1 name");
    check(profile.controls[1].name == "WAH", "default EXP 2 name");
    check(profile.controls[2].name == "CHORUS", "default SWITCH 1 name");
    check(profile.controls[7].name == "CLOCK", "default SWITCH 6 name");
    check(profile.controls[0].messageType == MessageTypes::Cc,
          "pedals normalise to CC");
    check(profile.controls[2].messageType == MessageTypes::Pc,
          "switches keep default PC");
}

void testPayloadLayout()
{
    Profile profile = Profile::defaults();
    profile.name = "MY PROFILE";
    profile.controls[2].name = "LEAD";
    profile.controls[2].messageType = MessageTypes::Cc;
    profile.controls[2].midiChannel = 3;
    profile.controls[2].messageNumber = 20;
    profile.controls[2].mode = 1;
    profile.controls[2].offMin = 5;
    profile.controls[2].onMax = 100;

    profile.controls[0].name = "VOL";
    profile.controls[0].messageType = MessageTypes::Cc;
    profile.controls[0].midiChannel = 16;
    profile.controls[0].messageNumber = 7;
    profile.controls[0].offMin = 0;
    profile.controls[0].onMax = 127;

    profile.patchModeDisplay = 1;
    profile.patchMidiChannel = 5;
    profile.midiThru = 1;
    profile.clockSelect = 1;

    const QByteArray payload = profile.toPayload();
    check(payload.size() == kPayloadSize, "payload is 198 bytes");
    check(payload.left(16) == QByteArray("MY PROFILE      "),
          "profile name is space padded");

    const QByteArray switch1 = payload.mid(0x10, 22);
    check(switch1.left(16) == QByteArray("LEAD            "),
          "SWITCH 1 name offset");
    check(static_cast<unsigned char>(switch1.at(0x10)) == 1, "SWITCH 1 message type");
    check(static_cast<unsigned char>(switch1.at(0x11)) == 3, "SWITCH 1 channel");
    check(static_cast<unsigned char>(switch1.at(0x12)) == 20, "SWITCH 1 number");
    check(static_cast<unsigned char>(switch1.at(0x13)) == 1, "SWITCH 1 mode");
    check(static_cast<unsigned char>(switch1.at(0x14)) == 5, "SWITCH 1 off value");
    check(static_cast<unsigned char>(switch1.at(0x15)) == 100, "SWITCH 1 on value");

    const QByteArray exp1 = payload.mid(0x94, 22);
    check(exp1.left(16) == QByteArray("VOL             "), "EXP 1 name offset");
    check(static_cast<unsigned char>(exp1.at(0x11)) == 16, "EXP 1 channel");

    check(static_cast<unsigned char>(payload.at(0xC0)) == 1, "patch mode display");
    check(static_cast<unsigned char>(payload.at(0xC1)) == 5, "patch MIDI channel");
    check(static_cast<unsigned char>(payload.at(0xC2)) == 0, "reserved byte 1");
    check(static_cast<unsigned char>(payload.at(0xC3)) == 0, "reserved byte 2");
    check(static_cast<unsigned char>(payload.at(0xC4)) == 1, "MIDI thru");
    check(static_cast<unsigned char>(payload.at(0xC5)) == 1, "clock select");

    Profile parsed;
    QString error;
    check(Profile::fromPayload(payload, &parsed, &error), "payload parses");
    check(parsed.name == "MY PROFILE", "profile name round trip");
    check(parsed.controls[2].name == "LEAD", "SWITCH 1 name round trip");
    check(parsed.controls[2].messageNumber == 20, "SWITCH 1 number round trip");
    check(parsed.controls[0].midiChannel == 16, "EXP 1 channel round trip");
    check(parsed.clockSelect == 1, "clock select round trip");

    check(Profile::fromPayload(QByteArray(197, '\0'), &parsed, &error) == false,
          "short payload rejected");
}

void testFileFormat()
{
    Profile profile = Profile::defaults();
    profile.name = "FILE TEST";
    profile.controls[4].name = "VERSE";
    profile.controls[4].messageNumber = 42;

    const QByteArray file = profile.toFileContents();
    check(file.size() == (kPayloadSize * 2 + kPayloadSize - 1),
          "file text has one space per byte");
    check(file.startsWith("46 49 4c 45"), "file text is lowercase spaced hex");

    QByteArray decorated = "  " + file.toUpper() + "\r\n";
    Profile parsed;
    QString error;
    check(Profile::fromFileContents(decorated, &parsed, &error),
          "file accepts uppercase and whitespace");
    check(parsed.name == "FILE TEST", "file profile name round trip");
    check(parsed.controls[4].name == "VERSE", "file control name round trip");

    check(Profile::fromFileContents("nope", &parsed, &error) == false,
          "invalid hex rejected");
    check(Profile::fromFileContents(QByteArray(394, '0'), &parsed, &error) == false,
          "wrong length file rejected");
}

void testNamesAndNotes()
{
    check(midiNoteName(0) == "C-1", "MIDI note 0 name");
    check(midiNoteName(60) == "C4", "MIDI note 60 name");
    check(midiNoteName(127) == "G9", "MIDI note 127 name");
    check(channelDisplayName(0) == "OMNI(ALL)", "OMNI display name");
    check(channelDisplayName(16) == "16", "channel 16 display name");

    const QString longName = QStringLiteral("1234567890123456");
    check(cleanName(longName, 16) == longName, "16-char name accepted");
    check(cleanName(longName + "X", 16) == longName, "17th char truncated");

    const QString accented = QStringLiteral("\u00e9\u00e9\u00e9\u00e9\u00e9\u00e9\u00e9\u00e9\u00e9");
    const QString clipped = cleanName(accented, 16);
    check(clipped.size() == 8, "UTF-8 truncation keeps whole characters");
    check(clipped.toUtf8().size() == 16, "UTF-8 truncation fills exactly 16 bytes");
}

void testCommands()
{
    check(Protocol::inquiryMessage().toHex().toUpper() == "F07E7F0601F7",
          "inquiry bytes");
    check(Protocol::readPresetMessage().toHex().toUpper()
              == "F000007401004D430100020000F7",
          "read bytes");
    check(Protocol::saveMessage().toHex().toUpper()
              == "F000007401004D43010005010001F7",
          "save bytes");

    Profile profile = Profile::defaults();
    const QByteArray load = Protocol::loadMessage(profile);
    check(load.size() == 212, "load message is 212 bytes");
    check(load.startsWith(QByteArray::fromHex("F000007401004D430100014601")),
          "load header");
    check(static_cast<unsigned char>(load.back()) == 0xF7, "load ends with F7");
    check(load.mid(13, kPayloadSize) == profile.toPayload(),
          "load contains payload");

    const QByteArray ack = QByteArray::fromHex("F000007401004D430100410000F7");
    check(Protocol::isLoadAck(ack), "load ack recognised");
    const QByteArray success = QByteArray::fromHex("F000007401004D43010045010001F7");
    check(Protocol::saveResult(success) == 1, "save success recognised");
    const QByteArray failure = QByteArray::fromHex("F000007401004D43010045010000F7");
    check(Protocol::saveResult(failure) == 0, "save failure recognised");
    const QByteArray update = QByteArray::fromHex("F000007401004D43010046010003F7");
    check(Protocol::controlUpdateIndex(update) == 3, "control update index");
    const QByteArray identity = QByteArray::fromHex("F07E7F060200007401000100F7");
    check(Protocol::isIdentityReply(identity), "identity reply recognised");
}

void testAccumulator()
{
    Profile profile = Profile::defaults();
    profile.name = "READ TEST";
    profile.controls[5].name = "BRIDGE";
    profile.controls[5].messageNumber = 33;
    const QByteArray payload = profile.toPayload();

    const QByteArray header = QByteArray::fromHex("00007401004D430100020000");
    const QByteArray trailer = QByteArray::fromHex("00");
    const QByteArray body = header + payload + trailer;
    const QByteArray full = framed(body);
    check(full.size() == 213, "read response is 213 bytes");

    {
        Protocol::PresetAccumulator accumulator;
        accumulator.addMessage(full);
        Profile parsed;
        QString error;
        check(accumulator.tryTake(&parsed, &error), "single-frame read parsed");
        check(parsed.name == "READ TEST", "single-frame profile name");
        check(parsed.controls[5].messageNumber == 33, "single-frame control value");
    }

    {
        Protocol::PresetAccumulator accumulator;
        const QList<int> chunks = {50, 50, 50, 50, 11};
        int offset = 0;
        for (const int size : chunks) {
            accumulator.addMessage(framed(body.mid(offset, size)));
            offset += size;
        }
        Profile parsed;
        QString error;
        check(accumulator.tryTake(&parsed, &error), "chunked read parsed");
        check(parsed.name == "READ TEST", "chunked profile name");
    }

    {
        Protocol::PresetAccumulator accumulator;
        accumulator.addMessage(framed(header));
        accumulator.addMessage(QByteArray::fromHex("F000007401004D43010046010003F7"));
        accumulator.addMessage(framed(payload.mid(0, 99)));
        accumulator.addMessage(framed(payload.mid(99)));
        Profile parsed;
        QString error;
        check(accumulator.tryTake(&parsed, &error), "echo plus chunks parsed");
        check(parsed.name == "READ TEST", "echo plus chunks profile name");
    }

    {
        Protocol::PresetAccumulator accumulator;
        accumulator.addMessage(framed(header));
        accumulator.addMessage(framed(payload.mid(0, 50)));
        Profile parsed;
        check(accumulator.tryTake(&parsed) == false, "incomplete read waits");
    }

    {
        Protocol::PresetAccumulator accumulator;
        accumulator.addMessage(Protocol::readPresetMessage());
        accumulator.addMessage(full);
        Profile parsed;
        QString error;
        check(accumulator.tryTake(&parsed, &error), "echoed command plus response parsed");
        check(parsed.name == "READ TEST", "echoed command profile name");
    }

    {
        Protocol::PresetAccumulator accumulator;
        accumulator.addMessage(framed(payload));
        Profile parsed;
        QString error;
        check(accumulator.tryTake(&parsed, &error), "headerless payload parsed");
        check(parsed.name == "READ TEST", "headerless profile name");
    }
}

void testHardwareReadResponse()
{
    // Captured from a real Live Logic. Note the response header ends in
    // 42 47 01, not the 02 00 00 form inferred in the protocol notes.
    const QByteArray response = QByteArray::fromHex(
        "F000007401004D43010042470154454D504C4154452020202020202020535749"
        "5443483120202020202020202001010101017F53574954434832202020202020"
        "20202001010201007F5357495443483320202020202020202001011001007F53"
        "57495443483420202020202020202001010A01007F5357495443483520202020"
        "202020202001011600007F535749544348362020202020202020200101150000"
        "7F504544414C312020202020202020202001010700007F504544414C32202020"
        "2020202020202001010800007F00010000010000F7");

    check(response.size() == 213, "hardware response is 213 bytes");
    check(Protocol::vendorCommand(response) == 0x42,
          "hardware response command is 0x42");

    {
        Protocol::PresetAccumulator accumulator;
        accumulator.addMessage(response);
        Profile parsed;
        QString error;
        check(accumulator.tryTake(&parsed, &error),
              "hardware response header recognised");
        check(parsed.name == "TEMPLATE", "hardware profile name");
        check(parsed.controls[2].name == "SWITCH1", "hardware SWITCH 1 name");
        check(parsed.controls[2].messageType == MessageTypes::Cc,
              "hardware SWITCH 1 message type");
        check(parsed.controls[2].midiChannel == 1, "hardware SWITCH 1 channel");
        check(parsed.controls[2].messageNumber == 1, "hardware SWITCH 1 number");
        check(parsed.controls[2].mode == 1, "hardware SWITCH 1 mode");
        check(parsed.controls[2].offMin == 1, "hardware SWITCH 1 off value");
        check(parsed.controls[2].onMax == 127, "hardware SWITCH 1 on value");
        check(parsed.controls[0].name == "PEDAL1", "hardware PEDAL 1 name");
        check(parsed.controls[0].messageNumber == 7,
              "hardware PEDAL 1 CC number");
        check(parsed.controls[0].onMax == 127, "hardware PEDAL 1 maximum");
        check(parsed.controls[1].name == "PEDAL2", "hardware PEDAL 2 name");
        check(parsed.controls[1].messageNumber == 8,
              "hardware PEDAL 2 CC number");
        check(parsed.patchMidiChannel == 1, "hardware patch MIDI channel");
        check(parsed.midiThru == 1, "hardware MIDI thru");
        check(parsed.clockSelect == 0, "hardware clock select");

        const QByteArray devicePayload = response.mid(13, kPayloadSize);
        check(parsed.toPayload() == devicePayload,
              "hardware payload round-trips byte for byte");
    }

    {
        Protocol::PresetAccumulator accumulator;
        const QByteArray body = response.mid(1, response.size() - 2);
        const QList<int> chunks = {47, 47, 47, 47, 23};
        int offset = 0;
        for (const int size : chunks) {
            accumulator.addMessage(framed(body.mid(offset, size)));
            offset += size;
        }
        Profile parsed;
        QString error;
        check(accumulator.tryTake(&parsed, &error),
              "hardware response accepted when chunked");
        check(parsed.controls[2].name == "SWITCH1",
              "chunked hardware SWITCH 1 name");
    }
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    testDefaults();
    testPayloadLayout();
    testFileFormat();
    testNamesAndNotes();
    testCommands();
    testAccumulator();
    testHardwareReadResponse();

    if (failures == 0)
        std::printf("\nAll tests passed.\n");
    else
        std::printf("\n%d test(s) failed.\n", failures);
    return failures == 0 ? 0 : 1;
}
