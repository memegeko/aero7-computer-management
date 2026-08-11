#include "PerformanceGraph.h"

#include <QPainter>
#include <QPainterPath>

PerformanceGraph::PerformanceGraph(QWidget *parent) : QWidget(parent)
{
    setMinimumHeight(250);
    setAutoFillBackground(true);
    m_colors = {{"CPU usage", QColor("#d43b3b")}, {"Memory used", QColor("#3274c8")},
                {"Swap used", QColor("#8e44ad")}, {"Disk throughput", QColor("#d99116")},
                {"Network throughput", QColor("#269b4d")}};
}

void PerformanceGraph::addSample(const QMap<QString, double> &values)
{
    for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
        QVector<double> &history = m_samples[it.key()];
        history << qBound(0.0, it.value(), 100.0);
        while (history.size() > 120) history.removeFirst();
    }
    update();
}

void PerformanceGraph::setVisibleCounters(const QStringList &counters)
{
    m_visible = counters;
    update();
}

void PerformanceGraph::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), Qt::white);
    const QRectF graph = rect().adjusted(42, 16, -14, -35);
    painter.setPen(QColor("#d7d7d7"));
    for (int i = 0; i <= 10; ++i) {
        const qreal y = graph.top() + graph.height() * i / 10.0;
        painter.drawLine(QPointF(graph.left(), y), QPointF(graph.right(), y));
    }
    painter.setPen(QColor("#6f6f6f"));
    painter.drawRect(graph);
    painter.drawText(QRectF(2, graph.top() - 6, 38, 20), Qt::AlignRight, "100");
    painter.drawText(QRectF(2, graph.bottom() - 10, 38, 20), Qt::AlignRight, "0");

    for (const QString &name : m_visible) {
        const QVector<double> values = m_samples.value(name);
        if (values.size() < 2) continue;
        QPainterPath path;
        for (int i = 0; i < values.size(); ++i) {
            const qreal x = graph.right() - (values.size() - 1 - i) * graph.width() / 119.0;
            const qreal y = graph.bottom() - values[i] * graph.height() / 100.0;
            if (i == 0) path.moveTo(x, y); else path.lineTo(x, y);
        }
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(m_colors.value(name, Qt::black), 1.5));
        painter.drawPath(path);
    }

    int legendX = int(graph.left());
    const int legendY = height() - 22;
    for (const QString &name : m_visible) {
        painter.fillRect(legendX, legendY + 4, 12, 3, m_colors.value(name, Qt::black));
        painter.setPen(Qt::black);
        painter.drawText(legendX + 17, legendY + 11, name);
        legendX += painter.fontMetrics().horizontalAdvance(name) + 42;
    }
}
