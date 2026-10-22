#include "midi.h"

#include <QElapsedTimer>
#include <QMutexLocker>
#include <QSet>
#include <QThread>

#include <alsa/asoundlib.h>

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <exception>

namespace {

QString alsaError(int error)
{
    return QString::fromUtf8(snd_strerror(error));
}

QString safeString(const char *text)
{
    return text ? QString::fromUtf8(text) : QString();
}

} // namespace

MidiDevice::MidiDevice(QObject *parent)
    : QObject(parent)
{
}

MidiDevice::~MidiDevice()
{
    close();
}

QVector<MidiPort> MidiDevice::availablePorts()
{
    QVector<MidiPort> ports;
    QSet<QString> seen;

    int card = -1;
    while (snd_card_next(&card) >= 0 && card >= 0) {
        const QByteArray controlName =
            QByteArray("hw:") + QByteArray::number(card);
        snd_ctl_t *control = nullptr;
        if (snd_ctl_open(&control, controlName.constData(), 0) < 0)
            continue;

        QString cardName;
        char *rawCardName = nullptr;
        if (snd_card_get_name(card, &rawCardName) >= 0 && rawCardName) {
            cardName = QString::fromUtf8(rawCardName);
            std::free(rawCardName);
        }

        snd_rawmidi_info_t *info = nullptr;
        snd_rawmidi_info_alloca(&info);

        int device = -1;
        while (snd_ctl_rawmidi_next_device(control, &device) >= 0 && device >= 0) {
            snd_rawmidi_info_set_device(info, device);
            snd_rawmidi_info_set_subdevice(info, 0);
            snd_rawmidi_info_set_stream(info, SND_RAWMIDI_STREAM_INPUT);
            if (snd_ctl_rawmidi_info(control, info) < 0)
                continue;

            unsigned int subdeviceCount = snd_rawmidi_info_get_subdevices_count(info);
            if (subdeviceCount == 0)
                subdeviceCount = 1;

            for (unsigned int subdevice = 0; subdevice < subdeviceCount; ++subdevice) {
                snd_rawmidi_info_set_device(info, device);
                snd_rawmidi_info_set_subdevice(info, subdevice);
                snd_rawmidi_info_set_stream(info, SND_RAWMIDI_STREAM_INPUT);
                if (snd_ctl_rawmidi_info(control, info) < 0)
                    continue;

                const QString inputName = safeString(snd_rawmidi_info_get_name(info));
                const QString subdeviceName =
                    safeString(snd_rawmidi_info_get_subdevice_name(info));

                // Programming the controller needs both directions.
                snd_rawmidi_info_set_device(info, device);
                snd_rawmidi_info_set_subdevice(info, subdevice);
                snd_rawmidi_info_set_stream(info, SND_RAWMIDI_STREAM_OUTPUT);
                if (snd_ctl_rawmidi_info(control, info) < 0)
                    continue;

                const QString outputName = safeString(snd_rawmidi_info_get_name(info));

                MidiPort port;
                port.id = QStringLiteral("hw:%1,%2,%3")
                              .arg(card)
                              .arg(device)
                              .arg(subdevice);
                port.cardName = cardName;

                QString display = !inputName.isEmpty() ? inputName : outputName;
                if (display.isEmpty())
                    display = cardName;
                if (display.isEmpty())
                    display = QStringLiteral("MIDI port");
                if (!subdeviceName.isEmpty() && subdeviceName != display) {
                    const bool redundant =
                        !inputName.isEmpty()
                        && (subdeviceName.compare(inputName, Qt::CaseInsensitive)
                                == 0
                            || subdeviceName.startsWith(inputName,
                                                         Qt::CaseInsensitive));
                    if (!redundant)
                        display += QStringLiteral(" \u2014 ") + subdeviceName;
                }
                port.name = display;

                if (!seen.contains(port.id)) {
                    seen.insert(port.id);
                    ports.append(port);
                }
            }
        }

        snd_ctl_close(control);
    }

    std::sort(ports.begin(), ports.end(), [](const MidiPort &a, const MidiPort &b) {
        return a.name.localeAwareCompare(b.name) < 0;
    });
    return ports;
}

bool MidiDevice::open(const QString &portId, QString *error)
{
    close();

    if (portId.trimmed().isEmpty()) {
        if (error)
            *error = QStringLiteral("No MIDI port selected.");
        return false;
    }

    snd_rawmidi_t *input = nullptr;
    snd_rawmidi_t *output = nullptr;
    const QByteArray id = portId.toLatin1();

    int result = snd_rawmidi_open(&input, nullptr, id.constData(),
                                  SND_RAWMIDI_NONBLOCK);
    if (result < 0) {
        if (error) {
            *error = QStringLiteral("Could not open MIDI input %1: %2")
                         .arg(portId, alsaError(result));
        }
        return false;
    }

    result = snd_rawmidi_open(nullptr, &output, id.constData(),
                              SND_RAWMIDI_NONBLOCK);
    if (result < 0) {
        snd_rawmidi_close(input);
        if (error) {
            *error = QStringLiteral("Could not open MIDI output %1: %2")
                         .arg(portId, alsaError(result));
        }
        return false;
    }

    m_input = input;
    m_output = output;
    m_portId = portId;
    snd_rawmidi_drop(input);
    m_running.store(true);

    try {
        m_reader = std::thread(&MidiDevice::readLoop, this);
    } catch (const std::exception &exception) {
        m_running.store(false);
        snd_rawmidi_close(output);
        snd_rawmidi_close(input);
        m_input = nullptr;
        m_output = nullptr;
        m_portId.clear();
        if (error) {
            *error = QStringLiteral("Could not start the MIDI reader: %1")
                         .arg(QString::fromUtf8(exception.what()));
        }
        return false;
    }

    return true;
}

void MidiDevice::close()
{
    m_running.store(false);
    if (m_reader.joinable())
        m_reader.join();

    if (m_output) {
        snd_rawmidi_drain(static_cast<snd_rawmidi_t *>(m_output));
        snd_rawmidi_close(static_cast<snd_rawmidi_t *>(m_output));
        m_output = nullptr;
    }
    if (m_input) {
        snd_rawmidi_close(static_cast<snd_rawmidi_t *>(m_input));
        m_input = nullptr;
    }
    m_portId.clear();
}

bool MidiDevice::isOpen() const
{
    return m_input != nullptr && m_output != nullptr;
}

bool MidiDevice::send(const QByteArray &message, QString *error)
{
    if (!isOpen()) {
        if (error)
            *error = QStringLiteral("The MIDI port is not open.");
        return false;
    }

    QMutexLocker locker(&m_writeMutex);
    auto *output = static_cast<snd_rawmidi_t *>(m_output);
    const char *data = message.constData();
    int remaining = message.size();
    QElapsedTimer retryTimer;
    retryTimer.start();

    while (remaining > 0) {
        const ssize_t written = snd_rawmidi_write(output, data, remaining);
        if (written < 0) {
            if ((written == -EAGAIN || written == -EINTR)
                && retryTimer.elapsed() < 2000) {
                QThread::msleep(1);
                continue;
            }
            if (error) {
                *error = QStringLiteral("MIDI write failed: %1")
                             .arg(alsaError(static_cast<int>(written)));
            }
            return false;
        }
        if (written == 0) {
            if (retryTimer.elapsed() >= 2000) {
                if (error)
                    *error = QStringLiteral("MIDI write timed out.");
                return false;
            }
            QThread::msleep(1);
            continue;
        }
        data += written;
        remaining -= static_cast<int>(written);
    }

    // Best-effort flush. In non-blocking mode drain() can legitimately return
    // -EAGAIN while the USB endpoint is still consuming the message; the bytes
    // have been accepted either way, so this must not turn a successful send
    // into a failure.
    snd_rawmidi_drain(output);
    return true;
}

void MidiDevice::readLoop()
{
    auto *input = static_cast<snd_rawmidi_t *>(m_input);
    QByteArray readBuffer(4096, '\0');
    QByteArray sysex;
    bool inSysex = false;

    while (m_running.load()) {
        const ssize_t count =
            snd_rawmidi_read(input, readBuffer.data(), readBuffer.size());

        if (count < 0) {
            if (count == -EAGAIN || count == -EWOULDBLOCK) {
                QThread::msleep(2);
                continue;
            }
            if (count == -EINTR)
                continue;
            m_running.store(false);
            emit linkError(m_portId, QStringLiteral("MIDI read failed: %1")
                                        .arg(alsaError(static_cast<int>(count))));
            return;
        }
        if (count == 0) {
            QThread::msleep(2);
            continue;
        }

        for (ssize_t i = 0; i < count; ++i) {
            const unsigned char byte =
                static_cast<unsigned char>(readBuffer.at(static_cast<int>(i)));
            if (byte == 0xF0) {
                inSysex = true;
                sysex.clear();
                sysex.append(static_cast<char>(byte));
                continue;
            }
            if (!inSysex)
                continue;

            sysex.append(static_cast<char>(byte));
            if (byte == 0xF7) {
                inSysex = false;
                if (sysex.size() >= 2)
                    emit sysexReceived(m_portId, sysex);
                sysex.clear();
            } else if (sysex.size() > 65536) {
                // Malformed stream: wait for the next start-of-exclusive.
                inSysex = false;
                sysex.clear();
            }
        }
    }
}
