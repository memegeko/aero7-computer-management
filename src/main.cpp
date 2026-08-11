#include "model/NavigationNodes.h"
#include "ui/MainWindow.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>

int main(int argc,char **argv)
{
    QApplication app(argc,argv);app.setOrganizationName("Aero7");app.setApplicationName("ComputerManagement");app.setApplicationDisplayName("Computer Management");app.setWindowIcon(QIcon::fromTheme("computer"));
    QCommandLineParser parser;parser.setApplicationDescription("Aero7 Computer Management console");parser.addHelpOption();parser.addVersionOption();
    QCommandLineOption open({"o","open"},"Open a management page by stable node ID.","node");parser.addOption(open);
    QCommandLineOption listSettings("list-settings-json","Print the standalone Start-menu search catalog as JSON and exit.");
    parser.addOption(listSettings);
    parser.process(app);
    if(parser.isSet(listSettings)){
        QJsonArray catalog;
        for(const auto &node:NavigationNodes::all()){
            if(!node.selectable)continue;
            catalog.append(QJsonObject{
                {"key",node.id},{"name",node.name},{"description",QString("Open %1 in Computer Management.").arg(node.name)},
                {"icon",node.icon},{"section","Computer Management"},{"keywords",QString("%1 administration management settings linux").arg(node.id)}
            });
        }
        QTextStream(stdout)<<QJsonDocument(catalog).toJson(QJsonDocument::Compact)<<Qt::endl;
        return 0;
    }
    const QString requested=parser.value(open).trimmed();if(!requested.isEmpty()&&!NavigationNodes::isValid(requested)){QTextStream(stderr)<<"Unknown management node: "<<requested<<"\nValid nodes: "<<NavigationNodes::validIds().join(", ")<<Qt::endl;return 2;}
    MainWindow window;if(!requested.isEmpty())window.openNode(requested);window.show();return app.exec();
}
