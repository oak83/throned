#include "include/ui/widget/ThronedWindowResizer.h"

#include <QApplication>
#include <QMouseEvent>
#include <QWidget>
#include <QWindow>

namespace {

Qt::CursorShape cursorForEdges(Qt::Edges edges) {
    if ((edges & Qt::TopEdge && edges & Qt::LeftEdge) || (edges & Qt::BottomEdge && edges & Qt::RightEdge))
        return Qt::SizeFDiagCursor;
    if ((edges & Qt::TopEdge && edges & Qt::RightEdge) || (edges & Qt::BottomEdge && edges & Qt::LeftEdge))
        return Qt::SizeBDiagCursor;
    if (edges & (Qt::LeftEdge | Qt::RightEdge)) return Qt::SizeHorCursor;
    return Qt::SizeVerCursor;
}

} // namespace

ThronedWindowResizer::ThronedWindowResizer(QWidget *window, int border)
    : QObject(window), window_(window), border_(border) {
    if (qApp) qApp->installEventFilter(this);
}

bool ThronedWindowResizer::resizable() const {
    return window_ && window_->isVisible() && !window_->isMaximized() && !window_->isFullScreen();
}

Qt::Edges ThronedWindowResizer::edgesAt(const QPoint &globalPos) const {
    const QRect frame = window_->frameGeometry();
    if (!frame.adjusted(-border_, -border_, border_, border_).contains(globalPos)) return {};
    Qt::Edges edges;
    if (globalPos.x() <= frame.left() + border_) edges |= Qt::LeftEdge;
    else if (globalPos.x() >= frame.right() - border_) edges |= Qt::RightEdge;
    if (globalPos.y() <= frame.top() + border_) edges |= Qt::TopEdge;
    else if (globalPos.y() >= frame.bottom() - border_) edges |= Qt::BottomEdge;
    return edges;
}

void ThronedWindowResizer::applyCursor(Qt::Edges edges) {
    if (edges) {
        const Qt::CursorShape shape = cursorForEdges(edges);
        if (cursorOverridden_) QApplication::changeOverrideCursor(shape);
        else QApplication::setOverrideCursor(shape);
        cursorOverridden_ = true;
    } else if (cursorOverridden_) {
        QApplication::restoreOverrideCursor();
        cursorOverridden_ = false;
    }
}

void ThronedWindowResizer::endDrag() {
    if (!dragEdges_) return;
    dragEdges_ = {};
    window_->releaseMouse();
}

bool ThronedWindowResizer::eventFilter(QObject *watched, QEvent *event) {
    const QEvent::Type type = event->type();
    if (type != QEvent::MouseMove && type != QEvent::MouseButtonPress && type != QEvent::MouseButtonRelease)
        return QObject::eventFilter(watched, event);

    auto *widget = qobject_cast<QWidget *>(watched);
    if (!widget || !window_ || widget->window() != window_) return QObject::eventFilter(watched, event);

    auto *mouse = static_cast<QMouseEvent *>(event);
    const QPoint globalPos = mouse->globalPosition().toPoint();

    if (dragEdges_) {
        if (type == QEvent::MouseMove) {
            const QPoint delta = globalPos - dragOrigin_;
            QRect geometry = dragGeometry_;
            if (dragEdges_ & Qt::LeftEdge) geometry.setLeft(geometry.left() + delta.x());
            if (dragEdges_ & Qt::RightEdge) geometry.setRight(geometry.right() + delta.x());
            if (dragEdges_ & Qt::TopEdge) geometry.setTop(geometry.top() + delta.y());
            if (dragEdges_ & Qt::BottomEdge) geometry.setBottom(geometry.bottom() + delta.y());
            const QSize minimum = window_->minimumSize().expandedTo(window_->minimumSizeHint());
            // Clamp against the anchored side so the window stops instead of
            // flipping over once the pointer passes the opposite edge.
            if (geometry.width() < minimum.width()) {
                if (dragEdges_ & Qt::LeftEdge) geometry.setLeft(geometry.right() - minimum.width());
                else geometry.setRight(geometry.left() + minimum.width());
            }
            if (geometry.height() < minimum.height()) {
                if (dragEdges_ & Qt::TopEdge) geometry.setTop(geometry.bottom() - minimum.height());
                else geometry.setBottom(geometry.top() + minimum.height());
            }
            window_->setGeometry(geometry);
            return true;
        }
        if (type == QEvent::MouseButtonRelease) {
            endDrag();
            applyCursor(resizable() ? edgesAt(globalPos) : Qt::Edges{});
            return true;
        }
        return true;
    }

    if (!resizable()) {
        applyCursor({});
        return QObject::eventFilter(watched, event);
    }

    const Qt::Edges edges = edgesAt(globalPos);
    if (type == QEvent::MouseMove) {
        if (mouse->buttons() == Qt::NoButton) applyCursor(edges);
        return QObject::eventFilter(watched, event);
    }
    if (type != QEvent::MouseButtonPress || mouse->button() != Qt::LeftButton || !edges)
        return QObject::eventFilter(watched, event);

    if (QWindow *handle = window_->windowHandle(); handle && handle->startSystemResize(edges)) {
        applyCursor({});
        return true;
    }
    dragEdges_ = edges;
    dragGeometry_ = window_->geometry();
    dragOrigin_ = globalPos;
    window_->grabMouse();
    return true;
}
