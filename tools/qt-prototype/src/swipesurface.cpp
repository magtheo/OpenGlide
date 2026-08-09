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
