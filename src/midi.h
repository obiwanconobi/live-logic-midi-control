#pragma once

#include <QByteArray>
#include <QMutex>
#include <QObject>
#include <QString>
#include <QVector>

#include <atomic>
#include <thread>

struct MidiPort
{
    QString id;        // ALSA name, e.g. "hw:1,0,0"
    QString name;      // display name
    QString cardName;  // ALSA card name
};

class MidiDevice : public QObject
{
    Q_OBJECT

public:
    explicit MidiDevice(QObject *parent = nullptr);
    ~MidiDevice() override;

    // Duplex raw-MIDI ports (both capture and playback streams present).
    static QVector<MidiPort> availablePorts();

    // Returns true if the port was opened. On failure, error contains the
    // ALSA error string.
    bool open(const QString &portId, QString *error = nullptr);
    void close();
    bool isOpen() const;
    QString portId() const { return m_portId; }

    // Sends one complete SysEx message. Thread-safe.
    bool send(const QByteArray &message, QString *error = nullptr);

signals:
    void sysexReceived(const QString &portId, const QByteArray &message);
    void linkError(const QString &portId, const QString &message);

private:
    void readLoop();

    void *m_input = nullptr;   // snd_rawmidi_t*
    void *m_output = nullptr;  // snd_rawmidi_t*
    QString m_portId;
    std::atomic_bool m_running{false};
    std::thread m_reader;
    QMutex m_writeMutex;
};
