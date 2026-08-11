#include "Pages.h"

#include "backends/AccountsBackend.h"
#include "backends/JournalBackend.h"
#include "backends/PerformanceBackend.h"
#include "backends/SambaBackend.h"
#include "backends/SystemdBackend.h"
#include "backends/UDisksBackend.h"
#include "model/NavigationNodes.h"
#include "util/Format.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace {

QLabel *heading(const QString &text)
{
    auto *label = new QLabel(text);
    QFont font = label->font(); font.setPointSize(font.pointSize() + 3); font.setBold(true);
    label->setFont(font);
    label->setStyleSheet("color:#174d8f; padding:4px 0 8px 0;");
    return label;
}

QTableWidget *table(const QStringList &headers)
{
    auto *widget = new QTableWidget;
    widget->setColumnCount(headers.size());
    widget->setHorizontalHeaderLabels(headers);
    widget->setSelectionBehavior(QAbstractItemView::SelectRows);
    widget->setSelectionMode(QAbstractItemView::SingleSelection);
    widget->setEditTriggers(QAbstractItemView::NoEditTriggers);
    widget->horizontalHeader()->setStretchLastSection(true);
    widget->verticalHeader()->hide();
    return widget;
}

void setCell(QTableWidget *t, int row, int column, const QString &text)
{
    t->setItem(row, column, new QTableWidgetItem(text));
}

class OverviewPage final : public ManagementPage {
public:
    explicit OverviewPage(QWidget *parent) : ManagementPage("overview", parent)
    {
        auto *layout = new QVBoxLayout(this);
        layout->addWidget(heading("Computer Management (Local)"));
        auto *intro = new QLabel("Use the console tree to view system tools, storage, devices, and services. "
                                 "Aero7 uses the native Linux service for each page and labels unavailable features honestly.");
        intro->setWordWrap(true); layout->addWidget(intro);
        auto *control = new QPushButton(QIcon::fromTheme("preferences-system"), "Open Control Panel");
        connect(control, &QPushButton::clicked, this, [this] {
            const QString app = QStandardPaths::findExecutable("controlpanel");
            if (app.isEmpty()) QMessageBox::information(this, "Control Panel", "Aero7 Control Panel is not installed.");
            else QProcess::startDetached(app, {});
        });
        layout->addWidget(control, 0, Qt::AlignLeft); layout->addStretch();
    }
};

class TimersPage final : public ManagementPage {
public:
    explicit TimersPage(QWidget *parent) : ManagementPage("task-scheduler", parent), m_table(table({"Task Name", "Next Run", "Last Run", "Activates"}))
    {
        auto *layout = new QVBoxLayout(this); layout->addWidget(heading("Task Scheduler"));
        auto *note = new QLabel("Scheduled tasks represented by systemd timer units. Creation and editing are not available in this testing release.");
        note->setWordWrap(true); layout->addWidget(note); layout->addWidget(m_table); refresh();
    }
    void refresh() override {
        QString error; const auto items = SystemdBackend::timers(&error); m_table->setRowCount(items.size());
        for (int r=0; r<items.size(); ++r) { setCell(m_table,r,0,items[r].unit); setCell(m_table,r,1,items[r].next); setCell(m_table,r,2,items[r].last); setCell(m_table,r,3,items[r].activates); }
        emit statusMessage(error.isEmpty() ? QString("%1 scheduled task(s)").arg(items.size()) : error);
    }
private: QTableWidget *m_table;
};

class EventsPage final : public ManagementPage {
public:
    explicit EventsPage(QWidget *parent) : ManagementPage("event-viewer", parent), m_table(table({"Date and Time", "Level", "Source", "Message"}))
    {
        auto *layout = new QVBoxLayout(this); layout->addWidget(heading("Event Viewer (Local)"));
        auto *note = new QLabel("Recent entries from the system journal. Visibility depends on your account's journal permissions.");
        note->setWordWrap(true); layout->addWidget(note); layout->addWidget(m_table); refresh();
    }
    void refresh() override {
        QString error; const auto items = JournalBackend::recent(&error); m_table->setRowCount(items.size());
        for (int r=0; r<items.size(); ++r) { setCell(m_table,r,0,items[r].time); setCell(m_table,r,1,items[r].priority); setCell(m_table,r,2,items[r].unit); setCell(m_table,r,3,items[r].message); }
        m_table->resizeColumnToContents(0); emit statusMessage(error.isEmpty() ? QString("%1 event(s)").arg(items.size()) : error);
    }
private: QTableWidget *m_table;
};

class SharesPage final : public ManagementPage {
public:
    explicit SharesPage(QWidget *parent) : ManagementPage("shared-folders", parent), m_table(table({"Share", "Process", "Client", "Connected"})), m_state(new QLabel)
    {
        auto *layout = new QVBoxLayout(this); layout->addWidget(heading("Shared Folders")); layout->addWidget(m_state); layout->addWidget(m_table); refresh();
    }
    void refresh() override {
        QString error; const auto items = SambaBackend::shares(&error); m_table->setRowCount(items.size());
        for (int r=0; r<items.size(); ++r) { setCell(m_table,r,0,items[r].service); setCell(m_table,r,1,items[r].pid); setCell(m_table,r,2,items[r].machine); setCell(m_table,r,3,items[r].connectedAt); }
        m_state->setText(error.isEmpty() ? "Active Samba share sessions" : error); m_state->setWordWrap(true); emit statusMessage(m_state->text());
    }
private: QTableWidget *m_table; QLabel *m_state;
};

class AccountsPage final : public ManagementPage {
public:
    AccountsPage(QString node, bool groups, QWidget *parent) : ManagementPage(std::move(node), parent), m_groups(groups), m_table(table(groups ? QStringList{"Name","Group ID","Members"} : QStringList{"Name","Full Name","User ID","Home Folder","Login Shell"}))
    {
        auto *layout = new QVBoxLayout(this); layout->addWidget(heading(groups ? "Groups" : "Users"));
        auto *note = new QLabel("Local account information from the system account database. This view is read-only."); note->setWordWrap(true);
        layout->addWidget(note); layout->addWidget(m_table); refresh();
    }
    void refresh() override {
        if (m_groups) { const auto items=AccountsBackend::groups(); m_table->setRowCount(items.size()); for(int r=0;r<items.size();++r){setCell(m_table,r,0,items[r].name);setCell(m_table,r,1,QString::number(items[r].gid));setCell(m_table,r,2,items[r].members.join(", "));} emit statusMessage(QString("%1 group(s)").arg(items.size())); }
        else { const auto items=AccountsBackend::users(); m_table->setRowCount(items.size()); for(int r=0;r<items.size();++r){setCell(m_table,r,0,items[r].name);setCell(m_table,r,1,items[r].fullName);setCell(m_table,r,2,QString::number(items[r].uid));setCell(m_table,r,3,items[r].home);setCell(m_table,r,4,items[r].shell);} emit statusMessage(QString("%1 user account(s)").arg(items.size())); }
    }
private: bool m_groups; QTableWidget *m_table;
};

class LocalAccountsPage final : public ManagementPage {
public:
    explicit LocalAccountsPage(QWidget *parent) : ManagementPage("local-users-groups", parent)
    {
        auto *layout=new QVBoxLayout(this); layout->addWidget(heading("Local Users and Groups"));
        auto *text=new QLabel("Select Users or Groups in the console tree to inspect the local account database. Account changes remain in Aero7 Control Panel so authorization is handled consistently."); text->setWordWrap(true); layout->addWidget(text); layout->addStretch();
    }
};

class PerformancePage final : public ManagementPage {
public:
    explicit PerformancePage(QWidget *parent) : ManagementPage("performance", parent), m_values(new QLabel)
    {
        auto *layout=new QVBoxLayout(this); layout->addWidget(heading("Performance Monitor")); m_values->setTextInteractionFlags(Qt::TextSelectableByMouse); layout->addWidget(m_values); layout->addStretch();
        auto *timer=new QTimer(this); connect(timer,&QTimer::timeout,this,&PerformancePage::refresh); timer->start(2000); refresh();
    }
    void refresh() override { QString error; const auto s=PerformanceBackend::snapshot(&error); const quint64 used=s.memoryTotal-s.memoryAvailable;
        m_values->setText(QString("<table cellspacing='8'><tr><td><b>Load average</b></td><td>%1, %2, %3</td></tr><tr><td><b>Memory in use</b></td><td>%4 of %5</td></tr><tr><td><b>System uptime</b></td><td>%6</td></tr></table>")
            .arg(s.load1,0,'f',2).arg(s.load5,0,'f',2).arg(s.load15,0,'f',2).arg(Format::bytes(used),Format::bytes(s.memoryTotal),Format::uptime(s.uptimeSeconds)));
        emit statusMessage(error.isEmpty()?"Live Linux performance data":error); }
private: QLabel *m_values;
};

class DeviceManagerPage final : public ManagementPage {
public:
    explicit DeviceManagerPage(QWidget *parent) : ManagementPage("device-manager", parent), m_state(new QLabel)
    { auto *layout=new QVBoxLayout(this); layout->addWidget(heading("Device Manager")); layout->addWidget(m_state); auto *open=new QPushButton(QIcon::fromTheme("preferences-system-devices"),"Open Device Manager"); connect(open,&QPushButton::clicked,this,[this]{triggerAction("Open Device Manager");}); layout->addWidget(open,0,Qt::AlignLeft);layout->addStretch(); refresh(); }
    QStringList actions() const override { return {"Open Device Manager","Refresh"}; }
    void refresh() override { const QString exe=QStandardPaths::findExecutable("devmgmt"); m_state->setText(exe.isEmpty()?"The external Device Manager (devmgmt) is not installed.":"Device Manager is installed and ready."); announceActions(); }
    void triggerAction(const QString &a) override { if(a=="Open Device Manager"){const QString exe=QStandardPaths::findExecutable("devmgmt");if(exe.isEmpty())QMessageBox::information(this,"Device Manager","devmgmt is not installed.");else QProcess::startDetached(exe,{});}else ManagementPage::triggerAction(a); }
private: QLabel *m_state;
};

class DisksPage final : public ManagementPage {
public:
    explicit DisksPage(QWidget *parent) : ManagementPage("disk-management", parent), m_table(table({"Device","Volume Label","File System","Capacity","Mount Point"}))
    { auto *layout=new QVBoxLayout(this);layout->addWidget(heading("Disk Management"));auto *note=new QLabel("Read-only disk inventory from UDisks2. Mount and unmount are the only operations enabled in this testing release.");note->setWordWrap(true);layout->addWidget(note);layout->addWidget(m_table);connect(m_table,&QTableWidget::itemSelectionChanged,this,&DisksPage::announceActions);refresh(); }
    QStringList actions() const override { QStringList a{"Refresh"}; const int r=m_table->currentRow(); if(r>=0 && !m_table->item(r,0)->data(Qt::UserRole).toString().isEmpty()) a << (m_table->item(r,4)->text().isEmpty()?"Mount":"Unmount"); return a; }
    void refresh() override { QString error; m_devices=UDisksBackend::devices(&error);m_table->setRowCount(m_devices.size());for(int r=0;r<m_devices.size();++r){setCell(m_table,r,0,m_devices[r].device);m_table->item(r,0)->setData(Qt::UserRole,m_devices[r].mountable?m_devices[r].objectPath:QString());setCell(m_table,r,1,m_devices[r].label);setCell(m_table,r,2,m_devices[r].fileSystem);setCell(m_table,r,3,Format::bytes(m_devices[r].size));setCell(m_table,r,4,m_devices[r].mountPoint);}emit statusMessage(error.isEmpty()?QString("%1 block device(s)").arg(m_devices.size()):error);announceActions(); }
    void triggerAction(const QString &a) override { if(a=="Refresh"){refresh();return;}const int r=m_table->currentRow();if(r<0)return;QString error;const QString p=m_devices[r].objectPath;const bool ok=a=="Mount"?UDisksBackend::mount(p,&error):UDisksBackend::unmount(p,&error);if(!ok)QMessageBox::warning(this,a,error);refresh(); }
private: QTableWidget *m_table; QList<BlockDevice> m_devices;
};

class ServicesPage final : public ManagementPage {
public:
    explicit ServicesPage(QWidget *parent) : ManagementPage("services", parent),m_table(table({"Name","Description","Status","Substate"}))
    {auto *layout=new QVBoxLayout(this);layout->addWidget(heading("Services"));auto *note=new QLabel("System services represented by systemd service units. Start and stop controls are withheld until their polkit authorization path is reviewed.");note->setWordWrap(true);layout->addWidget(note);layout->addWidget(m_table);refresh();}
    void refresh() override {QString error;const auto items=SystemdBackend::services(&error);m_table->setRowCount(items.size());for(int r=0;r<items.size();++r){setCell(m_table,r,0,items[r].unit);setCell(m_table,r,1,items[r].description);setCell(m_table,r,2,items[r].active);setCell(m_table,r,3,items[r].sub);}emit statusMessage(error.isEmpty()?QString("%1 service(s)").arg(items.size()):error);}
private:QTableWidget *m_table;
};

} // namespace

namespace Pages {
ManagementPage *create(const QString &id, QWidget *parent)
{
    if(id=="overview")return new OverviewPage(parent);
    if(id=="task-scheduler")return new TimersPage(parent);
    if(id=="event-viewer")return new EventsPage(parent);
    if(id=="shared-folders")return new SharesPage(parent);
    if(id=="local-users-groups")return new LocalAccountsPage(parent);
    if(id=="users")return new AccountsPage(id,false,parent);
    if(id=="groups")return new AccountsPage(id,true,parent);
    if(id=="performance")return new PerformancePage(parent);
    if(id=="device-manager")return new DeviceManagerPage(parent);
    if(id=="disk-management")return new DisksPage(parent);
    if(id=="services")return new ServicesPage(parent);
    return new OverviewPage(parent);
}
}

