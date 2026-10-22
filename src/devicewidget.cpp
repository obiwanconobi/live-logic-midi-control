#include "devicewidget.h"

#include <QFontMetricsF>
#include <QKeyEvent>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QToolTip>

#include <algorithm>
#include <cmath>

namespace {

QFont uiFont(int pixelSize, QFont::Weight weight = QFont::Normal,
             qreal letterSpacing = 0.0)
{
    QFont font(QStringLiteral("Fira Sans"));
    font.setPixelSize(pixelSize);
    font.setWeight(weight);
    if (letterSpacing != 0.0)
        font.setLetterSpacing(QFont::AbsoluteSpacing, letterSpacing);
    return font;
}

QFont monoFont(int pixelSize, QFont::Weight weight = QFont::Normal,
               qreal letterSpacing = 0.0)
{
    QFont font(QStringLiteral("DejaVu Sans Mono"));
    font.setPixelSize(pixelSize);
    font.setWeight(weight);
    if (letterSpacing != 0.0)
        font.setLetterSpacing(QFont::AbsoluteSpacing, letterSpacing);
    return font;
}

QString elide(const QString &text, const QFont &font, qreal width)
{
    return QFontMetricsF(font).elidedText(text, Qt::ElideRight, width);
}

QString controlSummary(const Control &control)
{
    switch (control.messageType) {
    case MessageTypes::Pc:
        return QStringLiteral("PC %1").arg(control.messageNumber);
    case MessageTypes::Cc:
        return QStringLiteral("CC %1").arg(control.messageNumber);
    case MessageTypes::Note:
        return QStringLiteral("%1").arg(midiNoteName(control.messageNumber));
    case MessageTypes::Clock:
        return QStringLiteral("CLOCK");
    case MessageTypes::None:
        return QStringLiteral("NONE");
    default:
        return QStringLiteral("?");
    }
}

QPointF expressionCenter(const QRectF &chassis, qreal topHeight, int expIndex)
{
    return QPointF(chassis.left() + 60.0 + expIndex * 94.0,
                   chassis.top() + topHeight * 0.335);
}

qreal expressionRadius(qreal topHeight)
{
    return qBound(18.0, topHeight * 0.215, 23.0);
}

QPainterPath roundedPanelPath(const QRectF &rect, qreal radius)
{
    QPainterPath path;
    path.addRoundedRect(rect, radius, radius);
    return path;
}

void drawMidiSocket(QPainter &painter, const QPointF &center, qreal radius)
{
    QLinearGradient bezel(center.x(), center.y() - radius, center.x(),
                          center.y() + radius);
    bezel.setColorAt(0.0, QColor(70, 75, 84));
    bezel.setColorAt(0.5, QColor(34, 37, 43));
    bezel.setColorAt(1.0, QColor(15, 16, 20));

    painter.setPen(QPen(QColor(8, 9, 11), 1.0));
    painter.setBrush(bezel);
    painter.drawEllipse(center, radius, radius);

    painter.setPen(QPen(QColor(78, 84, 94), 0.8));
    painter.setBrush(QColor(5, 6, 8));
    painter.drawEllipse(center, radius - 3.0, radius - 3.0);

    const QPointF pins[5] = {
        QPointF(-3.6, 2.8), QPointF(-1.8, -3.0), QPointF(1.8, -3.0),
        QPointF(3.6, 2.8),  QPointF(0.0, 4.6),
    };
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(91, 97, 108));
    for (const QPointF &pin : pins)
        painter.drawEllipse(center + pin, 1.0, 1.0);
}

void drawLed(QPainter &painter, const QPointF &center, bool lit, qreal radius)
{
    if (lit) {
        QRadialGradient glow(center, radius * 4.3);
        glow.setColorAt(0.0, QColor(242, 177, 82, 130));
        glow.setColorAt(0.45, QColor(242, 177, 82, 45));
        glow.setColorAt(1.0, QColor(242, 177, 82, 0));
        painter.setPen(Qt::NoPen);
        painter.setBrush(glow);
        painter.drawEllipse(center, radius * 4.3, radius * 4.3);
    }

    painter.setPen(QPen(lit ? QColor(255, 213, 145) : QColor(64, 68, 77), 0.9));
    painter.setBrush(lit ? QColor(244, 182, 88) : QColor(18, 20, 24));
    painter.drawEllipse(center, radius, radius);

    if (lit) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(255, 235, 202));
        painter.drawEllipse(center + QPointF(-radius * 0.25, -radius * 0.3),
                            radius * 0.3, radius * 0.3);
    }
}

} // namespace

DeviceWidget::DeviceWidget(QWidget *parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMinimumHeight(270);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setCursor(Qt::ArrowCursor);
    setToolTip(QStringLiteral(
        "Click a footswitch or expression input to edit it.\n"
        "Keys 1\u20136 select footswitches; \u2190/\u2192 cycle through all controls."));
    setAccessibleName(QStringLiteral("Live Logic MIDI Controller"));
    setAccessibleDescription(
        QStringLiteral("Six footswitches and two expression pedal inputs."));
}

void DeviceWidget::setProfile(const Profile &profile)
{
    m_profile = profile;
    update();
}

void DeviceWidget::setSelectedIndex(int index)
{
    if (index < 0 || index >= kControlCount || index == m_selected)
        return;
    m_selected = index;
    update();
}

void DeviceWidget::setConnected(bool connected)
{
    if (m_connected == connected)
        return;
    m_connected = connected;
    update();
}

QSize DeviceWidget::sizeHint() const
{
    return QSize(1160, 330);
}

QSize DeviceWidget::minimumSizeHint() const
{
    return QSize(940, 270);
}

QRectF DeviceWidget::chassisRect() const
{
    return QRectF(rect()).adjusted(12.0, 10.0, -12.0, -16.0);
}

QRectF DeviceWidget::faceRect() const
{
    const QRectF chassis = chassisRect();
    const qreal topHeight = qBound(86.0, chassis.height() * 0.33, 108.0);
    return QRectF(chassis.left(), chassis.top() + topHeight, chassis.width(),
                  chassis.height() - topHeight);
}

QRectF DeviceWidget::switchCellRect(int switchNumber) const
{
    const QRectF face = faceRect();
    const qreal leftMargin = 22.0;
    const qreal rightMargin = 22.0;
    const qreal cellWidth =
        (face.width() - leftMargin - rightMargin) / 6.0;
    return QRectF(face.left() + leftMargin + switchNumber * cellWidth,
                  face.top(), cellWidth, face.height());
}

QRectF DeviceWidget::expHitRect(int expNumber) const
{
    const QRectF chassis = chassisRect();
    const qreal topHeight = qBound(86.0, chassis.height() * 0.33, 108.0);
    const QPointF center = expressionCenter(chassis, topHeight, expNumber);
    const qreal radius = expressionRadius(topHeight);
    return QRectF(center.x() - 46.0, center.y() - radius - 6.0, 92.0,
                  radius * 2.0 + 40.0);
}

int DeviceWidget::selectionIndexAt(const QPointF &position) const
{
    for (int exp = 0; exp < 2; ++exp) {
        if (expHitRect(exp).contains(position))
            return exp;
    }
    for (int sw = 0; sw < 6; ++sw) {
        if (switchCellRect(sw).contains(position))
            return kSwitch1Index + sw;
    }
    return -1;
}

void DeviceWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    const QRectF chassis = chassisRect();
    if (chassis.width() < 100.0 || chassis.height() < 100.0)
        return;

    const qreal topHeight = qBound(86.0, chassis.height() * 0.33, 108.0);
    const QRectF face = faceRect();

    // Soft, layered shadow. Kept cheap so hover redraws stay smooth.
    painter.setPen(Qt::NoPen);
    for (int i = 8; i >= 1; --i) {
        painter.setBrush(QColor(0, 0, 0, 7));
        painter.drawRoundedRect(
            chassis.adjusted(-i * 0.45, i * 0.3, i * 0.45, i * 0.9), 22, 22);
    }

    QPainterPath body = roundedPanelPath(chassis, 20.0);
    QLinearGradient bodyGradient(chassis.topLeft(), chassis.bottomLeft());
    bodyGradient.setColorAt(0.0, QColor(38, 41, 47));
    bodyGradient.setColorAt(0.35, QColor(23, 25, 30));
    bodyGradient.setColorAt(1.0, QColor(12, 13, 16));
    painter.setPen(Qt::NoPen);
    painter.setBrush(bodyGradient);
    painter.drawPath(body);

    // Top control panel.
    painter.save();
    painter.setClipPath(body);
    QPainterPath topPanel;
    const qreal topRadius = 19.0;
    topPanel.moveTo(chassis.left() + topRadius, chassis.top());
    topPanel.lineTo(chassis.right() - topRadius, chassis.top());
    topPanel.quadTo(chassis.right(), chassis.top(), chassis.right(),
                    chassis.top() + topRadius);
    topPanel.lineTo(chassis.right(), chassis.top() + topHeight);
    topPanel.lineTo(chassis.left(), chassis.top() + topHeight);
    topPanel.lineTo(chassis.left(), chassis.top() + topRadius);
    topPanel.quadTo(chassis.left(), chassis.top(),
                    chassis.left() + topRadius, chassis.top());
    topPanel.closeSubpath();

    QLinearGradient topGradient(chassis.topLeft(),
                                QPointF(chassis.left(),
                                        chassis.top() + topHeight));
    topGradient.setColorAt(0.0, QColor(42, 45, 52));
    topGradient.setColorAt(0.55, QColor(31, 34, 40));
    topGradient.setColorAt(1.0, QColor(22, 24, 29));
    painter.fillPath(topPanel, topGradient);

    QLinearGradient faceOverlay(face.topLeft(), face.bottomLeft());
    faceOverlay.setColorAt(0.0, QColor(20, 22, 26));
    faceOverlay.setColorAt(1.0, QColor(14, 15, 18));
    painter.fillRect(face, faceOverlay);
    painter.restore();

    // Top highlight and panel seam.
    QLinearGradient topEdge(chassis.left(), 0, chassis.right(), 0);
    topEdge.setColorAt(0.0, QColor(255, 255, 255, 0));
    topEdge.setColorAt(0.2, QColor(255, 255, 255, 35));
    topEdge.setColorAt(0.8, QColor(255, 255, 255, 25));
    topEdge.setColorAt(1.0, QColor(255, 255, 255, 0));
    painter.setPen(QPen(QBrush(topEdge), 1.0));
    painter.drawLine(QPointF(chassis.left() + 24, chassis.top() + 1.0),
                     QPointF(chassis.right() - 24, chassis.top() + 1.0));

    QLinearGradient seam(chassis.left(), 0, chassis.right(), 0);
    seam.setColorAt(0.0, QColor(58, 63, 73, 0));
    seam.setColorAt(0.2, QColor(58, 63, 73, 220));
    seam.setColorAt(0.8, QColor(58, 63, 73, 220));
    seam.setColorAt(1.0, QColor(58, 63, 73, 0));
    painter.setPen(QPen(QBrush(seam), 1.0));
    painter.drawLine(QPointF(chassis.left() + 2, face.top()),
                     QPointF(chassis.right() - 2, face.top()));

    painter.save();
    painter.setClipPath(body);
    drawTopPanel(painter, chassis, topHeight);
    drawExpressionJacks(painter, chassis, topHeight);
    drawConnectors(painter, chassis, topHeight);
    drawDisplay(painter, chassis, topHeight);

    for (int sw = 0; sw < 6; ++sw) {
        drawSwitch(painter, sw, switchCellRect(sw));
    }
    painter.restore();

    drawScrews(painter, chassis);

    // Outer edge.
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(64, 69, 79), 1.2));
    painter.drawPath(body);
    painter.setPen(QPen(QColor(0, 0, 0, 120), 1.0));
    painter.drawRoundedRect(chassis.adjusted(1.2, 1.2, -1.2, -1.2), 18.5, 18.5);

    if (hasFocus()) {
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(QColor(232, 169, 77, 110), 1.2));
        painter.drawRoundedRect(rect().adjusted(5.0, 5.0, -5.0, -5.0), 17.0,
                                17.0);
    }
}

void DeviceWidget::drawTopPanel(QPainter &painter, const QRectF &chassis,
                                qreal topHeight)
{
    Q_UNUSED(chassis)
    Q_UNUSED(topHeight)

    // Faint brushed-metal streaks across the control panel.
    painter.save();
    painter.setPen(QPen(QColor(255, 255, 255, 5), 0.7));
    for (qreal y = chassisRect().top() + 12.0;
         y < chassisRect().top() + 80.0; y += 9.0) {
        painter.drawLine(QPointF(chassisRect().left() + 12.0, y),
                         QPointF(chassisRect().right() - 12.0, y));
    }
    painter.restore();
}

void DeviceWidget::drawExpressionJacks(QPainter &painter,
                                       const QRectF &chassis,
                                       qreal topHeight)
{
    const qreal radius = expressionRadius(topHeight);

    for (int exp = 0; exp < 2; ++exp) {
        const QPointF center = expressionCenter(chassis, topHeight, exp);
        const bool selected = m_selected == exp;
        const bool hovered = m_hovered == exp;

        if (selected || hovered) {
            QRadialGradient glow(center, radius + 26.0);
            const int glowAlpha = selected ? 85 : 30;
            glow.setColorAt(0.0, QColor(239, 172, 74, glowAlpha));
            glow.setColorAt(0.55, QColor(239, 172, 74, glowAlpha / 3));
            glow.setColorAt(1.0, QColor(239, 172, 74, 0));
            painter.setPen(Qt::NoPen);
            painter.setBrush(glow);
            painter.drawEllipse(center, radius + 26.0, radius + 26.0);
        }

        if (selected) {
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(QColor(242, 183, 90, 190), 2.0));
            painter.drawEllipse(center, radius + 4.0, radius + 4.0);
        } else if (hovered) {
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(QColor(190, 197, 208, 90), 1.2));
            painter.drawEllipse(center, radius + 3.0, radius + 3.0);
        }

        QLinearGradient bezel(center.x(), center.y() - radius, center.x(),
                              center.y() + radius);
        bezel.setColorAt(0.0, QColor(78, 83, 93));
        bezel.setColorAt(0.45, QColor(39, 42, 48));
        bezel.setColorAt(1.0, QColor(14, 15, 18));
        painter.setPen(QPen(QColor(8, 9, 11), 1.0));
        painter.setBrush(bezel);
        painter.drawEllipse(center, radius, radius);

        painter.setPen(QPen(QColor(93, 99, 110), 0.8));
        painter.setBrush(QColor(4, 5, 6));
        painter.drawEllipse(center, radius - 3.5, radius - 3.5);

        painter.setPen(QPen(QColor(47, 51, 58), 1.0));
        painter.setBrush(QColor(9, 10, 12));
        painter.drawEllipse(center, radius * 0.63, radius * 0.63);

        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(2, 2, 3));
        painter.drawEllipse(center, radius * 0.28, radius * 0.28);

        painter.setPen(QPen(QColor(255, 255, 255, 26), 1.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawArc(QRectF(center.x() - radius + 4.0,
                               center.y() - radius + 4.0,
                               (radius - 4.0) * 2.0, (radius - 4.0) * 2.0),
                        38 * 16, 95 * 16);

        // Selection LED beside the socket.
        drawLed(painter, QPointF(center.x() + radius + 5.0,
                                 center.y() - radius + 4.0),
                selected, 3.0);

        QFont labelFont = uiFont(9, QFont::DemiBold, 1.1);
        painter.setFont(labelFont);
        painter.setPen(selected ? QColor(244, 190, 106)
                                : QColor(188, 193, 202));
        QRectF labelRect(center.x() - 44.0, center.y() + radius + 4.0, 88.0, 13.0);
        painter.drawText(labelRect, Qt::AlignHCenter | Qt::AlignVCenter,
                         QStringLiteral("EXP %1").arg(exp + 1));

        painter.setFont(monoFont(8));
        painter.setPen(selected ? QColor(188, 145, 83)
                                : QColor(105, 111, 122));
        painter.drawText(QRectF(center.x() - 48.0,
                                center.y() + radius + 17.0, 96.0, 12.0),
                         Qt::AlignHCenter | Qt::AlignVCenter,
                         controlSummary(m_profile.controls[exp]));
    }
}

void DeviceWidget::drawConnectors(QPainter &painter, const QRectF &chassis,
                                  qreal topHeight)
{
    if (chassis.width() < 1000.0)
        return;

    const qreal centerY = chassis.top() + topHeight * 0.335;
    const qreal midiRadius = qBound(11.0, topHeight * 0.13, 13.5);
    const qreal centers[3] = {
        chassis.right() - 186.0, chassis.right() - 126.0,
        chassis.right() - 52.0,
    };

    for (int i = 0; i < 2; ++i) {
        drawMidiSocket(painter, QPointF(centers[i], centerY), midiRadius);
        painter.setFont(uiFont(7, QFont::DemiBold, 1.0));
        painter.setPen(QColor(112, 118, 129));
        painter.drawText(QRectF(centers[i] - 42.0,
                                centerY + midiRadius + 4.0, 84.0, 11.0),
                         Qt::AlignHCenter | Qt::AlignVCenter,
                         i == 0 ? QStringLiteral("MIDI IN")
                                : QStringLiteral("MIDI OUT"));
    }

    // USB-C socket, purely as part of the device illustration.
    QRectF usb(centers[2] - 12.0, centerY - 4.5, 24.0, 9.0);
    painter.setPen(QPen(QColor(81, 87, 98), 1.0));
    painter.setBrush(QColor(6, 7, 9));
    painter.drawRoundedRect(usb, 3.0, 3.0);
    painter.setPen(QPen(QColor(28, 31, 37), 1.0));
    painter.drawLine(usb.left() + 4.0, usb.center().y(),
                     usb.right() - 4.0, usb.center().y());

    painter.setFont(uiFont(7, QFont::DemiBold, 1.0));
    painter.setPen(QColor(112, 118, 129));
    painter.drawText(QRectF(centers[2] - 34.0, centerY + midiRadius + 4.0,
                            68.0, 11.0),
                     Qt::AlignHCenter | Qt::AlignVCenter,
                     QStringLiteral("USB"));
}

void DeviceWidget::drawDisplay(QPainter &painter, const QRectF &chassis,
                               qreal topHeight)
{
    const qreal width = qBound(220.0, chassis.width() * 0.23, 292.0);
    const qreal height = 24.0;
    const QRectF screen(chassis.center().x() - width / 2.0,
                        chassis.top() + topHeight - height - 13.0, width,
                        height);

    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 90));
    painter.drawRoundedRect(screen.adjusted(-1.0, 1.5, 1.0, 2.5), 6.0, 6.0);

    QLinearGradient glass(screen.topLeft(), screen.bottomLeft());
    glass.setColorAt(0.0, QColor(23, 27, 28));
    glass.setColorAt(1.0, QColor(5, 8, 9));
    painter.setBrush(glass);
    painter.setPen(QPen(QColor(54, 61, 65), 1.0));
    painter.drawRoundedRect(screen, 5.0, 5.0);

    painter.setPen(QPen(QColor(255, 255, 255, 14), 0.8));
    painter.drawLine(screen.topLeft() + QPointF(8, 1.5),
                     screen.topRight() + QPointF(-8, 1.5));

    const QPointF dot(screen.left() + 12.0, screen.center().y());
    if (m_connected) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(111, 211, 148, 45));
        painter.drawEllipse(dot, 6.0, 6.0);
    }
    painter.setPen(Qt::NoPen);
    painter.setBrush(m_connected ? QColor(111, 211, 148) : QColor(82, 88, 97));
    painter.drawEllipse(dot, 2.6, 2.6);

    QFont profileFont = monoFont(9, QFont::DemiBold, 0.7);
    painter.setFont(profileFont);
    painter.setPen(QColor(240, 190, 104));
    const QString profileName =
        elide(m_profile.name.isEmpty() ? QStringLiteral("TEMPLATE")
                                       : m_profile.name,
              profileFont, width - 94.0);
    painter.drawText(QRectF(screen.left() + 21.0, screen.top(), width - 94.0,
                            screen.height()),
                     Qt::AlignLeft | Qt::AlignVCenter, profileName);

    painter.setFont(uiFont(7, QFont::DemiBold, 1.2));
    painter.setPen(m_connected ? QColor(111, 191, 142) : QColor(99, 105, 115));
    painter.drawText(QRectF(screen.right() - 68.0, screen.top(), 56.0,
                            screen.height()),
                     Qt::AlignRight | Qt::AlignVCenter,
                     m_connected ? QStringLiteral("ONLINE")
                                 : QStringLiteral("OFFLINE"));
}

void DeviceWidget::drawSwitch(QPainter &painter, int switchNumber,
                              const QRectF &cell)
{
    const int selection = kSwitch1Index + switchNumber;
    const Control &control = m_profile.controls[selection];
    const QRectF face = faceRect();
    const bool selected = m_selected == selection;
    const bool hovered = m_hovered == selection;

    const qreal radius = qMax(
        34.0, qMin(cell.width() * 0.33,
                   qMin(62.0, (face.height() - 54.0) / 2.0)));
    const qreal contentHeight = radius * 2.0 + 54.0;
    const qreal contentTop =
        cell.top() + qMax(0.0, (cell.height() - contentHeight) / 2.0);

    const QPointF center(cell.center().x(), contentTop + 18.0 + radius);
    const QPointF ledCenter(center.x(), contentTop + 9.0);

    if (selected || hovered) {
        QRectF plate = cell.adjusted(3.0, 3.0, -3.0, -3.0);
        painter.setPen(selected ? QPen(QColor(190, 134, 54, 70), 1.0)
                                : QPen(QColor(255, 255, 255, 0), 1.0));
        painter.setBrush(selected ? QColor(232, 169, 77, 11)
                                  : QColor(255, 255, 255, 4));
        painter.drawRoundedRect(plate, 13.0, 13.0);
    }

    if (selected || hovered) {
        QRadialGradient glow(center, radius * 1.65);
        const int alpha = selected ? 80 : 26;
        glow.setColorAt(0.0, QColor(239, 172, 74, alpha));
        glow.setColorAt(0.55, QColor(239, 172, 74, alpha / 3));
        glow.setColorAt(1.0, QColor(239, 172, 74, 0));
        painter.setPen(Qt::NoPen);
        painter.setBrush(glow);
        painter.drawEllipse(center, radius * 1.65, radius * 1.65);
    }

    if (selected) {
        painter.setPen(QPen(QColor(242, 183, 90, 170), 1.8));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(center, radius + 3.5, radius + 3.5);
    } else if (hovered) {
        painter.setPen(QPen(QColor(202, 208, 218, 80), 1.1));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(center, radius + 2.5, radius + 2.5);
    }

    drawLed(painter, ledCenter, selected, 4.2);

    // Switch shadow.
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 110));
    painter.drawEllipse(center + QPointF(0.0, 4.5), radius, radius);

    QLinearGradient bezel(center.x(), center.y() - radius, center.x(),
                          center.y() + radius);
    bezel.setColorAt(0.0, QColor(80, 86, 96));
    bezel.setColorAt(0.35, QColor(45, 49, 56));
    bezel.setColorAt(1.0, QColor(14, 15, 18));
    painter.setPen(QPen(QColor(6, 7, 9), 1.0));
    painter.setBrush(bezel);
    painter.drawEllipse(center, radius, radius);

    painter.setPen(QPen(QColor(33, 36, 42), 1.0));
    painter.setBrush(QColor(11, 12, 15));
    painter.drawEllipse(center, radius - 4.5, radius - 4.5);

    QRadialGradient rubber(
        center + QPointF(-radius * 0.3, -radius * 0.35), radius * 1.35);
    rubber.setColorAt(0.0, QColor(50, 54, 62));
    rubber.setColorAt(0.48, QColor(31, 34, 40));
    rubber.setColorAt(0.82, QColor(20, 22, 26));
    rubber.setColorAt(1.0, QColor(12, 13, 16));
    painter.setPen(QPen(QColor(5, 6, 8), 1.0));
    painter.setBrush(rubber);
    painter.drawEllipse(center, radius - 9.0, radius - 9.0);

    painter.setPen(QPen(QColor(255, 255, 255, 24), 1.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawArc(QRectF(center.x() - radius + 13.0,
                           center.y() - radius + 13.0,
                           (radius - 13.0) * 2.0, (radius - 13.0) * 2.0),
                    40 * 16, 95 * 16);

    // Name and assignment.
    QFont nameFont = uiFont(11, QFont::Bold, 0.5);
    painter.setFont(nameFont);
    painter.setPen(selected ? QColor(246, 193, 108) : QColor(230, 226, 217));
    const QString name = control.name.isEmpty() ? QStringLiteral("\u2014")
                                                : control.name;
    const QString displayName = elide(name, nameFont, cell.width() - 14.0);
    const qreal nameTop = contentTop + 18.0 + radius * 2.0 + 6.0;
    painter.drawText(QRectF(cell.left(), nameTop, cell.width(), 15.0),
                     Qt::AlignHCenter | Qt::AlignVCenter, displayName);

    QFont subFont = monoFont(8);
    painter.setFont(subFont);
    painter.setPen(selected ? QColor(184, 142, 82) : QColor(108, 114, 125));
    const QString sub = QStringLiteral("%1  \u00b7  %2")
                            .arg(controlShortName(selection),
                                 controlSummary(control));
    painter.drawText(QRectF(cell.left(), nameTop + 15.0, cell.width(), 12.0),
                     Qt::AlignHCenter | Qt::AlignVCenter,
                     elide(sub, subFont, cell.width() - 10.0));
}

void DeviceWidget::drawScrews(QPainter &painter, const QRectF &chassis)
{
    const QPointF positions[2] = {
        QPointF(chassis.left() + 22.0, chassis.bottom() - 19.0),
        QPointF(chassis.right() - 22.0, chassis.bottom() - 19.0),
    };

    for (const QPointF &center : positions) {
        QRadialGradient screw(center + QPointF(-1.3, -1.3), 7.0);
        screw.setColorAt(0.0, QColor(54, 59, 68));
        screw.setColorAt(1.0, QColor(17, 19, 23));
        painter.setPen(QPen(QColor(8, 9, 11), 0.8));
        painter.setBrush(screw);
        painter.drawEllipse(center, 5.3, 5.3);
        painter.setPen(QPen(QColor(6, 7, 9), 1.2));
        painter.drawLine(center + QPointF(-2.7, -1.5),
                         center + QPointF(2.7, 1.5));
    }
}

void DeviceWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        const int index = selectionIndexAt(event->position());
        if (index >= 0) {
            setFocus(Qt::MouseFocusReason);
            emit controlSelected(index);
            event->accept();
            return;
        }
    }
    QWidget::mousePressEvent(event);
}

void DeviceWidget::mouseMoveEvent(QMouseEvent *event)
{
    const int hovered = selectionIndexAt(event->position());
    if (hovered != m_hovered) {
        m_hovered = hovered;
        setCursor(hovered >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update();
    }
    QWidget::mouseMoveEvent(event);
}

void DeviceWidget::leaveEvent(QEvent *event)
{
    if (m_hovered != -1) {
        m_hovered = -1;
        setCursor(Qt::ArrowCursor);
        update();
    }
    QWidget::leaveEvent(event);
}

void DeviceWidget::keyPressEvent(QKeyEvent *event)
{
    if (event->key() >= Qt::Key_1 && event->key() <= Qt::Key_6) {
        const int index = kSwitch1Index + (event->key() - Qt::Key_1);
        emit controlSelected(index);
        event->accept();
        return;
    }

    if (event->key() == Qt::Key_Left || event->key() == Qt::Key_Right) {
        static const int order[kControlCount] = {2, 3, 4, 5, 6, 7, 0, 1};
        int position = 0;
        for (int i = 0; i < kControlCount; ++i) {
            if (order[i] == m_selected) {
                position = i;
                break;
            }
        }
        const int delta = event->key() == Qt::Key_Right ? 1 : -1;
        position = (position + delta + kControlCount) % kControlCount;
        emit controlSelected(order[position]);
        event->accept();
        return;
    }

    QWidget::keyPressEvent(event);
}
