#pragma once

#include "profile.h"

#include <QWidget>

class QPainter;
class QMouseEvent;
class QKeyEvent;

class DeviceWidget : public QWidget
{
    Q_OBJECT

public:
    explicit DeviceWidget(QWidget *parent = nullptr);

    void setProfile(const Profile &profile);
    void setSelectedIndex(int index);
    int selectedIndex() const { return m_selected; }
    void setConnected(bool connected);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    void controlSelected(int index);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    QRectF chassisRect() const;
    QRectF faceRect() const;
    QRectF switchCellRect(int switchNumber) const;
    QRectF expHitRect(int expNumber) const;
    int selectionIndexAt(const QPointF &position) const;

    void drawTopPanel(QPainter &painter, const QRectF &chassis, qreal topHeight);
    void drawExpressionJacks(QPainter &painter, const QRectF &chassis, qreal topHeight);
    void drawConnectors(QPainter &painter, const QRectF &chassis, qreal topHeight);
    void drawDisplay(QPainter &painter, const QRectF &chassis, qreal topHeight);
    void drawSwitch(QPainter &painter, int switchNumber, const QRectF &cell);
    void drawScrews(QPainter &painter, const QRectF &chassis);

    Profile m_profile;
    int m_selected = kDefaultSelectedIndex;
    int m_hovered = -1;
    bool m_connected = false;
};
