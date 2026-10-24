#pragma once

#include <QDial>

class AngleDial final : public QDial
{
public:
    using QDial::QDial;

    // Convert between Qt's bottom-origin dial and right-origin map angles.
    static int positionForAngle(int angle);
    static int angleForPosition(int position);

protected:
    void mouseReleaseEvent(QMouseEvent *event) override;
};
