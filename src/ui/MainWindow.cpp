#include "MainWindow.h"

#include "ManagementPage.h"
#include "Pages.h"
#include "model/NavigationNodes.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QToolBar>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    setWindowTitle("Computer Management"); setWindowIcon(QIcon::fromTheme("computer")); resize(1050,680);
    buildMenus();
    m_splitter=new QSplitter(this);m_tree=new QTreeWidget(m_splitter);m_pages=new QStackedWidget(m_splitter);m_actions=new QWidget(m_splitter);m_actionsLayout=new QVBoxLayout(m_actions);
    m_tree->setHeaderLabel("Console Tree");m_tree->setMinimumWidth(210);m_actions->setMinimumWidth(185);m_actions->setMaximumWidth(260);
    m_splitter->addWidget(m_tree);m_splitter->addWidget(m_pages);m_splitter->addWidget(m_actions);m_splitter->setStretchFactor(1,1);setCentralWidget(m_splitter);
    connect(m_toggleTree,&QAction::toggled,m_tree,&QWidget::setVisible);
    buildTree();
    connect(m_tree,&QTreeWidget::itemActivated,this,[this](QTreeWidgetItem *i){const QString id=i->data(0,Qt::UserRole).toString();if(NavigationNodes::isValid(id))navigate(id);});
    connect(m_tree,&QTreeWidget::itemClicked,this,[this](QTreeWidgetItem *i){const QString id=i->data(0,Qt::UserRole).toString();if(NavigationNodes::isValid(id))navigate(id);});
    restoreState();
    QSettings settings("Aero7","ComputerManagement");
    const QString last = settings.value("lastNode", "overview").toString();
    navigate(NavigationNodes::isValid(last) ? last : QString("overview"));
}

MainWindow::~MainWindow() = default;

void MainWindow::buildMenus()
{
    auto *file=menuBar()->addMenu("&File");file->addAction("E&xit",qApp,&QApplication::quit);
    auto *action=menuBar()->addMenu("&Action");auto *refresh=action->addAction(QIcon::fromTheme("view-refresh"),"&Refresh");connect(refresh,&QAction::triggered,this,[this]{if(m_currentPage)m_currentPage->refresh();});
    auto *view=menuBar()->addMenu("&View");m_toggleTree=view->addAction("Show Console Tree");m_toggleTree->setCheckable(true);m_toggleTree->setChecked(true);
    auto *help=menuBar()->addMenu("&Help");help->addAction("Online documentation",[] {QDesktopServices::openUrl(QUrl("https://github.com/memegeko/aero7-computer-management#readme"));});help->addAction("About Computer Management",this,[this]{QMessageBox::about(this,"About Computer Management","Aero7 Computer Management 0.1\n\nOriginal MIT-licensed software from the Aero7 Open Project.");});
    auto *bar=addToolBar("Management");bar->setMovable(false);bar->setIconSize({16,16});
    m_back=bar->addAction(QIcon::fromTheme("go-previous"),"Back",this,[this]{navigate(m_history.back(),false);});
    m_forward=bar->addAction(QIcon::fromTheme("go-next"),"Forward",this,[this]{navigate(m_history.forward(),false);});
    bar->addAction(QIcon::fromTheme("view-list-tree"),"Show or hide console tree",m_toggleTree,&QAction::toggle);
    bar->addSeparator();
    bar->addAction(QIcon::fromTheme("document-properties"),"Properties",this,[this]{
        if(m_currentPage) QMessageBox::information(this,"Properties",QString("Page ID: %1\nBackend details are documented in BACKENDS.md.").arg(m_currentPage->nodeId()));
    });
    bar->addAction(QIcon::fromTheme("view-refresh"),"Refresh",this,[this]{if(m_currentPage)m_currentPage->refresh();});
    bar->addAction(QIcon::fromTheme("help-contents"),"Help",[]{QDesktopServices::openUrl(QUrl("https://github.com/memegeko/aero7-computer-management#readme"));});
}

void MainWindow::buildTree()
{
    for(const auto &node:NavigationNodes::all()){
        auto *item=new QTreeWidgetItem(QStringList{node.name});item->setIcon(0,QIcon::fromTheme(node.icon));item->setData(0,Qt::UserRole,node.id);m_items.insert(node.id,item);
        if(node.parentId.isEmpty())m_tree->addTopLevelItem(item);else m_items.value(node.parentId)->addChild(item);
    }m_tree->expandAll();
}

ManagementPage *MainWindow::page(const QString &id)
{
    if(m_pageMap.contains(id))return m_pageMap.value(id);auto *p=Pages::create(id,m_pages);m_pageMap.insert(id,p);m_pages->addWidget(p);
    connect(p,&ManagementPage::actionsChanged,this,&MainWindow::showActions);
    connect(p,&ManagementPage::statusMessage,this,[this](const QString &message){statusBar()->showMessage(message);});
    return p;
}

void MainWindow::navigate(const QString &id,bool record)
{
    if(!NavigationNodes::isValid(id))return;if(record)m_history.visit(id);m_currentPage=page(id);m_pages->setCurrentWidget(m_currentPage);m_tree->setCurrentItem(m_items.value(id));showActions(m_currentPage->actions());m_currentPage->refresh();updateHistoryActions();
}

bool MainWindow::openNode(const QString &id){if(!NavigationNodes::isValid(id))return false;navigate(id);return true;}

void MainWindow::showActions(const QStringList &actions)
{
    while(auto *item=m_actionsLayout->takeAt(0)){if(item->widget())item->widget()->deleteLater();delete item;}
    auto *title=new QLabel("Actions");QFont f=title->font();f.setBold(true);title->setFont(f);m_actionsLayout->addWidget(title);
    for(const QString &name:actions){auto *button=new QPushButton(name);button->setFlat(true);button->setStyleSheet("text-align:left;color:#0645ad;padding:4px;");connect(button,&QPushButton::clicked,this,[this,name]{if(m_currentPage)m_currentPage->triggerAction(name);});m_actionsLayout->addWidget(button);}
    m_actionsLayout->addStretch();
}

void MainWindow::updateHistoryActions(){m_back->setEnabled(m_history.canGoBack());m_forward->setEnabled(m_history.canGoForward());}

void MainWindow::restoreState(){
    QSettings s("Aero7","ComputerManagement");restoreGeometry(s.value("geometry").toByteArray());m_splitter->restoreState(s.value("splitter").toByteArray());
    const QStringList expanded=s.value("expandedNodes").toStringList();
    if(!expanded.isEmpty())for(auto it=m_items.cbegin();it!=m_items.cend();++it)it.value()->setExpanded(expanded.contains(it.key()));
}
void MainWindow::closeEvent(QCloseEvent *event){
    QSettings s("Aero7","ComputerManagement");s.setValue("geometry",saveGeometry());s.setValue("splitter",m_splitter->saveState());s.setValue("lastNode",m_history.current());
    QStringList expanded;for(auto it=m_items.cbegin();it!=m_items.cend();++it)if(it.value()->isExpanded())expanded<<it.key();s.setValue("expandedNodes",expanded);QMainWindow::closeEvent(event);
}
