#pragma once

#include "backends/UDisksBackend.h"

#include <QDialog>
#include <QList>
#include <QString>
#include <QWizard>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QLabel;
class QRadioButton;
class QSpinBox;

class InitializeDiskDialog final : public QDialog {
public:
    InitializeDiskDialog(const QList<QPair<int, BlockDevice>> &disks, QWidget *parent = nullptr);
    QList<int> selectedDiskIndexes() const;
    QString partitionTableType() const;

private:
    QList<QPair<int, QCheckBox *>> m_diskChecks;
    QRadioButton *m_mbr = nullptr;
    QRadioButton *m_gpt = nullptr;
};

struct NewVolumeOptions {
    quint64 sizeBytes = 0;
    bool format = true;
    QString fileSystem;
    QString label;
    bool quickFormat = true;
};

class NewSimpleVolumeWizard final : public QWizard {
public:
    NewSimpleVolumeWizard(const DiskFreeRegion &region,
                          const QList<FileSystemCapability> &capabilities,
                          QWidget *parent = nullptr);
    NewVolumeOptions options() const;

private:
    quint64 m_regionSize = 0;
};

struct FormatVolumeOptions {
    QString fileSystem;
    QString label;
    bool quickFormat = true;
};

class FormatVolumeDialog final : public QDialog {
public:
    FormatVolumeDialog(const QString &volumeName, const QString &currentLabel,
                       const QString &currentFileSystem,
                       const QList<FileSystemCapability> &capabilities,
                       QWidget *parent = nullptr);
    FormatVolumeOptions options() const;

private:
    QLineEdit *m_label = nullptr;
    QComboBox *m_fileSystem = nullptr;
    QCheckBox *m_quick = nullptr;
};

class ExtendVolumeWizard final : public QWizard {
public:
    ExtendVolumeWizard(int diskNumber, quint64 currentSize, quint64 availableSize,
                       QWidget *parent = nullptr);
    quint64 additionalBytes() const;

private:
    quint64 m_currentSize = 0;
    quint64 m_availableSize = 0;
};

class ShrinkVolumeDialog final : public QDialog {
public:
    ShrinkVolumeDialog(const QString &volumeName, quint64 currentSize,
                       quint64 maximumShrink, QWidget *parent = nullptr);
    quint64 shrinkBytes() const;

private:
    quint64 m_currentSize = 0;
    QSpinBox *m_amount = nullptr;
    QLabel *m_after = nullptr;
};
