#pragma once

#include <QWidget>

class QLabel;
class QFrame;
class QPropertyAnimation;

class StarWidget : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(qreal glow READ glow WRITE setGlow)

public:
    explicit StarWidget(QWidget *parent = nullptr);

    void setActive(bool active);
    bool isActive() const { return m_active; }
    void setDiameter(int diameter);

    qreal glow() const { return m_glow; }
    void setGlow(qreal glow);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    bool m_active = false;
    qreal m_glow = 0.0;
    int m_diameter = 24;
    QPropertyAnimation *m_animation = nullptr;
};

QLabel *makeSectionLabel(const QString &text, QWidget *parent = nullptr);
QLabel *makeMutedLabel(const QString &text, QWidget *parent = nullptr);
QFrame *makeCard(QWidget *parent = nullptr);
QFrame *makeDivider(QWidget *parent = nullptr);

QString appStyleSheet();
