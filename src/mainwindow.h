#pragma once

#include "midi.h"
#include "profile.h"
#include "protocol.h"

#include <QElapsedTimer>
#include <QMainWindow>
#include <QString>
#include <QStringList>
#include <QVector>

class QButtonGroup;
class QComboBox;
class QDialog;
class QFrame;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTimer;
class QWidget;
class DeviceWidget;
class StarWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    enum class SendPhase { Idle, AwaitingAck, Saving };
    enum class StatusState { Neutral, Busy, Success, Error };

    void buildUi();
    void buildMenus();
    QWidget *buildHeader();
    QFrame *buildProfileCard();
    QFrame *buildGlobalCard();
    QFrame *buildEditorCard();

    void refreshPorts(bool manual);
    void rebuildPortCombo();
    void openPort(const QString &portId, bool automatic);
    void handleDisconnected(const QString &reason);
    void updateConnectionUi();
    QString portDisplayName(const QString &portId) const;
    QString autoPortId() const;

    void readValues(bool automatic);
    void sendValues();
    void finishSend(bool success, const QString &detail, bool showDialog);
    void setTransferBusy(bool busy);
    void onSysExReceived(const QString &portId, const QByteArray &message);
    void onMidiLinkError(const QString &portId, const QString &error);
    void markDeviceConnected();

    void refreshAll();
    void refreshEditor();
    void setSelectedIndex(int index);
    void updateDeviceGraphic();
    void syncNoteEditors(int noteNumber);
    void rebuildNoteItems(int octaveIndex, int preferredPitch);
    void onTypeClicked(int messageType);

    void markProfileModified();
    void updateWindowTitle();
    void setActionStatus(const QString &text, StatusState state);

    bool openProfileFile();
    bool saveProfileFile(bool saveAs);
    void showAbout();
    void showMidiLog();
    void appendLogText(const QString &text);
    void logSysex(const QString &direction, const QByteArray &message);

    void updateNoteVisibility();
    void updateValueLabels();
    void updatePreview();
    void updateNameWarning(QLineEdit *edit);

    Profile m_profile = Profile::defaults();
    int m_selected = kDefaultSelectedIndex;

    DeviceWidget *m_device = nullptr;
    StarWidget *m_star = nullptr;
    QLabel *m_connectionTitle = nullptr;
    QLabel *m_connectionDetail = nullptr;
    QLabel *m_actionStatus = nullptr;
    QComboBox *m_portCombo = nullptr;
    QPushButton *m_rescanButton = nullptr;
    QPushButton *m_logButton = nullptr;

    QLineEdit *m_profileNameEdit = nullptr;
    QLineEdit *m_controlNameEdit = nullptr;
    QLabel *m_controlBadge = nullptr;
    QFrame *m_globalCard = nullptr;
    QFrame *m_editorCard = nullptr;

    QButtonGroup *m_typeGroup = nullptr;
    QPushButton *m_typeButtons[5] = {};

    QWidget *m_channelRow = nullptr;
    QWidget *m_numberRow = nullptr;
    QWidget *m_operationRow = nullptr;
    QWidget *m_noteRow = nullptr;
    QWidget *m_valuesRow = nullptr;
    QLabel *m_numberLabel = nullptr;
    QLabel *m_offLabel = nullptr;
    QLabel *m_onLabel = nullptr;
    QLabel *m_emptyLabel = nullptr;
    QLabel *m_previewLabel = nullptr;
    QComboBox *m_channelCombo = nullptr;
    QComboBox *m_operationCombo = nullptr;
    QComboBox *m_noteCombo = nullptr;
    QComboBox *m_octaveCombo = nullptr;
    QSpinBox *m_numberSpin = nullptr;
    QSpinBox *m_offSpin = nullptr;
    QSpinBox *m_onSpin = nullptr;

    QComboBox *m_patchDisplayCombo = nullptr;
    QComboBox *m_patchChannelCombo = nullptr;
    QComboBox *m_thruCombo = nullptr;
    QComboBox *m_clockCombo = nullptr;

    QPushButton *m_sendButton = nullptr;
    QPushButton *m_readButton = nullptr;

    QDialog *m_logDialog = nullptr;
    QPlainTextEdit *m_logView = nullptr;
    QStringList m_logHistory;

    MidiDevice *m_midi = nullptr;
    QVector<MidiPort> m_ports;
    QString m_currentPortId;
    QString m_lastAttemptedPort;
    QElapsedTimer m_autoAttemptTimer;
    QString m_currentFilePath;

    QTimer *m_scanTimer = nullptr;
    QTimer *m_readTimeout = nullptr;
    QTimer *m_sendTimeout = nullptr;

    Protocol::PresetAccumulator m_readAccumulator;

    bool m_updating = false;
    bool m_connected = false;
    bool m_modified = false;
    bool m_allowAutoRead = true;
    bool m_autoReadDone = false;
    bool m_waitingRead = false;
    bool m_readWasAuto = false;
    bool m_nameWarningActive = false;

    SendPhase m_sendPhase = SendPhase::Idle;
};
