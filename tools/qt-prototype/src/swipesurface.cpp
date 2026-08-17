#include "swipesurface.h"
#include <QHoverEvent>
#include <QMouseEvent>
#include <QLineF>

SwipeSurface::SwipeSurface(QQuickItem *parent) : QQuickItem(parent) {
    setAcceptedMouseButtons(Qt::LeftButton);
    setKeepMouseGrab(true);   // keep the grab through the whole glide
    // Hover is not decoration here: it is the only way the user can see which
    // key a glide would START on, which RESULTS.md measures as the difference
    // between 81% and 94% dict top-1. Hover reaches a window that never takes
    // keyboard focus — the chrome's own controls already highlight on
    // containsMouse across every shipped window mode (managed xcb, override-
    // redirect, layer-shell), so the delivery path is proven, not assumed.
    setAcceptHoverEvents(true);
}

void SwipeSurface::addPoint(const QPointF &local, qint64 dtMs) {
    const float w = width(), h = height();
    P p;
    p.x = (w > 0) ? float(local.x() / w) : 0.0f;   // normalized, UNCLAMPED
    p.y = (h > 0) ? float(local.y() / h) : 0.0f;
    p.t = float(dtMs);
    m_pts.push_back(p);
    emit cursorMoved(p.x, p.y);
}

void SwipeSurface::hoverMoveEvent(QHoverEvent *event) {
    event->accept();
    // During a glide the key highlight belongs to cursorMoved (which reports
    // unclamped points, including outside the surface); two writers on one
    // highlight would fight.
    if (m_swiping) return;
    const qreal w = width(), h = height();
    emit hoverMoved((w > 0) ? event->position().x() / w : 0.0,
                    (h > 0) ? event->position().y() / h : 0.0);
}

void SwipeSurface::hoverLeaveEvent(QHoverEvent *event) {
    event->accept();
    emit hoverLeft();
}

void SwipeSurface::mousePressEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton) return;
    event->accept();
    grabMouse();
    m_swiping = true;
    emit swipingChanged();
    m_pts.clear();
    m_t0 = event->timestamp();
    addPoint(event->position(), 0);
}

void SwipeSurface::mouseMoveEvent(QMouseEvent *event) {
    if (!m_swiping) return;
    event->accept();
    addPoint(event->position(), qint64(event->timestamp()) - qint64(m_t0));
}

void SwipeSurface::mouseReleaseEvent(QMouseEvent *event) {
    if (!m_swiping) return;
    event->accept();
    addPoint(event->position(), qint64(event->timestamp()) - qint64(m_t0));
    m_swiping = false;
    emit swipingChanged();
    ungrabMouse();

    // Tap vs swipe: a tap stays on one key. Measure displacement in KEY WIDTHS,
    // not in raw normalized units — those are anisotropic (keys are 1/cols apart
    // in x but 1/rows apart in y, i.e. 0.1 vs 0.333 on QWERTY). Comparing dx and
    // dy against one normalized threshold made it 0.50 of a key horizontally and
    // 0.15 vertically, so a tap with slight downward drift was classified as a
    // glide and decoded to garbage. Dividing each axis by its own key pitch makes
    // the threshold mean the same thing in both directions.
    const float TAP_THRESH2 = 0.35f * 0.35f;   // fraction of a key, squared
    float maxd2 = 0.0f;
    for (const P &p : m_pts) {
        const float dx = (p.x - m_pts[0].x) * float(m_cols);   // -> key widths
        const float dy = (p.y - m_pts[0].y) * float(m_rows);   // -> key heights
        const float d2 = dx * dx + dy * dy;
        if (d2 > maxd2) maxd2 = d2;
    }
    if (maxd2 <= TAP_THRESH2) {
        emit tapped(m_pts[0].x, m_pts[0].y);   // the key that was pressed
        return;
    }

    QVariantList out;
    out.reserve(int(m_pts.size()));
    for (const P &p : m_pts) {
        QVariantMap m;
        m["x"] = p.x;
        m["y"] = p.y;
        m["t"] = p.t;
        out.append(m);
    }
    emit swipeCompleted(out);
}
