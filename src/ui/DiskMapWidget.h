#pragma once

#include "backends/UDisksBackend.h"

#include <QPoint>
#include <QRect>
#include <QWidget>

class DiskMapWidget final : public QWidget {
    Q_OBJECT
public:
    explicit DiskMapWidget(QWidget *parent = nullptr);

    void setDevices(const QList<BlockDevice> &devices);
    void selectDevice(int deviceIndex);
    int selectedDevice() const { return m_selectedDevice; }
    QSize sizeHint() const override;

signals:
    void selectionChanged(int deviceIndex);
    void deviceActivated(int deviceIndex);
    void contextMenuRequested(int deviceIndex, const QPoint &globalPosition);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    struct HitTarget {
        QRect rectangle;
        int deviceIndex = -1;
    };

    struct DiskGroup {
        QString key;
        int diskIndex = -1;
        QList<int> volumeIndexes;
        quint64 capacity = 0;
    };

    QList<DiskGroup> groups() const;
    int hitTest(const QPoint &position) const;
    QString volumeStatus(const BlockDevice &device) const;

    QList<BlockDevice> m_devices;
    QList<HitTarget> m_hitTargets;
    int m_selectedDevice = -1;
};
