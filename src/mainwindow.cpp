#include "mainwindow.h"

#include "devicewidget.h"
#include "widgets.h"

#include <QAction>
#include <QApplication>
#include <QButtonGroup>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QScreen>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QStatusBar>
#include <QStyle>
#include <QTime>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>

namespace {

QLabel *makeFieldLabel(const QString &text)
{
    auto *label = new QLabel(text);
    label->setObjectName(QStringLiteral("FieldLabel"));
    QFont font(QStringLiteral("Fira Sans"));
    font.setPixelSize(9);
    font.setWeight(QFont::DemiBold);
    font.setLetterSpacing(QFont::AbsoluteSpacing, 1.0);
    label->setFont(font);
    return label;
}

QLabel *makeTinyCaption(const QString &text)
{
    auto *label = new QLabel(text);
    label->setObjectName(QStringLiteral("TinyCaption"));
    QFont font(QStringLiteral("Fira Sans"));
    font.setPixelSize(8);
    font.setWeight(QFont::DemiBold);
    font.setLetterSpacing(QFont::AbsoluteSpacing, 1.5);
    label->setFont(font);
    return label;
}

QLabel *makeHintLabel(const QString &text)
{
    auto *label = new QLabel(text);
    label->setObjectName(QStringLiteral("HintLabel"));
    label->setWordWrap(true);
    return label;
}

QWidget *makeFormRow(const QString &labelText, QWidget *field,
                     QLabel **outLabel = nullptr, int labelWidth = 132)
{
    auto *row = new QWidget;
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    QLabel *label = makeFieldLabel(labelText);
    label->setFixedWidth(labelWidth);
    layout->addWidget(label);
    layout->addWidget(field);
    layout->addStretch(1);

    if (outLabel)
        *outLabel = label;
    return row;
}

QSpinBox *makeValueSpin()
{
    auto *spin = new QSpinBox;
    spin->setRange(0, 127);
    spin->setKeyboardTracking(false);
    spin->setCorrectionMode(QAbstractSpinBox::CorrectToPreviousValue);
    spin->setAccelerated(true);
    spin->setFixedWidth(88);
    spin->setAlignment(Qt::AlignRight);
    spin->setToolTip(QStringLiteral("Value range: 0\u2013127"));
    return spin;
}

void fillChannelCombo(QComboBox *combo)
{
    combo->addItem(QStringLiteral("OMNI(ALL)"));
    for (int channel = 1; channel <= 16; ++channel)
        combo->addItem(QString::number(channel));
}

void repolish(QWidget *widget)
{
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->update();
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("Live Logic MIDI Control"));
    setWindowIcon(QIcon(QStringLiteral(":/icons/live-logic-control.png")));
    setMinimumSize(1200, 640);

    QSize initialSize(1320, 920);
    if (QScreen *screen = QGuiApplication::primaryScreen()) {
        const QSize available = screen->availableSize();
        initialSize.setWidth(qBound(1200, available.width() - 80, 1320));
        initialSize.setHeight(qBound(640, available.height() - 80, 920));
    }
    resize(initialSize);

    m_midi = new MidiDevice(this);
    connect(m_midi, &MidiDevice::sysexReceived, this, &MainWindow::onSysExReceived);
    connect(m_midi, &MidiDevice::linkError, this, &MainWindow::onMidiLinkError);

    m_scanTimer = new QTimer(this);
    m_scanTimer->setInterval(1200);
    connect(m_scanTimer, &QTimer::timeout, this, [this] { refreshPorts(false); });

    m_readTimeout = new QTimer(this);
    m_readTimeout->setSingleShot(true);
    connect(m_readTimeout, &QTimer::timeout, this, [this] {
        if (!m_waitingRead)
            return;
        m_waitingRead = false;
        setTransferBusy(false);

        const int buffered = m_readAccumulator.buffer().size();
        const QString text =
            buffered > 0
                ? tr("The device returned %1 bytes, but no complete 198-byte "
                     "profile could be decoded. Open the MIDI Log for details.")
                      .arg(buffered)
                : tr("The device did not return a preset. Check the MIDI port "
                     "and try READ VALUES again.");
        setActionStatus(text, StatusState::Error);
        statusBar()->showMessage(text);
        appendLogText(text);
        if (!m_readWasAuto) {
            QMessageBox::warning(this, QStringLiteral("WARNING"), text);
        }
    });

    m_sendTimeout = new QTimer(this);
    m_sendTimeout->setSingleShot(true);
    connect(m_sendTimeout, &QTimer::timeout, this, [this] {
        if (m_sendPhase == SendPhase::AwaitingAck) {
            finishSend(false,
                       tr("The device did not acknowledge the profile. Nothing "
                          "was saved."),
                       true);
        } else if (m_sendPhase == SendPhase::Saving) {
            finishSend(false,
                       tr("The device did not confirm the flash save. Check the "
                          "controller before sending again."),
                       true);
        }
    });

    buildUi();
    buildMenus();

    statusBar()->showMessage(tr("Scanning for MIDI devices\u2026"));
    statusBar()->addPermanentWidget(makeMutedLabel(tr("ALSA raw MIDI")));

    refreshAll();
    setActionStatus(
        tr("Connect the Live Logic controller, then read or send a profile."),
        StatusState::Neutral);
    updateConnectionUi();
    updateWindowTitle();

    QTimer::singleShot(150, this, [this] {
        refreshPorts(true);
        m_scanTimer->start();
    });
}

MainWindow::~MainWindow()
{
    if (m_midi)
        m_midi->close();
}

void MainWindow::buildUi()
{
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    auto *content = new QWidget;
    content->setObjectName(QStringLiteral("Content"));
    auto *root = new QVBoxLayout(content);
    root->setContentsMargins(22, 16, 22, 18);
    root->setSpacing(14);

    root->addWidget(buildHeader());

    m_device = new DeviceWidget(this);
    m_device->setMaximumHeight(370);
    connect(m_device, &DeviceWidget::controlSelected, this,
            &MainWindow::setSelectedIndex);
    root->addWidget(m_device);

    auto *bottom = new QHBoxLayout;
    bottom->setSpacing(14);
    bottom->addWidget(buildProfileCard(), 0);
    m_globalCard = buildGlobalCard();
    bottom->addWidget(m_globalCard, 0);
    m_editorCard = buildEditorCard();
    bottom->addWidget(m_editorCard, 1);
    root->addLayout(bottom, 1);

    scroll->setWidget(content);
    setCentralWidget(scroll);
}

QWidget *MainWindow::buildHeader()
{
    auto *header = new QWidget;
    auto *layout = new QHBoxLayout(header);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(18);

    auto *accent = new QFrame;
    accent->setObjectName(QStringLiteral("AccentBar"));
    accent->setFixedSize(3, 38);
    layout->addWidget(accent, 0, Qt::AlignVCenter);

    auto *brandLayout = new QVBoxLayout;
    brandLayout->setContentsMargins(0, 0, 0, 0);
    brandLayout->setSpacing(1);

    auto *title = new QLabel(QStringLiteral("LIVE LOGIC"));
    QFont titleFont(QStringLiteral("Fira Sans"));
    titleFont.setPixelSize(21);
    titleFont.setWeight(QFont::Black);
    titleFont.setLetterSpacing(QFont::AbsoluteSpacing, 5.0);
    title->setFont(titleFont);
    brandLayout->addWidget(title);

    auto *subtitle = new QLabel(QStringLiteral("MIDI CONTROL  \u00b7  LINUX"));
    QFont subtitleFont(QStringLiteral("Fira Sans"));
    subtitleFont.setPixelSize(8);
    subtitleFont.setWeight(QFont::DemiBold);
    subtitleFont.setLetterSpacing(QFont::AbsoluteSpacing, 2.4);
    subtitle->setFont(subtitleFont);
    subtitle->setObjectName(QStringLiteral("MutedLabel"));
    brandLayout->addWidget(subtitle);

    layout->addLayout(brandLayout);
    layout->addStretch(1);

    m_star = new StarWidget;
    m_star->setDiameter(25);
    layout->addWidget(m_star, 0, Qt::AlignVCenter);

    auto *connectionLayout = new QVBoxLayout;
    connectionLayout->setContentsMargins(0, 0, 0, 0);
    connectionLayout->setSpacing(0);

    m_connectionTitle = new QLabel(QStringLiteral("NO DEVICE"));
    m_connectionTitle->setObjectName(QStringLiteral("ConnectionTitle"));
    QFont connectionFont(QStringLiteral("Fira Sans"));
    connectionFont.setPixelSize(9);
    connectionFont.setWeight(QFont::DemiBold);
    connectionFont.setLetterSpacing(QFont::AbsoluteSpacing, 1.6);
    m_connectionTitle->setFont(connectionFont);
    connectionLayout->addWidget(m_connectionTitle);

    m_connectionDetail = makeMutedLabel(tr("Connect the Live Logic via USB"));
    m_connectionDetail->setMaximumWidth(300);
    QFont detailFont(QStringLiteral("Fira Sans"));
    detailFont.setPixelSize(11);
    m_connectionDetail->setFont(detailFont);
    connectionLayout->addWidget(m_connectionDetail);
    layout->addLayout(connectionLayout);

    auto *portLayout = new QVBoxLayout;
    portLayout->setContentsMargins(0, 0, 0, 0);
    portLayout->setSpacing(3);
    portLayout->addWidget(makeTinyCaption(QStringLiteral("MIDI PORT")));

    auto *portRow = new QHBoxLayout;
    portRow->setContentsMargins(0, 0, 0, 0);
    portRow->setSpacing(6);

    m_portCombo = new QComboBox;
    m_portCombo->setMinimumWidth(235);
    m_portCombo->setMaximumWidth(300);
    m_portCombo->setSizeAdjustPolicy(
        QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_portCombo->setMinimumContentsLength(20);
    m_portCombo->addItem(tr("Scanning for MIDI devices\u2026"));
    m_portCombo->setEnabled(false);
    m_portCombo->setToolTip(
        tr("ALSA raw MIDI ports. The Live Logic is detected automatically."));
    connect(m_portCombo, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (m_updating || index < 0)
            return;
        const QString portId = m_portCombo->itemData(index).toString();
        if (portId.isEmpty())
            return;
        if (m_currentPortId == portId && m_midi->isOpen())
            return;
        openPort(portId, false);
    });
    portRow->addWidget(m_portCombo);

    m_rescanButton = new QPushButton(tr("RESCAN"));
    m_rescanButton->setObjectName(QStringLiteral("GhostButton"));
    m_rescanButton->setCursor(Qt::PointingHandCursor);
    m_rescanButton->setToolTip(tr("Rescan ALSA MIDI ports (Ctrl+R)"));
    connect(m_rescanButton, &QPushButton::clicked, this, [this] {
        m_lastAttemptedPort.clear();
        m_autoAttemptTimer.invalidate();
        refreshPorts(true);
    });
    portRow->addWidget(m_rescanButton);

    m_logButton = new QPushButton(tr("MIDI LOG"));
    m_logButton->setObjectName(QStringLiteral("GhostButton"));
    m_logButton->setCursor(Qt::PointingHandCursor);
    m_logButton->setToolTip(tr("View incoming and outgoing SysEx (Ctrl+L)"));
    connect(m_logButton, &QPushButton::clicked, this, &MainWindow::showMidiLog);
    portRow->addWidget(m_logButton);

    portLayout->addLayout(portRow);
    layout->addLayout(portLayout);

    return header;
}

QFrame *MainWindow::buildProfileCard()
{
    QFrame *card = makeCard();
    card->setFixedWidth(330);
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(18, 16, 18, 16);
    layout->setSpacing(10);

    layout->addWidget(makeSectionLabel(QStringLiteral("PROFILE")));

    auto *nameCaption = new QHBoxLayout;
    nameCaption->setContentsMargins(0, 0, 0, 0);
    nameCaption->addWidget(makeTinyCaption(QStringLiteral("NAME")));
    nameCaption->addStretch(1);
    nameCaption->addWidget(makeTinyCaption(QStringLiteral("16 CHARS MAX")));
    layout->addLayout(nameCaption);

    m_profileNameEdit = new QLineEdit(m_profile.name);
    m_profileNameEdit->setMaxLength(64);
    m_profileNameEdit->setPlaceholderText(QStringLiteral("TEMPLATE"));
    m_profileNameEdit->setToolTip(
        tr("Profile name, up to 16 characters / 16 UTF-8 bytes"));
    QFont nameFont(QStringLiteral("Fira Sans"));
    nameFont.setPixelSize(15);
    nameFont.setWeight(QFont::DemiBold);
    m_profileNameEdit->setFont(nameFont);
    m_profileNameEdit->setMinimumHeight(34);
    connect(m_profileNameEdit, &QLineEdit::textEdited, this, [this] {
        if (m_updating)
            return;
        updateNameWarning(m_profileNameEdit);
        m_profile.name = m_profileNameEdit->text();
        markProfileModified();
        updateDeviceGraphic();
    });
    layout->addWidget(m_profileNameEdit);

    auto *fileRow = new QHBoxLayout;
    fileRow->setContentsMargins(0, 0, 0, 0);
    fileRow->setSpacing(8);

    auto *openButton = new QPushButton(tr("OPEN"));
    openButton->setObjectName(QStringLiteral("GhostButton"));
    openButton->setCursor(Qt::PointingHandCursor);
    openButton->setToolTip(tr("Open a .bstarmidi profile (Ctrl+O)"));
    connect(openButton, &QPushButton::clicked, this, [this] {
        openProfileFile();
    });
    fileRow->addWidget(openButton);

    auto *saveButton = new QPushButton(tr("SAVE"));
    saveButton->setObjectName(QStringLiteral("GhostButton"));
    saveButton->setCursor(Qt::PointingHandCursor);
    saveButton->setToolTip(tr("Save this profile to a file (Ctrl+S)"));
    connect(saveButton, &QPushButton::clicked, this, [this] {
        saveProfileFile(false);
    });
    fileRow->addWidget(saveButton);
    layout->addLayout(fileRow);

    layout->addWidget(makeDivider());
    layout->addWidget(makeSectionLabel(QStringLiteral("DEVICE SYNC")));

    m_sendButton = new QPushButton(tr("SEND VALUES"));
    m_sendButton->setObjectName(QStringLiteral("PrimaryButton"));
    m_sendButton->setCursor(Qt::PointingHandCursor);
    m_sendButton->setMinimumHeight(38);
    m_sendButton->setToolTip(
        tr("Upload the profile and commit it to the device flash (Ctrl+Enter)"));
    connect(m_sendButton, &QPushButton::clicked, this, &MainWindow::sendValues);
    layout->addWidget(m_sendButton);

    m_readButton = new QPushButton(tr("READ VALUES"));
    m_readButton->setObjectName(QStringLiteral("ReadButton"));
    m_readButton->setCursor(Qt::PointingHandCursor);
    m_readButton->setMinimumHeight(38);
    m_readButton->setToolTip(
        tr("Read the current preset from the device (F5)"));
    connect(m_readButton, &QPushButton::clicked, this,
            [this] { readValues(false); });
    layout->addWidget(m_readButton);

    layout->addWidget(makeHintLabel(
        tr("SEND writes the profile to the controller's flash. READ replaces "
           "the editor with the preset currently in the device.")));

    m_actionStatus = new QLabel;
    m_actionStatus->setObjectName(QStringLiteral("ActionStatus"));
    m_actionStatus->setWordWrap(true);
    m_actionStatus->setMinimumHeight(34);
    m_actionStatus->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    layout->addWidget(m_actionStatus);
    layout->addStretch(1);

    return card;
}

QFrame *MainWindow::buildGlobalCard()
{
    QFrame *card = makeCard();
    card->setFixedWidth(360);
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(18, 16, 18, 16);
    layout->setSpacing(9);

    layout->addWidget(makeSectionLabel(QStringLiteral("GLOBAL")));

    m_patchDisplayCombo = new QComboBox;
    m_patchDisplayCombo->addItems(
        {QStringLiteral("STANDARD 0-127"), QStringLiteral("OFFSET 1-128")});
    m_patchDisplayCombo->setFixedWidth(160);
    layout->addWidget(makeFormRow(QStringLiteral("PATCH MODE DISPLAY"),
                                  m_patchDisplayCombo, nullptr, 150));

    m_patchChannelCombo = new QComboBox;
    fillChannelCombo(m_patchChannelCombo);
    m_patchChannelCombo->setFixedWidth(160);
    layout->addWidget(makeFormRow(QStringLiteral("PATCH MODE MIDI CH."),
                                  m_patchChannelCombo, nullptr, 150));

    m_thruCombo = new QComboBox;
    m_thruCombo->addItems({QStringLiteral("OFF"), QStringLiteral("ON")});
    m_thruCombo->setFixedWidth(160);
    layout->addWidget(
        makeFormRow(QStringLiteral("MIDI THRU"), m_thruCombo, nullptr, 150));

    m_clockCombo = new QComboBox;
    m_clockCombo->addItems(
        {QStringLiteral("INTERNAL CLOCK"), QStringLiteral("EXTERNAL CLOCK")});
    m_clockCombo->setFixedWidth(160);
    layout->addWidget(
        makeFormRow(QStringLiteral("SELECT CLOCK"), m_clockCombo, nullptr, 150));

    connect(m_patchDisplayCombo, &QComboBox::currentIndexChanged, this,
            [this](int index) {
                if (m_updating)
                    return;
                m_profile.patchModeDisplay = index;
                markProfileModified();
            });
    connect(m_patchChannelCombo, &QComboBox::currentIndexChanged, this,
            [this](int index) {
                if (m_updating)
                    return;
                m_profile.patchMidiChannel = index;
                markProfileModified();
            });
    connect(m_thruCombo, &QComboBox::currentIndexChanged, this,
            [this](int index) {
                if (m_updating)
                    return;
                m_profile.midiThru = index;
                markProfileModified();
            });
    connect(m_clockCombo, &QComboBox::currentIndexChanged, this,
            [this](int index) {
                if (m_updating)
                    return;
                m_profile.clockSelect = index;
                markProfileModified();
            });

    layout->addWidget(makeDivider());
    layout->addWidget(makeHintLabel(
        tr("Global settings are stored inside the same 198-byte profile as the "
           "eight control assignments.")));

    auto *facts = new QGridLayout;
    facts->setContentsMargins(0, 2, 0, 0);
    facts->setHorizontalSpacing(12);
    facts->setVerticalSpacing(6);

    const QStringList captions = {
        QStringLiteral("PAYLOAD"),
        QStringLiteral("FILE"),
        QStringLiteral("TRANSPORT"),
    };
    const QStringList values = {
        QStringLiteral("198 bytes"),
        QStringLiteral(".bstarmidi"),
        QStringLiteral("ALSA raw MIDI"),
    };

    for (int row = 0; row < captions.size(); ++row) {
        QLabel *caption = makeTinyCaption(captions[row]);
        auto *value = new QLabel(values[row]);
        QFont mono(QStringLiteral("DejaVu Sans Mono"));
        mono.setPixelSize(9);
        value->setFont(mono);
        value->setStyleSheet(QStringLiteral("color: #B8BDC6;"));
        facts->addWidget(caption, row, 0);
        facts->addWidget(value, row, 1, Qt::AlignRight);
    }
    layout->addLayout(facts);
    layout->addStretch(1);

    return card;
}

QFrame *MainWindow::buildEditorCard()
{
    QFrame *card = makeCard();
    card->setMinimumWidth(410);
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(18, 16, 18, 16);
    layout->setSpacing(9);

    auto *topRow = new QHBoxLayout;
    topRow->setContentsMargins(0, 0, 0, 0);
    topRow->addWidget(makeSectionLabel(QStringLiteral("CONTROL")));
    topRow->addStretch(1);
    m_controlBadge = new QLabel(QStringLiteral("SWITCH 1"));
    m_controlBadge->setObjectName(QStringLiteral("ControlBadge"));
    m_controlBadge->setAlignment(Qt::AlignCenter);
    topRow->addWidget(m_controlBadge);
    layout->addLayout(topRow);

    auto *controlNameRow = new QHBoxLayout;
    controlNameRow->setContentsMargins(0, 0, 0, 0);
    controlNameRow->setSpacing(10);
    QLabel *controlNameLabel = makeFieldLabel(QStringLiteral("CONTROL NAME"));
    controlNameLabel->setFixedWidth(132);
    controlNameRow->addWidget(controlNameLabel);

    m_controlNameEdit = new QLineEdit;
    m_controlNameEdit->setMaxLength(64);
    m_controlNameEdit->setPlaceholderText(tr("Custom control name"));
    m_controlNameEdit->setToolTip(
        tr("Name shown for this control, up to 16 characters / 16 UTF-8 bytes"));
    m_controlNameEdit->setMinimumHeight(32);
    connect(m_controlNameEdit, &QLineEdit::textEdited, this, [this] {
        if (m_updating)
            return;
        updateNameWarning(m_controlNameEdit);
        m_profile.controls[m_selected].name = m_controlNameEdit->text();
        markProfileModified();
        updateDeviceGraphic();
    });
    controlNameRow->addWidget(m_controlNameEdit, 1);
    layout->addLayout(controlNameRow);

    layout->addWidget(makeDivider());
    layout->addWidget(makeSectionLabel(QStringLiteral("MESSAGE TYPE")));

    auto *typeRow = new QHBoxLayout;
    typeRow->setContentsMargins(0, 0, 0, 0);
    typeRow->setSpacing(6);

    m_typeGroup = new QButtonGroup(this);
    m_typeGroup->setExclusive(true);

    static const char *typeLabels[5] = {"PC", "CC", "NOTE", "CLOCK", "NONE"};
    static const char *typeTips[5] = {
        "Program Change",
        "Control Change",
        "MIDI Note",
        "MIDI Clock",
        "No message",
    };

    for (int type = 0; type < 5; ++type) {
        auto *button = new QPushButton(QString::fromLatin1(typeLabels[type]));
        button->setObjectName(QStringLiteral("TypeButton"));
        button->setCheckable(true);
        button->setCursor(Qt::PointingHandCursor);
        button->setMinimumHeight(31);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        button->setToolTip(QString::fromLatin1(typeTips[type]));
        m_typeGroup->addButton(button, type);
        m_typeButtons[type] = button;
        typeRow->addWidget(button);
    }
    connect(m_typeGroup, &QButtonGroup::idClicked, this,
            &MainWindow::onTypeClicked);
    layout->addLayout(typeRow);

    auto *panel = new QFrame;
    panel->setObjectName(QStringLiteral("InnerPanel"));
    auto *panelLayout = new QVBoxLayout(panel);
    panelLayout->setContentsMargins(14, 12, 14, 12);
    panelLayout->setSpacing(8);

    m_channelCombo = new QComboBox;
    fillChannelCombo(m_channelCombo);
    m_channelCombo->setFixedWidth(190);
    m_channelRow = makeFormRow(QStringLiteral("MIDI CHANNEL"),
                               m_channelCombo, nullptr, 145);
    connect(m_channelCombo, &QComboBox::currentIndexChanged, this,
            [this](int index) {
                if (m_updating)
                    return;
                m_profile.controls[m_selected].midiChannel = index;
                markProfileModified();
                updateDeviceGraphic();
            });
    panelLayout->addWidget(m_channelRow);

    m_numberSpin = makeValueSpin();
    m_numberSpin->setFixedWidth(100);
    m_numberRow = makeFormRow(QStringLiteral("PROGRAM NUMBER"), m_numberSpin,
                              &m_numberLabel, 145);
    connect(m_numberSpin, &QSpinBox::valueChanged, this, [this](int value) {
        if (m_updating)
            return;
        m_profile.controls[m_selected].messageNumber = value;
        markProfileModified();
        updateDeviceGraphic();
    });
    panelLayout->addWidget(m_numberRow);

    m_operationCombo = new QComboBox;
    m_operationCombo->addItems(
        {QStringLiteral("MOMENTARY"), QStringLiteral("TOGGLE")});
    m_operationCombo->setFixedWidth(190);
    m_operationRow = makeFormRow(QStringLiteral("OPERATION"),
                                 m_operationCombo, nullptr, 145);
    connect(m_operationCombo, &QComboBox::currentIndexChanged, this,
            [this](int index) {
                if (m_updating)
                    return;
                m_profile.controls[m_selected].mode = index;
                markProfileModified();
                updateDeviceGraphic();
            });
    panelLayout->addWidget(m_operationRow);

    auto *noteRow = new QWidget;
    auto *noteLayout = new QHBoxLayout(noteRow);
    noteLayout->setContentsMargins(0, 0, 0, 0);
    noteLayout->setSpacing(10);
    QLabel *noteLabel = makeFieldLabel(QStringLiteral("NOTE"));
    noteLabel->setFixedWidth(145);
    noteLayout->addWidget(noteLabel);

    m_noteCombo = new QComboBox;
    m_noteCombo->setFixedWidth(90);
    noteLayout->addWidget(m_noteCombo);

    QLabel *octaveLabel = makeFieldLabel(QStringLiteral("OCTAVE"));
    octaveLabel->setFixedWidth(52);
    noteLayout->addWidget(octaveLabel);

    m_octaveCombo = new QComboBox;
    for (int octave = -1; octave <= 9; ++octave)
        m_octaveCombo->addItem(QString::number(octave), octave + 1);
    m_octaveCombo->setFixedWidth(70);
    noteLayout->addWidget(m_octaveCombo);
    noteLayout->addStretch(1);
    m_noteRow = noteRow;

    connect(m_noteCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        if (m_updating)
            return;
        const int octaveIndex = m_octaveCombo->currentData().toInt();
        const int pitch = m_noteCombo->currentData().toInt();
        m_profile.controls[m_selected].messageNumber = octaveIndex * 12 + pitch;
        markProfileModified();
        updateDeviceGraphic();
    });
    connect(m_octaveCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        if (m_updating)
            return;
        const int octaveIndex = m_octaveCombo->currentData().toInt();
        int pitch = m_noteCombo->currentData().toInt();
        const int available = octaveIndex == 10 ? 8 : 12;
        if (pitch < 0 || pitch >= available)
            pitch = available - 1;
        rebuildNoteItems(octaveIndex, pitch);
        m_profile.controls[m_selected].messageNumber = octaveIndex * 12 + pitch;
        markProfileModified();
        updateDeviceGraphic();
    });
    panelLayout->addWidget(m_noteRow);

    auto *valuesRow = new QWidget;
    auto *valuesLayout = new QHBoxLayout(valuesRow);
    valuesLayout->setContentsMargins(0, 0, 0, 0);
    valuesLayout->setSpacing(8);

    m_offLabel = makeFieldLabel(QStringLiteral("OFF VALUE"));
    m_offSpin = makeValueSpin();
    valuesLayout->addWidget(m_offLabel);
    valuesLayout->addWidget(m_offSpin);
    valuesLayout->addSpacing(14);

    m_onLabel = makeFieldLabel(QStringLiteral("ON VALUE"));
    m_onSpin = makeValueSpin();
    valuesLayout->addWidget(m_onLabel);
    valuesLayout->addWidget(m_onSpin);
    valuesLayout->addStretch(1);

    connect(m_offSpin, &QSpinBox::valueChanged, this, [this](int value) {
        if (m_updating)
            return;
        m_profile.controls[m_selected].offMin = value;
        markProfileModified();
        updateDeviceGraphic();
    });
    connect(m_onSpin, &QSpinBox::valueChanged, this, [this](int value) {
        if (m_updating)
            return;
        m_profile.controls[m_selected].onMax = value;
        markProfileModified();
        updateDeviceGraphic();
    });
    m_valuesRow = valuesRow;
    panelLayout->addWidget(m_valuesRow);

    m_emptyLabel = new QLabel;
    m_emptyLabel->setObjectName(QStringLiteral("MutedLabel"));
    m_emptyLabel->setWordWrap(true);
    m_emptyLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_emptyLabel->setMinimumHeight(54);
    m_emptyLabel->setContentsMargins(0, 12, 0, 0);
    panelLayout->addWidget(m_emptyLabel);
    panelLayout->addStretch(1);
    panelLayout->addWidget(makeDivider(panel));

    auto *previewRow = new QHBoxLayout;
    previewRow->setContentsMargins(0, 0, 0, 0);
    previewRow->setSpacing(10);
    previewRow->addWidget(makeTinyCaption(QStringLiteral("OUTPUT PREVIEW")));
    previewRow->addStretch(1);
    m_previewLabel = new QLabel;
    QFont previewFont(QStringLiteral("DejaVu Sans Mono"));
    previewFont.setPixelSize(9);
    m_previewLabel->setFont(previewFont);
    m_previewLabel->setStyleSheet(QStringLiteral("color: #E8B164;"));
    previewRow->addWidget(m_previewLabel);
    panelLayout->addLayout(previewRow);

    layout->addWidget(panel, 1);
    layout->addWidget(makeHintLabel(
        tr("PC, CC and Note controls use a MIDI channel and 0\u2013127 values. "
           "Expression pedals can send CC or no message; their values are the "
           "pedal's minimum and maximum.")));

    return card;
}

void MainWindow::buildMenus()
{
    QMenu *fileMenu = menuBar()->addMenu(tr("&File"));

    auto *openAction = new QAction(tr("&Open Profile\u2026"), this);
    openAction->setShortcut(QKeySequence::Open);
    connect(openAction, &QAction::triggered, this,
            [this] { openProfileFile(); });
    fileMenu->addAction(openAction);

    auto *saveAction = new QAction(tr("&Save Profile"), this);
    saveAction->setShortcut(QKeySequence::Save);
    connect(saveAction, &QAction::triggered, this,
            [this] { saveProfileFile(false); });
    fileMenu->addAction(saveAction);

    auto *saveAsAction = new QAction(tr("Save Profile &As\u2026"), this);
    saveAsAction->setShortcut(QKeySequence::SaveAs);
    connect(saveAsAction, &QAction::triggered, this,
            [this] { saveProfileFile(true); });
    fileMenu->addAction(saveAsAction);

    fileMenu->addSeparator();
    auto *quitAction = new QAction(tr("&Quit"), this);
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, this, &QWidget::close);
    fileMenu->addAction(quitAction);

    QMenu *deviceMenu = menuBar()->addMenu(tr("&Device"));

    auto *readAction = new QAction(tr("&Read Values"), this);
    readAction->setShortcut(QKeySequence(Qt::Key_F5));
    connect(readAction, &QAction::triggered, this,
            [this] { readValues(false); });
    deviceMenu->addAction(readAction);

    auto *sendAction = new QAction(tr("&Send Values"), this);
    sendAction->setShortcuts(
        {QKeySequence(QStringLiteral("Ctrl+Return")),
         QKeySequence(QStringLiteral("Ctrl+Enter"))});
    connect(sendAction, &QAction::triggered, this, &MainWindow::sendValues);
    deviceMenu->addAction(sendAction);

    deviceMenu->addSeparator();
    auto *rescanAction = new QAction(tr("&Rescan MIDI Ports"), this);
    rescanAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+R")));
    connect(rescanAction, &QAction::triggered, this, [this] {
        m_lastAttemptedPort.clear();
        m_autoAttemptTimer.invalidate();
        refreshPorts(true);
    });
    deviceMenu->addAction(rescanAction);

    auto *logAction = new QAction(tr("MIDI &Log"), this);
    logAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+L")));
    connect(logAction, &QAction::triggered, this, &MainWindow::showMidiLog);
    deviceMenu->addAction(logAction);

    QMenu *helpMenu = menuBar()->addMenu(tr("&Help"));
    auto *aboutAction = new QAction(tr("&About Live Logic MIDI Control"), this);
    connect(aboutAction, &QAction::triggered, this, &MainWindow::showAbout);
    helpMenu->addAction(aboutAction);
}

void MainWindow::refreshPorts(bool manual)
{
    const QVector<MidiPort> ports = MidiDevice::availablePorts();

    bool changed = ports.size() != m_ports.size();
    if (!changed) {
        for (int i = 0; i < ports.size(); ++i) {
            if (ports[i].id != m_ports[i].id || ports[i].name != m_ports[i].name) {
                changed = true;
                break;
            }
        }
    }
    m_ports = ports;

    if (!m_currentPortId.isEmpty()) {
        const bool stillPresent =
            std::any_of(m_ports.cbegin(), m_ports.cend(),
                        [this](const MidiPort &port) {
                            return port.id == m_currentPortId;
                        });
        if (!stillPresent) {
            handleDisconnected(
                tr("The Live Logic MIDI port was disconnected."));
        }
    }

    if (changed || m_portCombo->count() <= 1)
        rebuildPortCombo();

    if (manual) {
        m_lastAttemptedPort.clear();
        m_autoAttemptTimer.invalidate();
    }

    if (!m_midi->isOpen()) {
        const QString autoId = autoPortId();
        if (!autoId.isEmpty()) {
            bool shouldTry = m_lastAttemptedPort != autoId;
            if (!shouldTry && m_autoAttemptTimer.isValid()
                && m_autoAttemptTimer.elapsed() > 6000) {
                shouldTry = true;
            }
            if (!m_autoAttemptTimer.isValid())
                shouldTry = true;
            if (shouldTry)
                openPort(autoId, true);
        } else {
            updateConnectionUi();
        }
    }

    if (m_midi->isOpen()) {
        statusBar()->showMessage(
            tr("MIDI: %1  \u00b7  %2")
                .arg(m_currentPortId, portDisplayName(m_currentPortId)));
    } else if (m_ports.isEmpty()) {
        statusBar()->showMessage(
            tr("No ALSA raw MIDI ports found. Connect the controller via USB."));
    } else {
        statusBar()->showMessage(
            tr("%1 MIDI port(s) available. Select one above or reconnect the "
               "Live Logic.")
                .arg(m_ports.size()));
    }
}

QString MainWindow::autoPortId() const
{
    for (const MidiPort &port : m_ports) {
        const auto matches = [](const QString &text) {
            return text.contains(QStringLiteral("live logic"), Qt::CaseInsensitive)
                   || text.contains(QStringLiteral("livelogic"),
                                    Qt::CaseInsensitive);
        };
        if (matches(port.name) || matches(port.cardName))
            return port.id;
    }
    return {};
}

QString MainWindow::portDisplayName(const QString &portId) const
{
    for (const MidiPort &port : m_ports) {
        if (port.id == portId)
            return port.name;
    }
    return portId;
}

void MainWindow::rebuildPortCombo()
{
    QSignalBlocker blocker(m_portCombo);
    m_portCombo->clear();

    if (m_ports.isEmpty()) {
        m_portCombo->addItem(tr("No MIDI ports detected"));
        if (auto *model = qobject_cast<QStandardItemModel *>(m_portCombo->model())) {
            if (QStandardItem *item = model->item(0))
                item->setEnabled(false);
        }
        m_portCombo->setCurrentIndex(0);
        m_portCombo->setEnabled(false);
        return;
    }

    m_portCombo->addItem(tr("Select a MIDI port\u2026"));
    if (auto *model = qobject_cast<QStandardItemModel *>(m_portCombo->model())) {
        if (QStandardItem *item = model->item(0))
            item->setEnabled(false);
    }

    int selectedIndex = 0;
    for (const MidiPort &port : m_ports) {
        m_portCombo->addItem(port.name, port.id);
        const int index = m_portCombo->count() - 1;
        m_portCombo->setItemData(
            index,
            tr("%1  (%2)\nCard: %3")
                .arg(port.name, port.id, port.cardName),
            Qt::ToolTipRole);
        if (port.id == m_currentPortId)
            selectedIndex = index;
    }
    m_portCombo->setCurrentIndex(selectedIndex);
    m_portCombo->setEnabled(!m_waitingRead && m_sendPhase == SendPhase::Idle
                           && !m_ports.isEmpty());
}

void MainWindow::openPort(const QString &portId, bool automatic)
{
    if (portId.isEmpty())
        return;
    if (m_midi->isOpen() && m_currentPortId == portId)
        return;

    m_midi->close();
    m_readTimeout->stop();
    m_sendTimeout->stop();
    m_waitingRead = false;
    m_sendPhase = SendPhase::Idle;
    m_connected = false;
    m_autoReadDone = false;
    m_currentPortId.clear();
    m_readAccumulator.reset();
    setTransferBusy(false);
    m_star->setActive(false);
    m_device->setConnected(false);

    setActionStatus(tr("Opening MIDI port \u201c%1\u201d\u2026")
                        .arg(portDisplayName(portId)),
                    StatusState::Busy);

    QString error;
    if (!m_midi->open(portId, &error)) {
        m_lastAttemptedPort = portId;
        m_autoAttemptTimer.start();
        m_currentPortId.clear();
        if (m_portCombo->count() > 0) {
            QSignalBlocker blocker(m_portCombo);
            m_portCombo->setCurrentIndex(0);
        }
        setActionStatus(tr("Could not open the MIDI port. %1").arg(error),
                        StatusState::Error);
        appendLogText(tr("Open failed: %1").arg(error));
        statusBar()->showMessage(tr("MIDI port open failed."));
        updateConnectionUi();
        if (!automatic) {
            QMessageBox::warning(
                this, tr("MIDI PORT"),
                tr("Could not open the MIDI port.\n\n%1\n\nClose other MIDI "
                   "applications that may be holding the device, then press "
                   "RESCAN.")
                    .arg(error));
        }
        return;
    }

    m_currentPortId = portId;
    m_lastAttemptedPort = portId;
    m_autoAttemptTimer.start();
    rebuildPortCombo();
    updateConnectionUi();
    statusBar()->showMessage(
        tr("MIDI: %1  \u00b7  %2").arg(portId, portDisplayName(portId)));
    appendLogText(tr("Opened MIDI port %1 (%2).")
                      .arg(portDisplayName(portId), portId));
    setActionStatus(tr("Port open. Waiting for the Live Logic handshake\u2026"),
                    StatusState::Busy);

    const QByteArray inquiry = Protocol::inquiryMessage();
    QString sendError;
    if (m_midi->send(inquiry, &sendError)) {
        logSysex(QStringLiteral("TX"), inquiry);
    } else {
        appendLogText(tr("Device inquiry failed: %1").arg(sendError));
        setActionStatus(tr("MIDI port is open, but the device inquiry could "
                           "not be sent. %1")
                            .arg(sendError),
                        StatusState::Error);
    }
}

void MainWindow::handleDisconnected(const QString &reason)
{
    if (!m_midi->isOpen() && m_currentPortId.isEmpty() && !m_connected) {
        updateConnectionUi();
        return;
    }

    const QString oldPort = m_currentPortId;
    m_midi->close();
    m_readTimeout->stop();
    m_sendTimeout->stop();
    m_waitingRead = false;
    m_sendPhase = SendPhase::Idle;
    m_connected = false;
    m_currentPortId.clear();
    m_readAccumulator.reset();
    setTransferBusy(false);
    m_star->setActive(false);
    m_device->setConnected(false);

    if (!oldPort.isEmpty()) {
        m_lastAttemptedPort = oldPort;
        m_autoAttemptTimer.start();
    }

    appendLogText(reason);
    setActionStatus(reason, StatusState::Error);
    statusBar()->showMessage(reason);
    rebuildPortCombo();
    updateConnectionUi();
}

void MainWindow::updateConnectionUi()
{
    const bool portOpen = m_midi && m_midi->isOpen();

    QString title;
    QString detail;
    QString state;

    if (m_connected) {
        title = tr("DEVICE CONNECTED");
        detail = portDisplayName(m_currentPortId);
        if (detail.isEmpty())
            detail = tr("Live Logic MIDI Controller");
        state = QStringLiteral("connected");
        m_device->setConnected(true);
        m_star->setActive(true);
    } else if (portOpen) {
        title = tr("MIDI PORT OPEN");
        detail = tr("Waiting for handshake \u00b7 %1")
                     .arg(portDisplayName(m_currentPortId));
        state = QStringLiteral("pending");
        m_device->setConnected(false);
        m_star->setActive(false);
    } else {
        title = tr("NO DEVICE");
        detail = m_ports.isEmpty()
                     ? tr("No MIDI ports \u2014 connect the controller via USB")
                     : tr("Connect the Live Logic via USB");
        state = QStringLiteral("idle");
        m_device->setConnected(false);
        m_star->setActive(false);
    }

    m_connectionTitle->setText(title);
    m_connectionTitle->setProperty("state", state);
    repolish(m_connectionTitle);
    m_connectionDetail->setText(detail);
    m_connectionDetail->setToolTip(detail);
}

void MainWindow::markDeviceConnected()
{
    if (m_connected)
        return;

    m_connected = true;
    updateConnectionUi();
    appendLogText(tr("Live Logic handshake received on %1.")
                      .arg(portDisplayName(m_currentPortId)));

    const bool mayAutoRead = m_allowAutoRead && !m_autoReadDone
                             && !m_waitingRead && m_sendPhase == SendPhase::Idle;
    if (mayAutoRead) {
        m_autoReadDone = true;
        setActionStatus(tr("Connected. Reading the current preset\u2026"),
                        StatusState::Busy);
        QTimer::singleShot(80, this, [this] {
            if (m_midi->isOpen() && !m_waitingRead
                && m_sendPhase == SendPhase::Idle) {
                readValues(true);
            }
        });
    } else if (!m_waitingRead && m_sendPhase == SendPhase::Idle) {
        if (m_modified) {
            setActionStatus(
                tr("Connected. Unsaved edits were kept; press READ VALUES to "
                   "load the device preset."),
                StatusState::Success);
        } else {
            setActionStatus(tr("Connected. Ready to read or send."),
                            StatusState::Success);
        }
    }
}

void MainWindow::onMidiLinkError(const QString &portId, const QString &error)
{
    if (portId != m_currentPortId)
        return;
    appendLogText(tr("MIDI link error: %1").arg(error));
    if (m_midi->isOpen() || !m_currentPortId.isEmpty()) {
        handleDisconnected(tr("Device disconnected: %1").arg(error));
    }
}

void MainWindow::onSysExReceived(const QString &portId, const QByteArray &message)
{
    if (portId != m_currentPortId)
        return;
    logSysex(QStringLiteral("RX"), message);

    if (Protocol::isIdentityReply(message)) {
        markDeviceConnected();
        return;
    }

    if (Protocol::isVendorMessage(message)) {
        markDeviceConnected();
        const int command = Protocol::vendorCommand(message);

        if (command == 0x41 && Protocol::isLoadAck(message)) {
            if (m_sendPhase == SendPhase::AwaitingAck) {
                m_sendPhase = SendPhase::Saving;
                setActionStatus(
                    tr("Profile accepted. Saving it to device flash\u2026"),
                    StatusState::Busy);
                const QByteArray save = Protocol::saveMessage();
                QString error;
                if (m_midi->send(save, &error)) {
                    logSysex(QStringLiteral("TX"), save);
                    m_sendTimeout->start(6000);
                } else {
                    finishSend(false,
                               tr("The profile was accepted, but the save "
                                  "command could not be sent: %1")
                                   .arg(error),
                               true);
                }
            }
            return;
        }

        if (command == 0x45) {
            const int result = Protocol::saveResult(message);
            if (result >= 0) {
                if (m_sendPhase != SendPhase::Idle) {
                    finishSend(
                        result == 1,
                        result == 1
                            ? tr("Send values to flash successfully!")
                            : tr("Send values to flash failed!"),
                        true);
                } else {
                    appendLogText(tr("Ignoring an unexpected save response."));
                }
            }
            return;
        }

        if (command == 0x46) {
            const int index = Protocol::controlUpdateIndex(message);
            if (index > 0) {
                if (m_sendPhase != SendPhase::Idle) {
                    setActionStatus(
                        tr("Device updated control %1 of 8\u2026").arg(index),
                        StatusState::Busy);
                }
                appendLogText(tr("Device reported control %1 updated.").arg(index));
            }
            return;
        }
    }

    if (!m_waitingRead)
        return;

    m_readAccumulator.addMessage(message);
    m_readTimeout->start(2500);

    Profile parsed;
    QString error;
    if (m_readAccumulator.tryTake(&parsed, &error)) {
        m_waitingRead = false;
        m_readTimeout->stop();
        setTransferBusy(false);

        m_profile = parsed;
        m_profile.sanitize();
        m_selected = kSwitch1Index;
        m_modified = false;
        m_currentFilePath.clear();
        m_allowAutoRead = true;
        m_autoReadDone = true;

        refreshAll();
        setActionStatus(tr("Preset read from the device."),
                        StatusState::Success);
        statusBar()->showMessage(
            tr("Read a 198-byte preset from %1.")
                .arg(portDisplayName(m_currentPortId)));
        appendLogText(tr("Read succeeded."));
    }
}

void MainWindow::readValues(bool automatic)
{
    if (!m_midi->isOpen()) {
        if (!automatic) {
            QMessageBox::warning(
                this, tr("NOT CONNECTED"),
                tr("Open the Live Logic MIDI port before reading values."));
        }
        return;
    }
    if (m_waitingRead || m_sendPhase != SendPhase::Idle)
        return;

    m_waitingRead = true;
    m_readWasAuto = automatic;
    m_readAccumulator.reset();
    setTransferBusy(true);
    setActionStatus(tr("Requesting the current preset from the device\u2026"),
                    StatusState::Busy);

    const QByteArray command = Protocol::readPresetMessage();
    QString error;
    if (!m_midi->send(command, &error)) {
        m_waitingRead = false;
        setTransferBusy(false);
        setActionStatus(tr("Could not send the read command. %1").arg(error),
                        StatusState::Error);
        appendLogText(tr("Read command failed: %1").arg(error));
        if (!automatic)
            QMessageBox::warning(this, tr("MIDI ERROR"),
                                 tr("Could not send the read command.\n\n%1")
                                     .arg(error));
        return;
    }

    logSysex(QStringLiteral("TX"), command);
    m_readTimeout->start(3500);
}

void MainWindow::sendValues()
{
    if (!m_midi->isOpen()) {
        QMessageBox::warning(
            this, tr("NOT CONNECTED"),
            tr("Open the Live Logic MIDI port before sending values."));
        return;
    }
    if (m_waitingRead || m_sendPhase != SendPhase::Idle)
        return;

    m_sendPhase = SendPhase::AwaitingAck;
    setTransferBusy(true);
    setActionStatus(tr("Sending profile to the device\u2026"),
                    StatusState::Busy);

    const QByteArray message = Protocol::loadMessage(m_profile);
    QString error;
    if (!m_midi->send(message, &error)) {
        finishSend(false, tr("Could not send the profile. %1").arg(error), true);
        return;
    }

    logSysex(QStringLiteral("TX"), message);
    m_sendTimeout->start(6000);
}

void MainWindow::finishSend(bool success, const QString &detail,
                            bool showDialog)
{
    m_sendTimeout->stop();
    m_sendPhase = SendPhase::Idle;
    setTransferBusy(false);
    setActionStatus(detail,
                    success ? StatusState::Success : StatusState::Error);
    statusBar()->showMessage(detail);
    appendLogText(detail);

    if (success) {
        m_modified = false;
        m_autoReadDone = true;
        updateWindowTitle();
    }

    if (showDialog) {
        if (success) {
            QMessageBox::information(this, tr("MESSAGE"), detail);
        } else {
            QMessageBox::warning(this, tr("WARNING"), detail);
        }
    }
}

void MainWindow::setTransferBusy(bool busy)
{
    if (m_sendButton)
        m_sendButton->setEnabled(!busy);
    if (m_readButton)
        m_readButton->setEnabled(!busy);
    if (m_editorCard)
        m_editorCard->setEnabled(!busy);
    if (m_globalCard)
        m_globalCard->setEnabled(!busy);
    if (m_profileNameEdit)
        m_profileNameEdit->setEnabled(!busy);
    if (m_portCombo)
        m_portCombo->setEnabled(!busy && !m_ports.isEmpty());
    if (m_rescanButton)
        m_rescanButton->setEnabled(!busy);
}

void MainWindow::refreshAll()
{
    m_updating = true;
    m_profile.sanitize();

    if (m_profileNameEdit)
        m_profileNameEdit->setText(m_profile.name);
    if (m_patchDisplayCombo)
        m_patchDisplayCombo->setCurrentIndex(m_profile.patchModeDisplay);
    if (m_patchChannelCombo)
        m_patchChannelCombo->setCurrentIndex(m_profile.patchMidiChannel);
    if (m_thruCombo)
        m_thruCombo->setCurrentIndex(m_profile.midiThru);
    if (m_clockCombo)
        m_clockCombo->setCurrentIndex(m_profile.clockSelect);

    if (m_device) {
        m_device->setProfile(m_profile);
        m_device->setSelectedIndex(m_selected);
    }

    m_updating = false;
    refreshEditor();
    updateWindowTitle();
}

void MainWindow::refreshEditor()
{
    const bool previous = m_updating;
    m_updating = true;

    const Control &control = m_profile.controls[m_selected];
    const bool pedal = isPedalIndex(m_selected);

    if (m_controlBadge) {
        m_controlBadge->setText(controlKindName(m_selected));
        m_controlBadge->setProperty("pedal", pedal);
        repolish(m_controlBadge);
    }

    if (m_controlNameEdit)
        m_controlNameEdit->setText(control.name);

    for (int type = 0; type < 5; ++type) {
        if (!m_typeButtons[type])
            continue;
        const bool allowed = !pedal || type == MessageTypes::Cc
                             || type == MessageTypes::None;
        m_typeButtons[type]->setEnabled(allowed);
        m_typeButtons[type]->setCheckable(allowed);
        m_typeButtons[type]->setChecked(allowed && type == control.messageType);
    }

    if (m_channelCombo)
        m_channelCombo->setCurrentIndex(control.midiChannel);
    if (m_numberSpin)
        m_numberSpin->setValue(control.messageNumber);
    if (m_operationCombo)
        m_operationCombo->setCurrentIndex(control.mode);
    if (m_offSpin)
        m_offSpin->setValue(control.offMin);
    if (m_onSpin)
        m_onSpin->setValue(control.onMax);

    if (m_operationCombo)
        m_operationCombo->setEnabled(!pedal);

    syncNoteEditors(control.messageNumber);
    updateValueLabels();
    updateNoteVisibility();
    updatePreview();

    if (m_device) {
        m_device->setProfile(m_profile);
        m_device->setSelectedIndex(m_selected);
    }

    m_updating = previous;
}

void MainWindow::setSelectedIndex(int index)
{
    if (index < 0 || index >= kControlCount)
        return;
    m_selected = index;
    if (m_device)
        m_device->setSelectedIndex(index);
    refreshEditor();
}

void MainWindow::updateDeviceGraphic()
{
    if (!m_device)
        return;
    m_device->setProfile(m_profile);
    m_device->update();
    updatePreview();
}

void MainWindow::updatePreview()
{
    if (!m_previewLabel)
        return;

    const Control &control = m_profile.controls[m_selected];
    const QString channel = control.midiChannel == 0
                                ? QStringLiteral("OMNI")
                                : QStringLiteral("CH %1").arg(control.midiChannel);
    QString preview;

    switch (control.messageType) {
    case MessageTypes::Pc:
        preview = QStringLiteral("PC %1  \u00b7  %2")
                      .arg(control.messageNumber)
                      .arg(channel);
        break;
    case MessageTypes::Cc:
        if (isPedalIndex(m_selected)) {
            preview = QStringLiteral("CC %1  \u00b7  %2  \u00b7  %3\u2192%4")
                          .arg(control.messageNumber)
                          .arg(channel)
                          .arg(control.offMin)
                          .arg(control.onMax);
        } else {
            preview = QStringLiteral("CC %1  \u00b7  %2  \u00b7  %3  \u00b7  "
                                     "%4\u2192%5")
                          .arg(control.messageNumber)
                          .arg(channel)
                          .arg(control.mode == 0 ? QStringLiteral("MOMENTARY")
                                                 : QStringLiteral("TOGGLE"))
                          .arg(control.offMin)
                          .arg(control.onMax);
        }
        break;
    case MessageTypes::Note:
        preview = QStringLiteral("NOTE %1  \u00b7  %2  \u00b7  %3  \u00b7  "
                                 "%4\u2192%5")
                      .arg(midiNoteName(control.messageNumber))
                      .arg(channel)
                      .arg(control.mode == 0 ? QStringLiteral("MOMENTARY")
                                             : QStringLiteral("TOGGLE"))
                      .arg(control.offMin)
                      .arg(control.onMax);
        break;
    case MessageTypes::Clock:
        preview = QStringLiteral("MIDI CLOCK");
        break;
    case MessageTypes::None:
        preview = QStringLiteral("NO MESSAGE");
        break;
    default:
        break;
    }

    m_previewLabel->setText(preview);
    m_previewLabel->setToolTip(preview);
}

void MainWindow::updateNoteVisibility()
{
    const Control &control = m_profile.controls[m_selected];
    const int type = control.messageType;

    const bool tone = type == MessageTypes::Pc || type == MessageTypes::Cc
                      || type == MessageTypes::Note;
    const bool showNumber =
        type == MessageTypes::Pc || type == MessageTypes::Cc;
    const bool showOperation =
        type == MessageTypes::Cc || type == MessageTypes::Note;
    const bool showNote = type == MessageTypes::Note;
    const bool showValues =
        type == MessageTypes::Cc || type == MessageTypes::Note;

    if (m_channelRow)
        m_channelRow->setVisible(tone);
    if (m_numberRow)
        m_numberRow->setVisible(showNumber);
    if (m_operationRow)
        m_operationRow->setVisible(showOperation);
    if (m_noteRow)
        m_noteRow->setVisible(showNote);
    if (m_valuesRow)
        m_valuesRow->setVisible(showValues);

    if (m_numberLabel) {
        m_numberLabel->setText(type == MessageTypes::Pc
                                   ? QStringLiteral("PROGRAM NUMBER")
                                   : QStringLiteral("CC MESSAGE NUMBER"));
    }

    if (m_emptyLabel) {
        const bool empty = !tone && !showNumber && !showOperation && !showNote
                           && !showValues;
        if (empty && type == MessageTypes::Clock) {
            m_emptyLabel->setText(
                tr("This control sends MIDI clock. No further settings are "
                   "required."));
        } else if (empty) {
            m_emptyLabel->setText(
                tr("This control sends no MIDI message. Its name is still "
                   "stored in the profile."));
        }
        m_emptyLabel->setVisible(empty);
    }
}

void MainWindow::updateValueLabels()
{
    const bool pedal = isPedalIndex(m_selected);
    if (m_offLabel)
        m_offLabel->setText(pedal ? QStringLiteral("MIN VALUE")
                                  : QStringLiteral("OFF VALUE"));
    if (m_onLabel)
        m_onLabel->setText(pedal ? QStringLiteral("MAX VALUE")
                                 : QStringLiteral("ON VALUE"));
}

void MainWindow::onTypeClicked(int messageType)
{
    if (m_updating || messageType < 0 || messageType > 4)
        return;
    if (isPedalIndex(m_selected) && messageType != MessageTypes::Cc
        && messageType != MessageTypes::None) {
        return;
    }

    m_profile.controls[m_selected].messageType = messageType;
    markProfileModified();
    refreshEditor();
    updateDeviceGraphic();
}

void MainWindow::syncNoteEditors(int noteNumber)
{
    noteNumber = qBound(0, noteNumber, 127);
    const int octaveIndex = noteNumber / 12;
    const int pitch = noteNumber % 12;

    if (m_octaveCombo) {
        QSignalBlocker blocker(m_octaveCombo);
        m_octaveCombo->setCurrentIndex(octaveIndex);
    }
    rebuildNoteItems(octaveIndex, pitch);
}

void MainWindow::rebuildNoteItems(int octaveIndex, int preferredPitch)
{
    if (!m_noteCombo)
        return;

    static const char *pitchNames[12] = {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B",
    };
    const int count = octaveIndex == 10 ? 8 : 12;

    QSignalBlocker blocker(m_noteCombo);
    m_noteCombo->clear();
    for (int pitch = 0; pitch < count; ++pitch)
        m_noteCombo->addItem(QString::fromLatin1(pitchNames[pitch]), pitch);
    m_noteCombo->setCurrentIndex(preferredPitch >= 0 && preferredPitch < count
                                     ? preferredPitch
                                     : 0);
}

void MainWindow::markProfileModified()
{
    m_modified = true;
    m_allowAutoRead = false;
    updateWindowTitle();
}

void MainWindow::updateWindowTitle()
{
    QString title = QStringLiteral("Live Logic MIDI Control");
    if (!m_currentFilePath.isEmpty())
        title = QFileInfo(m_currentFilePath).fileName()
                + QStringLiteral(" \u2014 ") + title;
    if (m_modified)
        title.prepend(QStringLiteral("* "));
    setWindowTitle(title);
}

void MainWindow::setActionStatus(const QString &text, StatusState state)
{
    if (!m_actionStatus)
        return;
    m_actionStatus->setText(text);
    m_actionStatus->setToolTip(text);

    const char *stateName = "neutral";
    switch (state) {
    case StatusState::Busy:
        stateName = "busy";
        break;
    case StatusState::Success:
        stateName = "success";
        break;
    case StatusState::Error:
        stateName = "error";
        break;
    case StatusState::Neutral:
        break;
    }
    m_actionStatus->setProperty("state", QString::fromLatin1(stateName));
    repolish(m_actionStatus);
}

void MainWindow::updateNameWarning(QLineEdit *edit)
{
    if (!edit)
        return;
    const QString text = edit->text();
    if (text.size() <= 16 && text.toUtf8().size() <= 16)
        return;

    const QString clipped = cleanName(text, 16);
    {
        QSignalBlocker blocker(edit);
        edit->setText(clipped);
        edit->setCursorPosition(clipped.size());
    }

    if (!m_nameWarningActive) {
        m_nameWarningActive = true;
        QMessageBox::warning(this, tr("WARNING"),
                             tr("The length of the input name is out of range!"));
        m_nameWarningActive = false;
    }
}

bool MainWindow::openProfileFile()
{
    if (m_waitingRead || m_sendPhase != SendPhase::Idle) {
        QMessageBox::information(
            this, tr("TRANSFER IN PROGRESS"),
            tr("Wait for the current read or send operation to finish before "
               "opening another profile."));
        return false;
    }

    if (m_modified) {
        const auto choice = QMessageBox::question(
            this, tr("UNSAVED CHANGES"),
            tr("Opening another profile will discard the current unsaved "
               "changes. Continue?"),
            QMessageBox::Discard | QMessageBox::Cancel,
            QMessageBox::Cancel);
        if (choice != QMessageBox::Discard)
            return false;
    }

    const QString startDir =
        m_currentFilePath.isEmpty()
            ? QDir::homePath()
            : QFileInfo(m_currentFilePath).absolutePath();
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open profile"), startDir,
        tr("Blackstar MIDI profile (*.bstarmidi);;All files (*)"));
    if (path.isEmpty())
        return false;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("OPEN FAILED"),
                             tr("Could not open %1:\n%2")
                                 .arg(path, file.errorString()));
        return false;
    }

    Profile parsed;
    QString error;
    if (!Profile::fromFileContents(file.readAll(), &parsed, &error)) {
        QMessageBox::warning(this, tr("INVALID PROFILE"),
                             tr("Could not read \"%1\".\n\n%2")
                                 .arg(QFileInfo(path).fileName(), error));
        appendLogText(tr("Profile open failed: %1").arg(error));
        return false;
    }

    m_profile = parsed;
    m_profile.sanitize();
    m_selected = kSwitch1Index;
    m_currentFilePath = path;
    m_modified = false;
    m_allowAutoRead = false;
    m_autoReadDone = true;
    refreshAll();

    setActionStatus(tr("Loaded %1.").arg(QFileInfo(path).fileName()),
                    StatusState::Success);
    statusBar()->showMessage(tr("Profile loaded from %1").arg(path));
    appendLogText(tr("Opened profile %1 (%2 bytes).")
                      .arg(path)
                      .arg(kPayloadSize));
    return true;
}

bool MainWindow::saveProfileFile(bool saveAs)
{
    QString path = m_currentFilePath;
    if (saveAs || path.isEmpty()) {
        const QString startDir =
            path.isEmpty()
                ? QDir::home().filePath(QStringLiteral("Untitled.bstarmidi"))
                : path;
        path = QFileDialog::getSaveFileName(
            this, tr("Choose the path to save the data file"), startDir,
            tr("Blackstar MIDI profile (*.bstarmidi);;All files (*)"));
        if (path.isEmpty())
            return false;
        if (QFileInfo(path).suffix().isEmpty())
            path += QStringLiteral(".bstarmidi");
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, tr("SAVE FAILED"),
                             tr("Could not create %1:\n%2")
                                 .arg(path, file.errorString()));
        return false;
    }

    const QByteArray contents = m_profile.toFileContents();
    if (file.write(contents) != contents.size() || !file.commit()) {
        QMessageBox::warning(this, tr("SAVE FAILED"),
                             tr("Could not save %1:\n%2")
                                 .arg(path, file.errorString()));
        return false;
    }

    m_currentFilePath = path;
    m_modified = false;
    m_allowAutoRead = false;
    m_autoReadDone = true;
    updateWindowTitle();
    setActionStatus(tr("Saved %1.").arg(QFileInfo(path).fileName()),
                    StatusState::Success);
    statusBar()->showMessage(tr("Profile saved to %1").arg(path));
    appendLogText(tr("Saved profile %1 (%2 hex characters).")
                      .arg(path)
                      .arg(contents.size()));
    return true;
}

void MainWindow::showMidiLog()
{
    if (!m_logDialog) {
        m_logDialog = new QDialog(this);
        m_logDialog->setWindowTitle(tr("MIDI Log"));
        m_logDialog->resize(780, 460);

        auto *layout = new QVBoxLayout(m_logDialog);
        layout->setContentsMargins(14, 14, 14, 14);
        layout->setSpacing(10);

        auto *hint = makeHintLabel(
            tr("Live SysEx traffic and connection events. Messages are shown "
               "as hexadecimal; use this log when verifying the device's read "
               "chunking or reporting a problem."));
        layout->addWidget(hint);

        m_logView = new QPlainTextEdit;
        m_logView->setReadOnly(true);
        m_logView->setLineWrapMode(QPlainTextEdit::NoWrap);
        m_logView->setMaximumBlockCount(5000);
        m_logView->setPlainText(m_logHistory.join(QLatin1Char('\n')));
        layout->addWidget(m_logView, 1);

        auto *buttons = new QHBoxLayout;
        buttons->setContentsMargins(0, 0, 0, 0);
        auto *clearButton = new QPushButton(tr("CLEAR"));
        clearButton->setObjectName(QStringLiteral("GhostButton"));
        connect(clearButton, &QPushButton::clicked, this, [this] {
            m_logHistory.clear();
            if (m_logView)
                m_logView->clear();
        });
        buttons->addWidget(clearButton);

        auto *copyButton = new QPushButton(tr("COPY ALL"));
        copyButton->setObjectName(QStringLiteral("GhostButton"));
        connect(copyButton, &QPushButton::clicked, this, [this] {
            if (m_logView)
                QApplication::clipboard()->setText(m_logView->toPlainText());
        });
        buttons->addWidget(copyButton);
        buttons->addStretch(1);

        auto *closeButton = new QPushButton(tr("CLOSE"));
        closeButton->setObjectName(QStringLiteral("PrimaryButton"));
        connect(closeButton, &QPushButton::clicked, m_logDialog, &QDialog::hide);
        buttons->addWidget(closeButton);
        layout->addLayout(buttons);
    }

    m_logDialog->show();
    m_logDialog->raise();
    m_logDialog->activateWindow();
}

void MainWindow::appendLogText(const QString &text)
{
    const QString line =
        QStringLiteral("[%1] %2")
            .arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss.zzz")),
                 text);
    m_logHistory.append(line);
    if (m_logHistory.size() > 5000)
        m_logHistory.removeFirst();
    if (m_logView)
        m_logView->appendPlainText(line);
}

void MainWindow::logSysex(const QString &direction, const QByteArray &message)
{
    appendLogText(QStringLiteral("%1 %2")
                      .arg(direction, Protocol::prettyHex(message)));
}

void MainWindow::showAbout()
{
    QMessageBox::about(
        this, tr("About Live Logic MIDI Control"),
        tr("<h3>Live Logic MIDI Control</h3>"
           "<p>Version %1 for Linux.</p>"
           "<p>A native profile editor for the Blackstar Live Logic MIDI "
           "Controller, built from the reverse-engineered protocol notes in "
           "the repository.</p>"
           "<p>MIDI I/O uses ALSA raw MIDI. Profiles are uploaded over SysEx "
           "and committed to the device flash by the controller when it "
           "acknowledges a load.</p>"
           "<p>Not affiliated with Blackstar Amplification Ltd.</p>")
            .arg(QApplication::applicationVersion()));
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_modified) {
        QMessageBox box(QMessageBox::Question, tr("Unsaved profile"),
                        tr("Save changes to the current profile before "
                           "closing?"),
                        QMessageBox::Save | QMessageBox::Discard
                            | QMessageBox::Cancel,
                        this);
        box.setDefaultButton(QMessageBox::Save);
        const int result = box.exec();
        if (result == QMessageBox::Cancel) {
            event->ignore();
            return;
        }
        if (result == QMessageBox::Save && !saveProfileFile(false)) {
            event->ignore();
            return;
        }
    }

    m_midi->close();
    event->accept();
}
