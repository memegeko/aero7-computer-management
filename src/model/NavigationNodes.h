#pragma once

#include <QList>
#include <QString>

struct NavigationNode {
    QString id;
    QString name;
    QString icon;
    QString parentId;
    bool selectable = true;
};

namespace NavigationNodes {
const QList<NavigationNode> &all();
const NavigationNode *find(const QString &id);
bool isValid(const QString &id);
QStringList validIds();
}

