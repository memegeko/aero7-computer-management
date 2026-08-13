#include "ui/MainWindow.h"
#include "ui/DiskDialogs.h"

#include <QApplication>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QSplitter>

int main(int argc, char **argv)
{
    QApplication application(argc, argv);
    QSettings("Aero7", "ComputerManagement").clear();

    MainWindow window;
    window.resize(1050, 680);
    window.show();
    application.processEvents();
    if (!window.openNode("performance")) return 1;
    application.processEvents();

    auto *splitter = window.findChild<QSplitter *>("managementSplitter");
    auto *summary = window.findChild<QLabel *>("performanceSummary");
    if (!splitter || !summary || splitter->sizes().size() != 3) return 2;

    const QList<int> before = splitter->sizes();
    summary->setText(QString(6000, 'W'));
    summary->updateGeometry();
    application.processEvents();
    const QList<int> after = splitter->sizes();
    if (before[0] != after[0] || before[2] != after[2]) return 3;
    if (!window.openNode("overview")) return 4;
    application.processEvents();
    auto *actionsPane = window.findChild<QWidget *>("actionsPane");
    int visibleActionButtons = 0;
    for (auto *button : actionsPane->findChildren<QPushButton *>())
        if (!button->isHidden()) ++visibleActionButtons;
    if (visibleActionButtons != 3) return 5;
    DiskFreeRegion region;
    region.size = 100ULL * 1024ULL * 1024ULL;
    FileSystemCapability ntfs;
    ntfs.type = "ntfs";
    ntfs.displayName = "NTFS";
    ntfs.canFormat = true;
    FileSystemCapability ext4;
    ext4.type = "ext4";
    ext4.displayName = "ext4";
    ext4.canFormat = true;
    const QList<FileSystemCapability> capabilities{ntfs, ext4};
    NewSimpleVolumeWizard createWizard(region, capabilities);
    auto *volumeSize = createWizard.findChild<QSpinBox *>("simpleVolumeSizeMiB");
    if (!volumeSize || volumeSize->value() != 100 || createWizard.options().sizeBytes != region.size)
        return 6;
    if (createWizard.findChild<QWidget *>("assignDriveLetter")
        || createWizard.findChild<QWidget *>("driveLetter")
        || createWizard.findChild<QWidget *>("mountFolder")) return 7;
    ExtendVolumeWizard extendWizard(1, 200ULL * 1024ULL * 1024ULL,
                                    50ULL * 1024ULL * 1024ULL);
    auto *extendAmount = extendWizard.findChild<QSpinBox *>("extendAmountMiB");
    if (!extendAmount || extendAmount->value() != 50
        || extendWizard.additionalBytes() != 50ULL * 1024ULL * 1024ULL)
        return 8;
    ShrinkVolumeDialog shrinkDialog("DATA", 200ULL * 1024ULL * 1024ULL,
                                    50ULL * 1024ULL * 1024ULL);
    auto *shrinkAmount = shrinkDialog.findChild<QSpinBox *>("shrinkAmountMiB");
    if (!shrinkAmount || shrinkAmount->value() != 50
        || shrinkDialog.shrinkBytes() != 50ULL * 1024ULL * 1024ULL)
        return 9;
    return 0;
}
