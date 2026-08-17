// SwipeSurface — the spec's §4.2 gesture surface, as a Qt6 QQuickItem.
// Captures a mouse glide (LMB press/move/release) with an active mouse grab,
// recording points normalized to the surface frame but UNCLAMPED (spec §6.3:
// overshoot preserved; clipping, if needed, belongs in the decoder adapter).
#pragma once
#include <QQuickItem>
#include <QVariantList>
#include <vector>

class SwipeSurface : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(bool swiping READ swiping NOTIFY swipingChanged)
    // Grid dimensions of the letter block, used only to make the tap/swipe
    // threshold isotropic in key units (see mouseReleaseEvent). Defaults match
    // QWERTY; set from QML if the layout ever changes shape.
    Q_PROPERTY(int cols READ cols WRITE setCols NOTIFY gridChanged)
    Q_PROPERTY(int rows READ rows WRITE setRows NOTIFY gridChanged)
public:
    explicit SwipeSurface(QQuickItem *parent = nullptr);
    bool swiping() const { return m_swiping; }
    int cols() const { return m_cols; }
    int rows() const { return m_rows; }
    void setCols(int c) { if (c > 0 && c != m_cols) { m_cols = c; emit gridChanged(); } }
    void setRows(int r) { if (r > 0 && r != m_rows) { m_rows = r; emit gridChanged(); } }

signals:
    void swipingChanged();
    void gridChanged();
    // points: list of {x, y, t} with x,y normalized-unclamped, t in ms since press
    void swipeCompleted(QVariantList points);
    // A tap (press+release with little movement) — type the key under it. nx,ny
    // are normalized to the surface frame, same convention as swipeCompleted.
    void tapped(qreal nx, qreal ny);
    // live normalized-unclamped cursor during a glide (for key-pop UI); same frame as swipeCompleted
    void cursorMoved(qreal nx, qreal ny);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    struct P { float x, y, t; };
    void addPoint(const QPointF &local, qint64 dtMs);

    bool m_swiping = false;
    std::vector<P> m_pts;
    quint64 m_t0 = 0;   // event timestamp (ms) at press
    int m_cols = 10;    // letter-block grid, for the key-unit tap threshold
    int m_rows = 3;
};
