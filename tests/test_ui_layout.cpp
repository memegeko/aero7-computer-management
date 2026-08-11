#include "ui/MainWindow.h"

#include <QApplication>
#include <QLabel>
#include <QSettings>
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
    return 0;
}
