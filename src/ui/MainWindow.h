#pragma once

#include "model/NavigationHistory.h"

#include <QHash>
#include <QMainWindow>

class QAction;
class QSplitter;
class QStackedWidget;
class QToolBar;
class QTreeWidget;
class QTreeWidgetItem;
class QVBoxLayout;
class ManagementPage;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;
    bool openNode(const QString &id);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void buildMenus();
    void buildTree();
    void navigate(const QString &id, bool record = true);
    void showActions(const QStringList &actions);
    void updateHistoryActions();
    void restoreState();
    ManagementPage *page(const QString &id);

    QSplitter *m_splitter = nullptr;
    QTreeWidget *m_tree = nullptr;
    QStackedWidget *m_pages = nullptr;
    QWidget *m_actions = nullptr;
    QVBoxLayout *m_actionsLayout = nullptr;
    QAction *m_back = nullptr;
    QAction *m_forward = nullptr;
    QAction *m_toggleTree = nullptr;
    NavigationHistory m_history;
    QHash<QString,QTreeWidgetItem*> m_items;
    QHash<QString,ManagementPage*> m_pageMap;
    ManagementPage *m_currentPage = nullptr;
};

