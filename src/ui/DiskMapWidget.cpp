#include "DiskMapWidget.h"

#include "util/Format.h"

#include <QContextMenuEvent>
#include <QFontMetrics>
#include <QHash>
#include <QIcon>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>

#include <algorithm>

namespace {
constexpr int RowHeight = 92;
constexpr int LeftColumnWidth = 150;
constexpr int OuterMargin = 6;
constexpr int SegmentGap = 3;
constexpr quint64 MeaningfulGap = 1024ULL * 1024ULL;

struct Segment {
    int deviceIndex = -1;
    quint64 offset = 0;
    quint64 size = 0;
    bool unallocated = false;
    QString diskObjectPath;
    QString diskStableId;
    quint64 diskDeviceNumber = 0;
    quint64 diskSize = 0;
};

QString displayName(const BlockDevice &device)
{
    if (!device.label.isEmpty()) return device.label;
    if (!device.device.isEmpty()) return device.device;
    return "Volume";
}
}

DiskMapWidget::DiskMapWidget(QWidget *parent) : QWidget(parent)
{
    setAutoFillBackground(true);
    setMinimumWidth(560);
    setFocusPolicy(Qt::StrongFocus);
}

void DiskMapWidget::setDevices(const QList<BlockDevice> &devices)
{
    const QString selectedPath = m_selectedDevice >= 0 && m_selectedDevice < m_devices.size()
        ? m_devices[m_selectedDevice].objectPath : QString{};
    m_devices = devices;
    m_selectedDevice = -1;
    m_selectedFreeRegion.reset();
    if (!selectedPath.isEmpty()) {
        for (int index = 0; index < m_devices.size(); ++index) {
            if (m_devices[index].objectPath == selectedPath) {
                m_selectedDevice = index;
                break;
            }
        }
    }
    setMinimumHeight(sizeHint().height());
    updateGeometry();
    update();
}

void DiskMapWidget::selectFreeRegion(const DiskFreeRegion &region)
{
    m_selectedDevice = -1;
    m_selectedFreeRegion = region;
    update();
}

void DiskMapWidget::selectDevice(int deviceIndex)
{
    if (deviceIndex < -1 || deviceIndex >= m_devices.size())
        return;
    if (m_selectedDevice == deviceIndex && !m_selectedFreeRegion) return;
    m_selectedDevice = deviceIndex;
    m_selectedFreeRegion.reset();
    update();
}

QSize DiskMapWidget::sizeHint() const
{
    const int rowCount = int(groups().size());
    return {760, qMax(RowHeight + OuterMargin * 2,
                      rowCount * RowHeight + OuterMargin * 2)};
}

QList<DiskMapWidget::DiskGroup> DiskMapWidget::groups() const
{
    QList<DiskGroup> result;
    QHash<QString, int> groupByKey;
    for (int index = 0; index < m_devices.size(); ++index) {
        const BlockDevice &device = m_devices[index];
        const QString key = !device.driveObjectPath.isEmpty() && device.driveObjectPath != "/"
            ? device.driveObjectPath : device.objectPath;
        int groupIndex = groupByKey.value(key, -1);
        if (groupIndex < 0) {
            groupIndex = result.size();
            groupByKey.insert(key, groupIndex);
            result.push_back({key});
        }
        DiskGroup &group = result[groupIndex];
        if (!device.partition && group.diskIndex < 0)
            group.diskIndex = index;
        if (device.partition)
            group.volumeIndexes << index;
        group.capacity = qMax(group.capacity,
                              device.partition ? device.partitionOffset + device.size : device.size);
    }
    for (DiskGroup &group : result) {
        if (group.volumeIndexes.isEmpty() && group.diskIndex >= 0
            && (m_devices[group.diskIndex].mountable || !m_devices[group.diskIndex].fileSystem.isEmpty()))
            group.volumeIndexes << group.diskIndex;
        std::sort(group.volumeIndexes.begin(), group.volumeIndexes.end(), [this](int left, int right) {
            return m_devices[left].partitionOffset < m_devices[right].partitionOffset;
        });
    }
    return result;
}

QString DiskMapWidget::volumeStatus(const BlockDevice &device) const
{
    if (device.readOnly) return "Healthy (Read-only)";
    if (device.mountPoint == "/") return "Healthy (System, Mounted)";
    if (device.mountPoint == "/boot" || device.mountPoint == "/boot/efi")
        return "Healthy (EFI System Partition)";
    if (!device.mountPoint.isEmpty()) return "Healthy (Mounted)";
    return device.mountable ? "Healthy" : "Healthy";
}

void DiskMapWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), palette().base());
    painter.setRenderHint(QPainter::TextAntialiasing);
    m_hitTargets.clear();

    const QList<DiskGroup> diskGroups = groups();
    for (int diskNumber = 0; diskNumber < diskGroups.size(); ++diskNumber) {
        const DiskGroup &group = diskGroups[diskNumber];
        const BlockDevice *disk = group.diskIndex >= 0 ? &m_devices[group.diskIndex] : nullptr;
        const int rowTop = OuterMargin + diskNumber * RowHeight;
        const QRect leftRect(OuterMargin, rowTop, LeftColumnWidth - OuterMargin, RowHeight - OuterMargin);
        const int leftSelection = group.diskIndex >= 0 ? group.diskIndex
            : group.volumeIndexes.value(0, -1);
        if (leftSelection == m_selectedDevice)
            painter.fillRect(leftRect, QColor("#d8eafa"));
        painter.setPen(QColor("#a5a5a5"));
        painter.drawRect(leftRect.adjusted(0, 0, -1, -1));
        const QIcon icon = QIcon::fromTheme(disk && disk->optical ? "drive-optical" : "drive-harddisk");
        icon.paint(&painter, QRect(leftRect.left() + 7, leftRect.top() + 8, 28, 28));
        painter.setPen(palette().text().color());
        QFont bold = painter.font();
        bold.setBold(true);
        painter.setFont(bold);
        painter.drawText(QRect(leftRect.left() + 40, leftRect.top() + 7,
                               leftRect.width() - 44, 20), Qt::AlignLeft | Qt::AlignVCenter,
                         QString("Disk %1").arg(diskNumber));
        painter.setFont(font());
        const bool initialized = disk && !disk->partitionTable.isEmpty();
        const QString kind = !initialized ? "Unknown" : disk->removable ? "Removable" : "Basic";
        painter.drawText(leftRect.left() + 40, leftRect.top() + 43, kind);
        painter.drawText(leftRect.left() + 40, leftRect.top() + 59, Format::bytes(group.capacity));
        painter.drawText(leftRect.left() + 40, leftRect.top() + 75,
                         !initialized ? "Not Initialized"
                                      : disk && disk->readOnly ? "Read-only" : "Online");
        m_hitTargets << HitTarget{leftRect, leftSelection, std::nullopt};

        QList<Segment> segments;
        quint64 cursor = 0;
        for (const int index : group.volumeIndexes) {
            const BlockDevice &volume = m_devices[index];
            const quint64 offset = volume.partition ? volume.partitionOffset : 0;
            if (offset > cursor + MeaningfulGap)
                segments << Segment{-1, cursor, offset - cursor, true,
                    disk ? disk->objectPath : QString{}, disk ? disk->stableId : QString{},
                    disk ? disk->deviceNumber : 0, group.capacity};
            segments << Segment{index, offset, volume.size, false};
            cursor = qMax(cursor, offset + volume.size);
        }
        if ((!disk || !disk->partitionTable.isEmpty())
            && group.capacity > cursor + MeaningfulGap)
            segments << Segment{-1, cursor, group.capacity - cursor, true,
                disk ? disk->objectPath : QString{}, disk ? disk->stableId : QString{},
                disk ? disk->deviceNumber : 0, group.capacity};

        const int areaLeft = LeftColumnWidth + OuterMargin;
        const int areaWidth = qMax(1, width() - areaLeft - OuterMargin);
        if (segments.isEmpty()) {
            const QRect emptyRect(areaLeft, rowTop, areaWidth, RowHeight - OuterMargin);
            painter.fillRect(emptyRect, QColor("#f1f1f1"));
            painter.setPen(QColor("#8b8b8b"));
            painter.drawRect(emptyRect.adjusted(0, 0, -1, -1));
            painter.drawText(emptyRect, Qt::AlignCenter,
                             disk && disk->partitionTable.isEmpty() ? "Not Initialized"
                             : group.capacity ? "Unallocated" : "No media");
            m_hitTargets << HitTarget{emptyRect, leftSelection, std::nullopt};
            continue;
        }

        const int usableWidth = qMax(1, areaWidth - SegmentGap * (segments.size() - 1));
        QList<int> widths;
        int desiredTotal = 0;
        for (const Segment &segment : segments) {
            const double fraction = group.capacity ? double(segment.size) / double(group.capacity) : 1.0;
            const int desired = qMax(72, int(fraction * usableWidth));
            widths << desired;
            desiredTotal += desired;
        }
        if (desiredTotal > usableWidth) {
            for (int &segmentWidth : widths)
                segmentWidth = qMax(48, segmentWidth * usableWidth / desiredTotal);
        }

        int x = areaLeft;
        int remainingWidth = areaWidth;
        for (int segmentNumber = 0; segmentNumber < segments.size(); ++segmentNumber) {
            const Segment &segment = segments[segmentNumber];
            const int remainingSegments = segments.size() - segmentNumber;
            const int reservedGaps = qMax(0, remainingSegments - 1) * SegmentGap;
            const int availableForSegment = qMax(1, remainingWidth - reservedGaps);
            const int segmentWidth = segmentNumber == segments.size() - 1
                ? availableForSegment : qMin(widths[segmentNumber], availableForSegment);
            const QRect segmentRect(x, rowTop, qMax(1, segmentWidth), RowHeight - OuterMargin);
            const bool selected = segment.deviceIndex == m_selectedDevice
                || (segment.unallocated && m_selectedFreeRegion
                    && m_selectedFreeRegion->diskObjectPath == segment.diskObjectPath
                    && m_selectedFreeRegion->offset == segment.offset
                    && m_selectedFreeRegion->size == segment.size);
            painter.fillRect(segmentRect, selected ? QColor("#d8eafa")
                                                   : segment.unallocated ? QColor("#f2f2f2") : Qt::white);
            painter.fillRect(QRect(segmentRect.left(), segmentRect.top(), segmentRect.width(), 7),
                             segment.unallocated ? Qt::black : QColor("#103a89"));
            painter.setPen(selected ? QColor("#2d75b5") : QColor("#777777"));
            painter.drawRect(segmentRect.adjusted(0, 0, -1, -1));
            painter.save();
            painter.setClipRect(segmentRect.adjusted(4, 9, -4, -3));
            painter.setPen(palette().text().color());
            if (segment.unallocated) {
                painter.setFont(bold);
                painter.drawText(segmentRect.adjusted(5, 15, -5, -5), Qt::AlignHCenter | Qt::AlignTop,
                                 Format::bytes(segment.size));
                painter.setFont(font());
                painter.drawText(segmentRect.adjusted(5, 34, -5, -5), Qt::AlignHCenter | Qt::AlignTop,
                                 "Unallocated");
            } else {
                const BlockDevice &volume = m_devices[segment.deviceIndex];
                const QFontMetrics metrics(bold);
                painter.setFont(bold);
                painter.drawText(segmentRect.adjusted(5, 13, -5, -5), Qt::AlignHCenter | Qt::AlignTop,
                                 metrics.elidedText(displayName(volume), Qt::ElideRight,
                                                    segmentRect.width() - 10));
                painter.setFont(font());
                painter.drawText(segmentRect.adjusted(5, 32, -5, -5), Qt::AlignHCenter | Qt::AlignTop,
                                 QString("%1  %2").arg(Format::bytes(volume.size),
                                                       volume.fileSystem.isEmpty() ? "Unknown" : volume.fileSystem));
                painter.drawText(segmentRect.adjusted(5, 50, -5, -5), Qt::AlignHCenter | Qt::AlignTop,
                                 volumeStatus(volume));
            }
            painter.restore();
            std::optional<DiskFreeRegion> freeRegion;
            if (segment.unallocated) freeRegion = DiskFreeRegion{segment.diskObjectPath,
                segment.diskStableId, segment.diskDeviceNumber, segment.diskSize,
                segment.offset, segment.size};
            m_hitTargets << HitTarget{segmentRect,
                                      segment.unallocated ? leftSelection : segment.deviceIndex,
                                      freeRegion};
            x += segmentRect.width() + SegmentGap;
            remainingWidth -= segmentRect.width() + SegmentGap;
        }
    }
}

std::optional<DiskMapWidget::HitTarget> DiskMapWidget::hitTest(const QPoint &position) const
{
    for (auto iterator = m_hitTargets.crbegin(); iterator != m_hitTargets.crend(); ++iterator) {
        if (iterator->rectangle.contains(position)) return *iterator;
    }
    return std::nullopt;
}

void DiskMapWidget::mousePressEvent(QMouseEvent *event)
{
    const auto target = hitTest(event->position().toPoint());
    if (target) {
        DiskMapSelection selection;
        if (target->freeRegion) {
            selectFreeRegion(*target->freeRegion);
            selection.kind = DiskMapSelection::Unallocated;
            selection.freeRegion = *target->freeRegion;
        } else {
            selectDevice(target->deviceIndex);
            m_selectedFreeRegion.reset();
            selection.kind = DiskMapSelection::Device;
            selection.deviceIndex = target->deviceIndex;
            emit selectionChanged(target->deviceIndex);
        }
        emit mapSelectionChanged(selection);
        setFocus();
    }
    QWidget::mousePressEvent(event);
}

void DiskMapWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    const auto target = hitTest(event->position().toPoint());
    if (target && !target->freeRegion && target->deviceIndex >= 0)
        emit deviceActivated(target->deviceIndex);
    QWidget::mouseDoubleClickEvent(event);
}

void DiskMapWidget::contextMenuEvent(QContextMenuEvent *event)
{
    const auto target = hitTest(event->pos());
    if (target) {
        DiskMapSelection selection;
        if (target->freeRegion) {
            selectFreeRegion(*target->freeRegion);
            selection.kind = DiskMapSelection::Unallocated;
            selection.freeRegion = *target->freeRegion;
        } else {
            selectDevice(target->deviceIndex);
            m_selectedFreeRegion.reset();
            selection.kind = DiskMapSelection::Device;
            selection.deviceIndex = target->deviceIndex;
            emit selectionChanged(target->deviceIndex);
            emit contextMenuRequested(target->deviceIndex, event->globalPos());
        }
        emit mapSelectionChanged(selection);
        emit mapContextMenuRequested(selection, event->globalPos());
        event->accept();
        return;
    }
    QWidget::contextMenuEvent(event);
}
