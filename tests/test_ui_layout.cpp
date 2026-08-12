#include "ui/MainWindow.h"
#include "ui/DiskDialogs.h"

#include <QApplication>
#include <QLabel>
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
    DiskFreeRegion region;
    region.size = 100ULL * 1024ULL * 1024ULL;
    const QList<FileSystemCapability> capabilities{{"ntfs", "NTFS", true},
                                                    {"ext4", "ext4", true}};
    NewSimpleVolumeWizard createWizard(region, capabilities, {"D:", "E:"});
    auto *volumeSize = createWizard.findChild<QSpinBox *>("simpleVolumeSizeMiB");
    if (!volumeSize || volumeSize->value() != 100 || createWizard.options().sizeBytes != region.size)
        return 4;
    ExtendVolumeWizard extendWizard(1, 200ULL * 1024ULL * 1024ULL,
                                    50ULL * 1024ULL * 1024ULL);
    auto *extendAmount = extendWizard.findChild<QSpinBox *>("extendAmountMiB");
    if (!extendAmount || extendAmount->value() != 50
        || extendWizard.additionalBytes() != 50ULL * 1024ULL * 1024ULL)
        return 5;
    return 0;
}
