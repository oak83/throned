#pragma once

#include <QObject>
#include <QPoint>
#include <QRect>
#include <Qt>

class QEvent;
class QWidget;

// A frameless window loses the native sizing border, so edge dragging has to be
// reproduced. The filter watches the whole application because the edges of the
// window are covered by ordinary child widgets, and those children are what the
// pointer actually lands on.
//
// Resizing is handed to the window manager when it accepts the request; the
// manual geometry drag is the fallback for platforms and window flags where it
// refuses.
class ThronedWindowResizer final : public QObject {
    Q_OBJECT
public:
    explicit ThronedWindowResizer(QWidget *window, int border = 6);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    [[nodiscard]] Qt::Edges edgesAt(const QPoint &globalPos) const;
    [[nodiscard]] bool resizable() const;
    void applyCursor(Qt::Edges edges);
    void endDrag();

    QWidget *window_;
    int border_;
    Qt::Edges dragEdges_;
    QRect dragGeometry_;
    QPoint dragOrigin_;
    bool cursorOverridden_ = false;
};
