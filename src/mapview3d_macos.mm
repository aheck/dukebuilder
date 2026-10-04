#include "mapview3d.h"

#import <AppKit/AppKit.h>
#include <CoreGraphics/CGRemoteOperation.h>
#include <cmath>

bool MapView3D::beginMacMouseLook()
{
    // Disconnect pointer positioning from motion while keeping native motion
    // events. This avoids both synthetic events and per-event cursor warping.
    if (CGAssociateMouseAndMouseCursorPosition(false) != kCGErrorSuccess) {
        return false;
    }
    m_macRelativeMouse = true;
    m_macMouseCaptureTime = [[NSProcessInfo processInfo] systemUptime];
    return true;
}

void MapView3D::endMacMouseLook()
{
    if (m_macRelativeMouse) {
        CGAssociateMouseAndMouseCursorPosition(true);
        m_macRelativeMouse = false;
    }
}

bool MapView3D::nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result)
{
    Q_UNUSED(result);
    if (!m_active || !m_captured || !m_macRelativeMouse
        || eventType != "mac_generic_NSEvent" || !message) {
        return false;
    }
    NSEvent *event = static_cast<NSEvent *>(message);
    if (![NSApp isActive] || [event timestamp] < m_macMouseCaptureTime) {
        return false;
    }
    switch ([event type]) {
    case NSEventTypeMouseMoved:
    case NSEventTypeLeftMouseDragged:
    case NSEventTypeRightMouseDragged:
    case NSEventTypeOtherMouseDragged: {
        // Preserve fractional trackpad motion and apply each native delta once,
        // before Qt can coalesce absolute-position mouse events.
        const double horizontal = [event deltaX];
        const double vertical = [event deltaY];
        if (std::isfinite(horizontal) && std::isfinite(vertical)) {
            duke_camera_rotate(&m_camera, static_cast<float>(horizontal * 0.003),
                               static_cast<float>(-vertical * 0.003));
        }
        break;
    }
    default: {
        break;
    }
    }
    // Qt still receives clicks, scrolling and focus changes normally.
    return false;
}
