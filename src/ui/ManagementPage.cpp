#include "ManagementPage.h"

ManagementPage::ManagementPage(QString nodeId, QWidget *parent)
    : QWidget(parent), m_nodeId(std::move(nodeId)) {}

QStringList ManagementPage::actions() const { return {"Refresh"}; }
void ManagementPage::refresh() {}
void ManagementPage::triggerAction(const QString &action) { if (action == "Refresh") refresh(); }
void ManagementPage::announceActions() { emit actionsChanged(actions()); }

