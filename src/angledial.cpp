#include "angledial.h"
#include <QMouseEvent>

int AngleDial::positionForAngle(int angle) { return (angle + 270) % 360; }

int AngleDial::angleForPosition(int position) { return (position + 90) % 360; }

void AngleDial::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && event->buttons() == Qt::NoButton
        && event->modifiers().testFlag(Qt::ShiftModifier)) {
        // QDial's default release replaces the position with the raw mouse
        // angle. Commit the snapped position instead.
        setSliderPosition((qRound(sliderPosition() / 15.0) * 15) % 360);
        setSliderDown(false);
        event->accept();
        return;
    }
    QDial::mouseReleaseEvent(event);
}
