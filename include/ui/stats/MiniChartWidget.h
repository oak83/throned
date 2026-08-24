#pragma once

#include <QWidget>
#include <QColor>
#include <QList>

#include <deque>
#include <functional>
#include <vector>

struct MiniChartSeriesStyle {
    QColor color;
    Qt::PenStyle penStyle = Qt::SolidLine;
    bool fill = false;
};

// A lightweight rolling line chart. The vertical scale follows the normal data
// range while isolated spikes are clipped at, and visibly marked on, the upper
// edge. This keeps the useful part of a latency graph readable without hiding
// exceptional events. Negative samples represent a missing measurement and are
// drawn as crosses at the ceiling without taking part in auto-scaling.
class MiniChartWidget : public QWidget {
public:
    explicit MiniChartWidget(QWidget* parent = nullptr);

    // Rolling-window capacity (older samples fall off the left). Default 60.
    void setCapacity(int n);
    // Series colours; pass an invalid QColor to keep the palette-derived default.
    void setColors(const QColor& primary, const QColor& secondary);
    // Replace the series layout. Used by monitors that compare more than two
    // paths; changing it deliberately clears the old, incompatible history.
    void setSeriesStyles(const QList<MiniChartSeriesStyle>& styles);
    // Fixed vertical max; <= 0 (default) auto-scales to a nice ceiling.
    void setMaxValue(double m);
    // Formats a raw value for the scale labels (default: plain number).
    void setFormatter(std::function<QString(double)> formatter);
    // Caption drawn inside the panel, bottom-right (e.g. "CPU", "RAM").
    void setCaption(const QString& caption);
    // Append one sample per series and repaint.
    void push(double primary, double secondary);
    void pushValues(const QList<double>& values);
    void clear();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    struct SeriesData {
        std::deque<double> values;
        MiniChartSeriesStyle style;
    };

    std::vector<SeriesData> series_;
    int cap_ = 60;
    double fixedMax_ = -1.0;
    std::function<QString(double)> formatter_;
    QString caption_;
};
