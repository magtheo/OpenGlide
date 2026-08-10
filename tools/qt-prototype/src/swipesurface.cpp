#include "swipesurface.h"
#include <QMouseEvent>
#include <QLineF>

SwipeSurface::SwipeSurface(QQuickItem *parent) : QQuickItem(parent) {
    setAcceptedMouseButtons(Qt::LeftButton);
    setKeepMouseGrab(true);   // keep the grab through the whole glide
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

    // Tap vs swipe: a tap stays on one key (small total displacement from the
    // press point). Keys are ~0.1 apart normalized, so <0.05 means it never left
    // the pressed key. Emit tapped instead of swipeCompleted in that case.
    const float TAP_THRESH2 = 0.05f * 0.05f;
    float maxd2 = 0.0f;
    for (const P &p : m_pts) {
        float dx = p.x - m_pts[0].x, dy = p.y - m_pts[0].y;
        float d2 = dx * dx + dy * dy;
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
