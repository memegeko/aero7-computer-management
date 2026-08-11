#include "model/NavigationNodes.h"
#include "ui/MainWindow.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QTextStream>

int main(int argc,char **argv)
{
    QApplication app(argc,argv);app.setOrganizationName("Aero7");app.setApplicationName("ComputerManagement");app.setApplicationDisplayName("Computer Management");app.setWindowIcon(QIcon::fromTheme("computer"));
    QCommandLineParser parser;parser.setApplicationDescription("Aero7 Computer Management console");parser.addHelpOption();parser.addVersionOption();
    QCommandLineOption open({"o","open"},"Open a management page by stable node ID.","node");parser.addOption(open);parser.process(app);
    const QString requested=parser.value(open).trimmed();if(!requested.isEmpty()&&!NavigationNodes::isValid(requested)){QTextStream(stderr)<<"Unknown management node: "<<requested<<"\nValid nodes: "<<NavigationNodes::validIds().join(", ")<<Qt::endl;return 2;}
    MainWindow window;if(!requested.isEmpty())window.openNode(requested);window.show();return app.exec();
}

