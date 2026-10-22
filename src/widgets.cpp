#include "widgets.h"

#include <QFont>
#include <QFrame>
#include <QLabel>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QRadialGradient>

#include <cmath>

namespace {

QPainterPath starPath(const QPointF &center, qreal outerRadius, qreal innerRadius)
{
    QPainterPath path;
    for (int i = 0; i < 10; ++i) {
        const qreal angle = -M_PI / 2.0 + i * M_PI / 5.0;
        const qreal radius = (i % 2 == 0) ? outerRadius : innerRadius;
        const QPointF point(center.x() + std::cos(angle) * radius,
                            center.y() + std::sin(angle) * radius);
        if (i == 0)
            path.moveTo(point);
        else
            path.lineTo(point);
    }
    path.closeSubpath();
    return path;
}

} // namespace

StarWidget::StarWidget(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    m_animation = new QPropertyAnimation(this, "glow", this);
    m_animation->setDuration(260);
    m_animation->setEasingCurve(QEasingCurve::OutCubic);
}

void StarWidget::setActive(bool active)
{
    if (m_active == active)
        return;
    m_active = active;

    m_animation->stop();
    m_animation->setStartValue(m_glow);
    m_animation->setEndValue(active ? 1.0 : 0.0);
    m_animation->start();
}

void StarWidget::setDiameter(int diameter)
{
    m_diameter = diameter;
    setFixedSize(diameter, diameter);
    update();
}

void StarWidget::setGlow(qreal glow)
{
    m_glow = qBound(0.0, glow, 1.0);
    update();
}

QSize StarWidget::sizeHint() const
{
    return QSize(m_diameter, m_diameter);
}

void StarWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const QPointF center(width() / 2.0, height() / 2.0);
    const qreal radius = qMin(width(), height()) / 2.0;
    const qreal outer = radius - 1.0;
    const qreal inner = outer * 0.44;
    const QPainterPath star = starPath(center, outer, inner);

    if (m_glow > 0.001) {
        QRadialGradient glow(center, radius * 1.15);
        glow.setColorAt(0.0, QColor(240, 171, 76, static_cast<int>(150 * m_glow)));
        glow.setColorAt(0.45, QColor(240, 171, 76, static_cast<int>(70 * m_glow)));
        glow.setColorAt(1.0, QColor(240, 171, 76, 0));
        painter.setPen(Qt::NoPen);
        painter.setBrush(glow);
        painter.drawEllipse(center, radius * 1.15, radius * 1.15);
    }

    painter.setPen(QPen(QColor(62, 66, 76), 1.1));
    painter.setBrush(QColor(31, 34, 40));
    painter.drawPath(star);

    if (m_glow > 0.001) {
        QLinearGradient fill(center.x(), center.y() - outer, center.x(),
                             center.y() + outer);
        fill.setColorAt(0.0, QColor(255, 209, 132, static_cast<int>(255 * m_glow)));
        fill.setColorAt(0.55, QColor(239, 172, 74, static_cast<int>(255 * m_glow)));
        fill.setColorAt(1.0, QColor(198, 125, 38, static_cast<int>(255 * m_glow)));

        painter.setPen(QPen(QColor(255, 210, 139, static_cast<int>(220 * m_glow)), 1.1));
        painter.setBrush(fill);
        painter.drawPath(star);
    }
}

QLabel *makeSectionLabel(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("SectionTitle"));
    QFont font(QStringLiteral("Fira Sans"));
    font.setPixelSize(10);
    font.setWeight(QFont::DemiBold);
    font.setLetterSpacing(QFont::AbsoluteSpacing, 2.2);
    label->setFont(font);
    return label;
}

QLabel *makeMutedLabel(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("MutedLabel"));
    return label;
}

QFrame *makeCard(QWidget *parent)
{
    auto *frame = new QFrame(parent);
    frame->setObjectName(QStringLiteral("Card"));
    frame->setFrameShape(QFrame::NoFrame);
    return frame;
}

QFrame *makeDivider(QWidget *parent)
{
    auto *line = new QFrame(parent);
    line->setObjectName(QStringLiteral("Divider"));
    line->setFrameShape(QFrame::HLine);
    line->setFixedHeight(1);
    return line;
}

QString appStyleSheet()
{
    return QStringLiteral(R"(
QWidget {
    color: #EEEAE2;
    font-family: "Fira Sans";
    font-size: 13px;
}
QMainWindow, QDialog {
    background-color: #0B0C0F;
}
QToolTip {
    background-color: #1B1E24;
    color: #F0EDE6;
    border: 1px solid #3A3F49;
    padding: 5px 7px;
    border-radius: 5px;
}
QFrame#Card {
    background-color: #14161A;
    border: 1px solid #252932;
    border-radius: 14px;
}
QFrame#InnerPanel {
    background-color: #101216;
    border: 1px solid #23262D;
    border-radius: 10px;
}
QFrame#Divider {
    background-color: #23262D;
    border: none;
}
QLabel#SectionTitle {
    color: #858B96;
}
QLabel#FieldLabel {
    color: #8F95A0;
}
QLabel#TinyCaption {
    color: #68707B;
}
QLabel#ConnectionTitle {
    color: #8B909B;
}
QLabel#ConnectionTitle[state="connected"] {
    color: #79D39B;
}
QLabel#ConnectionTitle[state="pending"] {
    color: #E2B56B;
}
QLabel#ConnectionTitle[state="idle"] {
    color: #8B909B;
}
QLabel#ConnectionTitle[state="error"] {
    color: #E47777;
}
QLabel#ControlBadge {
    color: #F4BE6A;
    background-color: #2A2115;
    border: 1px solid #4D3A20;
    border-radius: 9px;
    padding: 3px 9px;
    font-size: 9px;
    font-weight: 600;
}
QLabel#ControlBadge[pedal="true"] {
    color: #9FD4D0;
    background-color: #162526;
    border-color: #2D4A4B;
}
QLabel#ActionStatus {
    color: #7D828C;
    font-size: 11px;
}
QLabel#ActionStatus[state="busy"] {
    color: #E8B164;
}
QLabel#ActionStatus[state="success"] {
    color: #79D39B;
}
QLabel#ActionStatus[state="error"] {
    color: #E47777;
}
QFrame#AccentBar {
    background-color: #E8A94D;
    border: none;
    border-radius: 1px;
}
QScrollArea {
    background-color: #0B0C0F;
    border: none;
}
QWidget#Content {
    background-color: #0B0C0F;
}
QLabel#MutedLabel {
    color: #7D828C;
}
QLabel#HintLabel {
    color: #666C77;
    font-size: 11px;
}
QLabel#ValueLabel {
    color: #B4B8C0;
    font-size: 12px;
}
QLabel#ErrorLabel {
    color: #E47777;
    font-size: 11px;
}
QLabel#SuccessLabel {
    color: #79D39B;
    font-size: 11px;
}
QLineEdit, QSpinBox, QComboBox {
    background-color: #0D0F13;
    border: 1px solid #2A2E37;
    border-radius: 7px;
    padding: 4px 8px;
    min-height: 20px;
    selection-background-color: #B67C2E;
    selection-color: #FFF4E3;
}
QLineEdit:hover, QSpinBox:hover, QComboBox:hover {
    border-color: #3B404B;
}
QLineEdit:focus, QSpinBox:focus, QComboBox:focus {
    border-color: #B67C2E;
}
QLineEdit:disabled, QSpinBox:disabled, QComboBox:disabled {
    color: #565B64;
    background-color: #111317;
    border-color: #20232A;
}
QComboBox::drop-down {
    border: none;
    width: 28px;
}
QComboBox::down-arrow {
    image: url(:/icons/chevron-down.svg);
    width: 10px;
    height: 7px;
}
QComboBox QAbstractItemView {
    background-color: #15181E;
    border: 1px solid #333842;
    padding: 4px;
    outline: 0;
    selection-background-color: #2B2418;
    selection-color: #F4BE6A;
}
QSpinBox::up-button, QSpinBox::down-button {
    background-color: #1A1D23;
    border-left: 1px solid #2A2E37;
    width: 19px;
}
QSpinBox::up-button {
    border-top-right-radius: 6px;
}
QSpinBox::down-button {
    border-bottom-right-radius: 6px;
}
QSpinBox::up-arrow {
    image: url(:/icons/chevron-up.svg);
    width: 8px;
    height: 6px;
}
QSpinBox::down-arrow {
    image: url(:/icons/chevron-down.svg);
    width: 8px;
    height: 6px;
}
QPushButton {
    background-color: #1B1E24;
    border: 1px solid #2D313A;
    border-radius: 8px;
    padding: 7px 14px;
    font-weight: 500;
}
QPushButton:hover {
    background-color: #23272F;
    border-color: #3A3F49;
}
QPushButton:pressed {
    background-color: #15171C;
}
QPushButton:disabled {
    color: #555A63;
    background-color: #121418;
    border-color: #1F2228;
}
QPushButton#PrimaryButton {
    background-color: #E8A94D;
    color: #17120B;
    border: 1px solid #F2BE73;
    font-weight: 700;
    padding: 10px 16px;
}
QPushButton#PrimaryButton:hover {
    background-color: #F1B45C;
}
QPushButton#PrimaryButton:pressed {
    background-color: #D8983C;
}
QPushButton#ReadButton {
    background-color: #1A222C;
    color: #CFE1F4;
    border: 1px solid #2C3A4B;
    padding: 10px 16px;
}
QPushButton#ReadButton:hover {
    background-color: #202A36;
    border-color: #3A4B61;
}
QPushButton#GhostButton {
    background-color: transparent;
    border: 1px solid #2D313A;
    padding: 6px 10px;
    color: #AAB0BA;
    font-size: 11px;
}
QPushButton#GhostButton:hover {
    background-color: #181B21;
    color: #F0EDE6;
}
QPushButton#TypeButton {
    background-color: #15181D;
    border: 1px solid #292D35;
    border-radius: 8px;
    padding: 8px 4px;
    font-weight: 600;
    font-size: 11px;
}
QPushButton#TypeButton:hover {
    background-color: #1D2027;
}
QPushButton#TypeButton:checked {
    background-color: #2A2115;
    border-color: #B67C2E;
    color: #F4BE6A;
}
QPushButton#TypeButton:disabled {
    color: #4F545D;
    background-color: #111318;
    border-color: #1D2026;
}
QMenuBar {
    background-color: #0B0C0F;
    color: #8E939D;
    padding: 3px 8px;
}
QMenuBar::item {
    background-color: transparent;
    padding: 5px 10px;
    border-radius: 4px;
}
QMenuBar::item:selected {
    background-color: #171A20;
    color: #EEEAE2;
}
QMenu {
    background-color: #15181E;
    border: 1px solid #2D313A;
    padding: 5px;
}
QMenu::item {
    padding: 7px 26px 7px 24px;
    border-radius: 4px;
}
QMenu::item:selected {
    background-color: #2A2115;
    color: #F4BE6A;
}
QMenu::separator {
    height: 1px;
    background-color: #2A2E36;
    margin: 5px 8px;
}
QStatusBar {
    background-color: #0B0C0F;
    color: #717782;
    border-top: 1px solid #1B1E24;
    font-size: 11px;
}
QPlainTextEdit {
    background-color: #0D0F13;
    border: 1px solid #262A32;
    border-radius: 8px;
    font-family: "DejaVu Sans Mono";
    font-size: 11px;
    color: #C8CDD5;
    selection-background-color: #B67C2E;
}
QMessageBox {
    background-color: #14161A;
}
QScrollBar:vertical {
    background: transparent;
    width: 10px;
    margin: 2px;
}
QScrollBar::handle:vertical {
    background: #333842;
    border-radius: 5px;
    min-height: 26px;
}
QScrollBar::handle:vertical:hover {
    background: #454B57;
}
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
    height: 0;
}
QScrollBar:horizontal {
    background: transparent;
    height: 10px;
    margin: 2px;
}
QScrollBar::handle:horizontal {
    background: #333842;
    border-radius: 5px;
    min-width: 26px;
}
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {
    width: 0;
}
)");
}
