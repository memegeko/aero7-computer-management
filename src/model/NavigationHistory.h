#pragma once

#include <QStringList>

class NavigationHistory {
public:
    void visit(const QString &id);
    bool canGoBack() const;
    bool canGoForward() const;
    QString back();
    QString forward();
    QString current() const;

private:
    QStringList m_entries;
    int m_index = -1;
};

