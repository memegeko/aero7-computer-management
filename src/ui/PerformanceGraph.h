#pragma once

#include <QMap>
#include <QVector>
#include <QWidget>

class PerformanceGraph final : public QWidget {
    Q_OBJECT
public:
    explicit PerformanceGraph(QWidget *parent = nullptr);
    void addSample(const QMap<QString, double> &values);
    void setVisibleCounters(const QStringList &counters);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QMap<QString, QVector<double>> m_samples;
    QStringList m_visible;
    QMap<QString, QColor> m_colors;
};
