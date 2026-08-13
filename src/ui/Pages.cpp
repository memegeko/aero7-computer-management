#include "Pages.h"

#include "DiskMapWidget.h"
#include "DiskDialogs.h"
#include "PerformanceGraph.h"
#include "backends/AccountsBackend.h"
#include "backends/JournalBackend.h"
#include "backends/PerformanceBackend.h"
#include "backends/SambaBackend.h"
#include "backends/SystemInfoBackend.h"
#include "backends/SystemdBackend.h"
#include "backends/UDisksBackend.h"
#include "model/NavigationNodes.h"
#include "util/Format.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFrame>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSet>
#include <QSizePolicy>
#include <QSplitter>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtConcurrent>

#include <unistd.h>

namespace {
QLabel *heading(const QString &text)
{
    auto *label = new QLabel(text);
    QFont font = label->font();
    font.setPointSize(font.pointSize() + 3);
    font.setBold(true);
    label->setFont(font);
    label->setStyleSheet("color:#174d8f; padding:4px 0 8px 0;");
    return label;
}

QLabel *description(const QString &text)
{
    auto *label = new QLabel(text);
    label->setWordWrap(true);
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
    widget->setAlternatingRowColors(true);
    widget->setSortingEnabled(true);
    widget->horizontalHeader()->setStretchLastSection(true);
    widget->verticalHeader()->hide();
    return widget;
}

void setCell(QTableWidget *widget, int row, int column, const QString &text,
             const QVariant &data = {})
{
    auto *item = new QTableWidgetItem(text);
    if (data.isValid()) item->setData(Qt::UserRole, data);
    widget->setItem(row, column, item);
}

int selectedSourceRow(const QTableWidget *widget)
{
    const int visualRow = widget->currentRow();
    if (visualRow < 0)
        return -1;
    const QTableWidgetItem *identity = widget->item(visualRow, 0);
    if (!identity)
        return -1;
    bool ok = false;
    const int sourceRow = identity->data(Qt::UserRole).toInt(&ok);
    return ok ? sourceRow : visualRow;
}

QString yesNo(bool value) { return value ? "Yes" : "No"; }

void showProperties(QWidget *parent, const QString &title,
                    const QList<QPair<QString, QString>> &properties,
                    const QString &details = {})
{
    QDialog dialog(parent);
    dialog.setWindowTitle(title);
    dialog.resize(600, details.isEmpty() ? 360 : 520);
    auto *layout = new QVBoxLayout(&dialog);
    auto *form = new QFormLayout;
    for (const auto &[label, value] : properties) {
        auto *field = new QLabel(value.isEmpty() ? "—" : value);
        field->setTextInteractionFlags(Qt::TextSelectableByMouse);
        field->setWordWrap(true);
        form->addRow(label + ':', field);
    }
    layout->addLayout(form);
    if (!details.isEmpty()) {
        auto *text = new QPlainTextEdit(details);
        text->setReadOnly(true);
        layout->addWidget(text);
    }
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    dialog.exec();
}

bool confirm(QWidget *parent, const QString &title, const QString &text)
{
    return QMessageBox::warning(parent, title, text, QMessageBox::Yes | QMessageBox::No,
                                QMessageBox::No) == QMessageBox::Yes;
}

QString executable(const QStringList &names)
{
    for (const QString &name : names) {
        const QString path = QStandardPaths::findExecutable(name);
        if (!path.isEmpty()) return path;
    }
    return {};
}

class OverviewPage final : public ManagementPage {
public:
    explicit OverviewPage(QWidget *parent) : ManagementPage("overview", parent)
    {
        auto *layout = new QVBoxLayout(this);
        layout->addWidget(heading("Computer Management (Local)"));
        layout->addWidget(description("A summary of this Aero7 computer. Every value comes from the running Linux system."));
        auto *box = new QGroupBox("System summary");
        m_form = new QFormLayout(box);
        const QStringList labels = {"Computer name", "Aero7 version", "Arch Linux base", "Kernel",
                                    "Architecture", "Uptime", "CPU", "Installed memory", "System disk",
                                    "Current user", "Desktop / session", "Network status"};
        for (const QString &label : labels) {
            auto *value = new QLabel;
            value->setTextInteractionFlags(Qt::TextSelectableByMouse);
            value->setWordWrap(true);
            m_values << value;
            m_form->addRow(label + ':', value);
        }
        layout->addWidget(box);
        auto *links = new QHBoxLayout;
        for (const auto &[name, id] : QList<QPair<QString, QString>>{{"System Tools", "system-tools"},
                 {"Storage", "storage"}, {"Services and Applications", "services-applications"}}) {
            auto *button = new QPushButton(name);
            connect(button, &QPushButton::clicked, this, [this, id]{ navigateTo(id); });
            links->addWidget(button);
        }
        auto *control = new QPushButton(QIcon::fromTheme("preferences-system"), "Open Control Panel");
        connect(control, &QPushButton::clicked, this, [this] {
            const QString app = executable({"control", "controlpanel"});
            if (app.isEmpty()) QMessageBox::information(this, "Control Panel", "Aero7 Control Panel is not installed.");
            else QProcess::startDetached(app, {});
        });
        links->addWidget(control);
        links->addStretch();
        layout->addLayout(links);
        layout->addStretch();
        refresh();
    }
    QStringList actions() const override { return {"Refresh", "Open Control Panel", "Properties"}; }
    void refresh() override
    {
        QString error;
        const SystemSummary summary = SystemInfoBackend::summary(&error);
        const QStringList values = {summary.computerName, summary.aero7Version, summary.archVersion,
            summary.kernelVersion, summary.architecture, summary.uptime, summary.cpu, summary.memory,
            summary.systemDisk, summary.currentUser, summary.session, summary.network};
        for (int i = 0; i < m_values.size(); ++i) m_values[i]->setText(values.value(i));
        emit statusMessage(error.isEmpty() ? "System summary refreshed" : error);
    }
    void triggerAction(const QString &action) override
    {
        if (action == "Open Control Panel") {
            const QString app = executable({"control", "controlpanel"});
            if (app.isEmpty()) QMessageBox::information(this, "Control Panel", "Aero7 Control Panel is not installed.");
            else QProcess::startDetached(app, {});
        } else if (action == "Properties") {
            QString error; const auto s = SystemInfoBackend::summary(&error);
            showProperties(this, "Computer Management (Local) Properties", {{"Computer name", s.computerName},
                {"Aero7 version", s.aero7Version}, {"Kernel", s.kernelVersion}, {"Architecture", s.architecture},
                {"Current user", s.currentUser}, {"Session", s.session}});
        } else ManagementPage::triggerAction(action);
    }
private:
    QFormLayout *m_form = nullptr;
    QList<QLabel *> m_values;
};

class ContainerPage final : public ManagementPage {
public:
    ContainerPage(QString id, const QString &title, const QString &text,
                  const QList<QPair<QString, QString>> &links, QWidget *parent)
        : ManagementPage(std::move(id), parent)
    {
        auto *layout = new QVBoxLayout(this);
        layout->addWidget(heading(title));
        layout->addWidget(description(text));
        for (const auto &[name, target] : links) {
            auto *button = new QPushButton(QIcon::fromTheme(NavigationNodes::find(target)->icon), name);
            button->setMinimumHeight(36);
            connect(button, &QPushButton::clicked, this, [this, target]{ navigateTo(target); });
            layout->addWidget(button);
        }
        layout->addStretch();
    }
    QStringList actions() const override { return {"Properties"}; }
    void triggerAction(const QString &action) override
    {
        if (action == "Properties")
            showProperties(this, "Management container", {{"Page", nodeId()}, {"Backend", "Navigation only"}});
    }
};

class TimersPage final : public ManagementPage {
public:
    explicit TimersPage(QWidget *parent) : ManagementPage("task-scheduler", parent),
        m_table(table({"Name", "Status", "Next Run Time", "Last Run Time", "Trigger / Service", "Enabled", "Scope"}))
    {
        auto *layout = new QVBoxLayout(this);
        layout->addWidget(heading("Task Scheduler"));
        layout->addWidget(description("Scheduled system and user tasks backed by real systemd .timer and .service units."));
        layout->addWidget(m_table);
        connect(m_table, &QTableWidget::itemSelectionChanged, this, &TimersPage::announceActions);
        connect(m_table, &QTableWidget::itemDoubleClicked, this, [this]{ properties(); });
        auto *timer = new QTimer(this);
        connect(timer, &QTimer::timeout, this, &TimersPage::refresh);
        timer->start(30000);
        refresh();
    }
    QStringList actions() const override
    {
        QStringList result{"Refresh", "Create Basic Task"};
        const int row = selectedSourceRow(m_table);
        if (row >= 0 && row < m_items.size()) {
            result << "Properties";
            result << (m_items[row].enabled == "enabled" ? "Disable Timer" : "Enable Timer");
            if (!m_items[row].activates.isEmpty()) result << "Run Associated Task";
            if (m_items[row].scope == UnitScope::User) result << "Delete Task";
        }
        return result;
    }
    void refresh() override
    {
        QString error; m_items = SystemdBackend::timers(&error);
        m_table->setSortingEnabled(false); m_table->setRowCount(m_items.size());
        for (int row = 0; row < m_items.size(); ++row) {
            const auto &item = m_items[row];
            setCell(m_table, row, 0, item.unit, row);
            setCell(m_table, row, 1, item.status);
            setCell(m_table, row, 2, item.next);
            setCell(m_table, row, 3, item.last);
            setCell(m_table, row, 4, item.activates);
            setCell(m_table, row, 5, item.enabled);
            setCell(m_table, row, 6, SystemdBackend::scopeName(item.scope));
        }
        m_table->setSortingEnabled(true); announceActions();
        emit statusMessage(error.isEmpty() ? QString("%1 scheduled task(s)").arg(m_items.size()) : error);
    }
    void triggerAction(const QString &action) override
    {
        if (action == "Refresh") { refresh(); return; }
        if (action == "Create Basic Task") { createTask(); return; }
        const int row = selectedSourceRow(m_table); if (row < 0 || row >= m_items.size()) return;
        const TimerInfo item = m_items[row];
        if (action == "Properties") { properties(); return; }
        QString error; bool ok = false;
        if (action == "Enable Timer") {
            ok = SystemdBackend::setUnitEnabled(item.unit, item.scope, true, &error);
            if (ok) ok = SystemdBackend::startUnit(item.unit, item.scope, &error);
        } else if (action == "Disable Timer") {
            SystemdBackend::stopUnit(item.unit, item.scope, nullptr);
            ok = SystemdBackend::setUnitEnabled(item.unit, item.scope, false, &error);
        }
        else if (action == "Run Associated Task") ok = SystemdBackend::startUnit(item.activates, item.scope, &error);
        else if (action == "Delete Task" && confirm(this, "Delete Task", "Delete this user timer and its associated service?"))
            ok = SystemdBackend::deleteUserTimer(item.unit, &error);
        if (!ok && !error.isEmpty()) QMessageBox::warning(this, action, error);
        refresh();
    }
private:
    void properties()
    {
        const int row = selectedSourceRow(m_table); if (row < 0 || row >= m_items.size()) return;
        const auto item = m_items[row]; QString error;
        const UnitDetails details = SystemdBackend::details(item.unit, item.scope, &error);
        showProperties(this, item.unit + " Properties", {{"Timer unit", item.unit}, {"Service unit", item.activates},
            {"Description", item.description}, {"Status", item.status}, {"Last triggered", item.last},
            {"Next trigger", item.next}, {"Timer expression", details.timerExpression},
            {"Persistent", yesNo(item.persistent)}, {"Enabled", item.enabled},
            {"Scope", SystemdBackend::scopeName(item.scope)}, {"Unit file", details.fragmentPath}});
    }
    void createTask()
    {
        QDialog dialog(this); dialog.setWindowTitle("Create Basic Task");
        auto *layout = new QFormLayout(&dialog);
        QLineEdit name, taskDescription, command;
        QComboBox schedule; schedule.addItems({"Daily", "Weekly", "At login/startup", "Custom calendar expression"});
        QLineEdit custom("daily"); custom.setEnabled(false);
        connect(&schedule, &QComboBox::currentIndexChanged, &dialog, [&]{ custom.setEnabled(schedule.currentIndex() == 3); });
        layout->addRow("Name:", &name); layout->addRow("Description:", &taskDescription);
        layout->addRow("Trigger:", &schedule); layout->addRow("Calendar expression:", &custom);
        layout->addRow("Program and arguments:", &command);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        layout->addRow(buttons); connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (dialog.exec() != QDialog::Accepted) return;
        QString expression;
        if (schedule.currentIndex() == 0) expression = "OnCalendar=daily";
        else if (schedule.currentIndex() == 1) expression = "OnCalendar=weekly";
        else if (schedule.currentIndex() == 2) expression = "OnBootSec=1min";
        else expression = "OnCalendar=" + custom.text().trimmed();
        QString error;
        if (!SystemdBackend::createUserTimer(name.text(), taskDescription.text(), expression, command.text(), &error))
            QMessageBox::warning(this, "Create Basic Task", error);
        refresh();
    }
    QTableWidget *m_table;
    QList<TimerInfo> m_items;
};

class EventsPage final : public ManagementPage {
public:
    explicit EventsPage(QWidget *parent) : ManagementPage("event-viewer", parent),
        m_table(table({"Level", "Date and Time", "Source", "Message"}))
    {
        auto *layout = new QVBoxLayout(this); layout->addWidget(heading("Event Viewer (Local)"));
        auto *filters = new QHBoxLayout;
        m_category = new QComboBox; m_category->addItems({"All Events", "Aero7 Logs", "System", "Applications", "Boot", "Kernel", "Authentication / Security", "Hardware"});
        QSettings settings("Aero7", "ComputerManagement");
        settings.beginGroup("customEventViews");
        for (const QString &name : settings.childKeys()) {
            const QStringList definition = settings.value(name).toStringList();
            if (definition.size() != 2) continue;
            m_customViews.insert(name, {definition[0], definition[1]});
            m_category->addItem("Custom View: " + name);
        }
        settings.endGroup();
        m_search = new QLineEdit; m_search->setPlaceholderText("Find in current log...");
        filters->addWidget(new QLabel("Log:")); filters->addWidget(m_category); filters->addWidget(m_search, 1);
        layout->addLayout(filters); layout->addWidget(m_table);
        connect(m_category, &QComboBox::currentTextChanged, this, &EventsPage::refresh);
        connect(m_search, &QLineEdit::returnPressed, this, &EventsPage::refresh);
        connect(m_table, &QTableWidget::itemSelectionChanged, this, &EventsPage::announceActions);
        connect(m_table, &QTableWidget::itemDoubleClicked, this, [this]{ properties(); });
        refresh();
    }
    QStringList actions() const override
    {
        QStringList result{"Open Saved Log", "Create Custom View", "Filter Current Log", "Find", "Refresh"};
        if (m_table->currentRow() >= 0) result << "Properties";
        return result;
    }
    void refresh() override
    {
        QString category = m_category->currentText();
        QString savedSearch;
        if (category.startsWith("Custom View: ")) {
            const auto definition = m_customViews.value(category.mid(13));
            category = definition.first;
            savedSearch = definition.second;
        }
        QString error; m_entries = JournalBackend::query(category, savedSearch.isEmpty() ? m_search->text() : savedSearch, &error);
        if (!savedSearch.isEmpty() && !m_search->text().isEmpty()) {
            const QString liveSearch = m_search->text();
            m_entries.removeIf([&](const JournalEntry &entry) {
                return !entry.time.contains(liveSearch, Qt::CaseInsensitive)
                    && !entry.source.contains(liveSearch, Qt::CaseInsensitive)
                    && !entry.message.contains(liveSearch, Qt::CaseInsensitive);
            });
        }
        populate(); emit statusMessage(error.isEmpty() ? QString("%1 event(s)").arg(m_entries.size()) : error);
    }
    void triggerAction(const QString &action) override
    {
        if (action == "Refresh") refresh();
        else if (action == "Properties") properties();
        else if (action == "Find") { m_search->setFocus(); m_search->selectAll(); }
        else if (action == "Filter Current Log") m_category->showPopup();
        else if (action == "Create Custom View") createCustomView();
        else if (action == "Open Saved Log") {
            const QString path = QFileDialog::getOpenFileName(this, "Open Saved Journal", {}, "Journal files (*.journal *.journal~);;All files (*)");
            if (path.isEmpty()) return;
            QString error; m_entries = JournalBackend::saved(path, &error); populate();
            if (!error.isEmpty()) QMessageBox::warning(this, "Open Saved Log", error);
        }
    }
private:
    void createCustomView()
    {
        QDialog dialog(this);
        dialog.setWindowTitle("Create Custom View");
        auto *layout = new QFormLayout(&dialog);
        QLineEdit name;
        QComboBox category;
        category.addItems({"All Events", "Aero7 Logs", "System", "Applications", "Boot", "Kernel", "Authentication / Security", "Hardware"});
        QLineEdit match;
        match.setPlaceholderText("Optional text to match");
        layout->addRow("Name:", &name);
        layout->addRow("Log:", &category);
        layout->addRow("Contains:", &match);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        layout->addRow(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (dialog.exec() != QDialog::Accepted) return;
        const QString viewName = name.text().trimmed();
        if (viewName.isEmpty() || viewName.contains('/') || viewName.contains('\\')) {
            QMessageBox::warning(this, "Create Custom View", "Enter a name without slash characters.");
            return;
        }
        m_customViews.insert(viewName, {category.currentText(), match.text()});
        QSettings settings("Aero7", "ComputerManagement");
        settings.setValue("customEventViews/" + viewName, QStringList{category.currentText(), match.text()});
        const QString label = "Custom View: " + viewName;
        if (m_category->findText(label) < 0) m_category->addItem(label);
        m_category->setCurrentText(label);
    }

    void populate()
    {
        m_table->setSortingEnabled(false); m_table->setRowCount(m_entries.size());
        for (int row = 0; row < m_entries.size(); ++row) {
            setCell(m_table, row, 0, m_entries[row].priority, row);
            setCell(m_table, row, 1, m_entries[row].time);
            setCell(m_table, row, 2, m_entries[row].source);
            setCell(m_table, row, 3, m_entries[row].message);
        }
        m_table->resizeColumnToContents(0); m_table->resizeColumnToContents(1);
        m_table->setSortingEnabled(true); announceActions();
    }
    void properties()
    {
        const int row = selectedSourceRow(m_table); if (row < 0 || row >= m_entries.size()) return;
        const JournalEntry entry = m_entries[row];
        QStringList fields; for (auto it = entry.fields.constBegin(); it != entry.fields.constEnd(); ++it) fields << it.key() + '=' + it.value();
        showProperties(this, "Event Properties", {{"Level", entry.priority}, {"Date and Time", entry.time},
            {"Source", entry.source}, {"PID", entry.pid}, {"UID", entry.uid}, {"Unit", entry.unit},
            {"Executable", entry.executable}, {"Boot ID", entry.bootId}, {"Message", entry.message}}, fields.join('\n'));
    }
    QComboBox *m_category;
    QLineEdit *m_search;
    QTableWidget *m_table;
    QList<JournalEntry> m_entries;
    QMap<QString, QPair<QString, QString>> m_customViews;
};

class SharesPage final : public ManagementPage {
public:
    explicit SharesPage(QWidget *parent) : ManagementPage("shared-folders", parent),
        m_shares(table({"Share Name", "Folder Path", "Type", "Comment"})),
        m_sessions(table({"User", "Computer", "Client IP", "Protocol", "Connected Since"})),
        m_files(table({"File", "User", "Client", "Access Mode", "PID"}))
    {
        auto *layout = new QVBoxLayout(this); layout->addWidget(heading("Shared Folders"));
        m_state = new QLabel; m_state->setWordWrap(true); layout->addWidget(m_state);
        m_tabs = new QTabWidget; m_tabs->addTab(m_shares, "Shares"); m_tabs->addTab(m_sessions, "Sessions"); m_tabs->addTab(m_files, "Open Files"); layout->addWidget(m_tabs);
        connect(m_tabs, &QTabWidget::currentChanged, this, &SharesPage::announceActions);
        connect(m_shares, &QTableWidget::itemSelectionChanged, this, &SharesPage::announceActions);
        connect(m_shares, &QTableWidget::itemDoubleClicked, this, [this]{ properties(); });
        auto *timer = new QTimer(this); connect(timer, &QTimer::timeout, this, &SharesPage::refresh); timer->start(15000);
        refresh();
    }
    QStringList actions() const override
    {
        QStringList result{"Refresh"};
        if (m_tabs->currentIndex() == 0) {
            result << "Create Share";
            const int row = selectedSourceRow(m_shares);
            if (row >= 0 && row < m_shareItems.size()) result << "Open" << "Properties";
            if (row >= 0 && row < m_shareItems.size() && m_shareItems[row].userShare) result << "Stop Sharing";
        }
        return result;
    }
    void refresh() override
    {
        QString shareError, sessionError, fileError;
        m_shareItems = SambaBackend::shares(&shareError); m_sessionItems = SambaBackend::sessions(&sessionError); m_fileItems = SambaBackend::openFiles(&fileError);
        m_shares->setSortingEnabled(false); m_shares->setRowCount(m_shareItems.size());
        for (int row = 0; row < m_shareItems.size(); ++row) {
            setCell(m_shares, row, 0, m_shareItems[row].name, row); setCell(m_shares, row, 1, m_shareItems[row].path);
            setCell(m_shares, row, 2, m_shareItems[row].type); setCell(m_shares, row, 3, m_shareItems[row].comment);
        }
        m_shares->setSortingEnabled(true);
        m_sessions->setSortingEnabled(false); m_sessions->setRowCount(m_sessionItems.size());
        for (int row = 0; row < m_sessionItems.size(); ++row) {
            const auto &s = m_sessionItems[row]; setCell(m_sessions,row,0,s.user);setCell(m_sessions,row,1,s.computer);
            setCell(m_sessions,row,2,s.clientIp);setCell(m_sessions,row,3,s.protocol);setCell(m_sessions,row,4,s.connectedSince);
        }
        m_sessions->setSortingEnabled(true);
        m_files->setSortingEnabled(false); m_files->setRowCount(m_fileItems.size());
        for (int row = 0; row < m_fileItems.size(); ++row) {
            const auto &f = m_fileItems[row]; setCell(m_files,row,0,f.file);setCell(m_files,row,1,f.user);
            setCell(m_files,row,2,f.client);setCell(m_files,row,3,f.accessMode);setCell(m_files,row,4,f.pid);
        }
        m_files->setSortingEnabled(true);
        m_state->setText(!shareError.isEmpty() ? shareError : QString("%1 share(s), %2 active session(s), %3 open file(s)")
                         .arg(m_shareItems.size()).arg(m_sessionItems.size()).arg(m_fileItems.size()));
        announceActions(); emit statusMessage(m_state->text());
    }
    void triggerAction(const QString &action) override
    {
        if (action == "Refresh") { refresh(); return; }
        if (action == "Create Share") { createShare(); return; }
        const int row = selectedSourceRow(m_shares); if (row < 0 || row >= m_shareItems.size()) return;
        const SambaShare share = m_shareItems[row];
        if (action == "Open") QDesktopServices::openUrl(QUrl::fromLocalFile(share.path));
        else if (action == "Properties") properties();
        else if (action == "Stop Sharing" && confirm(this, "Stop Sharing", QString("Stop sharing '%1'? The folder and its files will not be deleted.").arg(share.name))) {
            QString error; if (!SambaBackend::removeUserShare(share.name, &error)) QMessageBox::warning(this, "Stop Sharing", error); refresh();
        }
    }
private:
    void properties()
    {
        const int row = selectedSourceRow(m_shares); if (row < 0 || row >= m_shareItems.size()) return;
        const auto s = m_shareItems[row]; showProperties(this, s.name + " Properties", {{"Share name",s.name},{"Folder path",s.path},{"Type",s.type},{"Comment",s.comment},{"Source",s.userShare?"Samba user share":"Samba system configuration"}});
    }
    void createShare()
    {
        QDialog dialog(this); dialog.setWindowTitle("Create a Shared Folder"); auto *layout = new QFormLayout(&dialog);
        QLineEdit name, path, comment; auto *browse = new QPushButton("Browse..."); auto *pathRow = new QHBoxLayout; pathRow->addWidget(&path); pathRow->addWidget(browse);
        connect(browse,&QPushButton::clicked,&dialog,[&]{const QString chosen=QFileDialog::getExistingDirectory(&dialog,"Select Folder");if(!chosen.isEmpty())path.setText(chosen);});
        layout->addRow("Share name:",&name);layout->addRow("Folder:",pathRow);layout->addRow("Comment:",&comment);
        auto *buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);layout->addRow(buttons);connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
        if (dialog.exec() != QDialog::Accepted) return;
        QString error;
        if (!SambaBackend::createUserShare(name.text().trimmed(), path.text(), comment.text(), &error))
            QMessageBox::warning(this, "Create Share", error);
        refresh();
    }
    QLabel *m_state; QTabWidget *m_tabs; QTableWidget *m_shares; QTableWidget *m_sessions; QTableWidget *m_files;
    QList<SambaShare> m_shareItems; QList<SambaSession> m_sessionItems; QList<SambaOpenFile> m_fileItems;
};

class LocalAccountsPage final : public ManagementPage {
public:
    explicit LocalAccountsPage(QWidget *parent) : ManagementPage("local-users-groups", parent)
    {
        auto *layout=new QVBoxLayout(this);layout->addWidget(heading("Local Users and Groups"));
        layout->addWidget(description("Manage real Linux accounts and security groups. Changes use standard account tools through a narrowly scoped polkit helper; Computer Management itself never runs as root."));
        for(const auto &[name,id]:QList<QPair<QString,QString>>{{"Users","users"},{"Groups","groups"}}){auto *b=new QPushButton(QIcon::fromTheme(id=="users"?"user-identity":"system-users"),name);connect(b,&QPushButton::clicked,this,[this,id]{navigateTo(id);});layout->addWidget(b);}layout->addStretch();
    }
    QStringList actions() const override { return {"Properties"}; }
    void triggerAction(const QString &action) override { if(action=="Properties")showProperties(this,"Local Users and Groups",{{"Backend","libc account database + authenticated shadow tools"},{"Privilege boundary","polkit helper"}}); }
};

class AccountsPage final : public ManagementPage {
public:
    AccountsPage(QString node, bool groups, QWidget *parent) : ManagementPage(std::move(node), parent), m_groups(groups),
        m_table(table(groups?QStringList{"Name","Description","Group ID","Members"}:QStringList{"Name","Full Name","Description","User ID","Home Folder","Login Shell"}))
    {
        auto *layout=new QVBoxLayout(this);layout->addWidget(heading(groups?"Groups":"Users"));
        if(!groups){m_showSystem=new QCheckBox("Show system accounts");connect(m_showSystem,&QCheckBox::toggled,this,&AccountsPage::refresh);layout->addWidget(m_showSystem);}
        layout->addWidget(m_table);connect(m_table,&QTableWidget::itemSelectionChanged,this,&AccountsPage::announceActions);connect(m_table,&QTableWidget::itemDoubleClicked,this,[this]{properties();});refresh();
    }
    QStringList actions() const override
    {
        QStringList result{"Refresh", m_groups ? "New Group" : "New User"};
        const int row = selectedSourceRow(m_table);
        if (row < 0) return result;
        result << "Properties";
        if (m_groups) {
            const QString name = m_groupItems.value(row).name;
            if (name != "root" && name != "wheel") result << "Rename Group" << "Delete Group";
            result << "Add Member" << "Remove Member";
        } else {
            const LocalUser user = m_userItems.value(row);
            const bool protectedIdentity = user.uid == 0 || user.uid == getuid();
            if (!protectedIdentity) result << "Rename User" << "Delete User";
            result << "Change Full Name" << "Change Password" << "Add to Group" << "Remove from Group";
            if (!protectedIdentity) {
                if (!user.lockKnown || !user.locked) result << "Lock Account";
                if (!user.lockKnown || user.locked) result << "Unlock Account";
            }
        }
        return result;
    }
    void refresh() override
    {
        m_table->setSortingEnabled(false);
        if(m_groups){m_groupItems=AccountsBackend::groups();m_table->setRowCount(m_groupItems.size());for(int r=0;r<m_groupItems.size();++r){setCell(m_table,r,0,m_groupItems[r].name,r);setCell(m_table,r,1,m_groupItems[r].description);setCell(m_table,r,2,QString::number(m_groupItems[r].gid));setCell(m_table,r,3,m_groupItems[r].members.join(", "));}emit statusMessage(QString("%1 group(s)").arg(m_groupItems.size()));}
        else{m_userItems.clear();for(const auto &user:AccountsBackend::users())if(!user.systemAccount||m_showSystem->isChecked())m_userItems<<user;m_table->setRowCount(m_userItems.size());for(int r=0;r<m_userItems.size();++r){const auto &u=m_userItems[r];setCell(m_table,r,0,u.name,r);setCell(m_table,r,1,u.fullName);setCell(m_table,r,2,u.systemAccount?"System account":"Local account");setCell(m_table,r,3,QString::number(u.uid));setCell(m_table,r,4,u.home);setCell(m_table,r,5,u.shell);}emit statusMessage(QString("%1 user account(s)").arg(m_userItems.size()));}
        m_table->setSortingEnabled(true);announceActions();
    }
    void triggerAction(const QString &action) override
    {
        if(action=="Refresh"){refresh();return;}if(action=="New User"){newUser();return;}if(action=="New Group"){newGroup();return;}
        const int row=selectedSourceRow(m_table);if(row<0)return;QString error;bool changed=false;
        if(action=="Properties"){properties();return;}
        if(m_groups){const QString name=m_groupItems.value(row).name;
            if(action=="Delete Group"&&confirm(this,"Delete Group",QString("Delete the Linux group '%1'? This does not delete its users.").arg(name)))changed=AccountsBackend::deleteGroup(name,&error);
            else if(action=="Rename Group"){bool ok=false;const QString value=QInputDialog::getText(this,"Rename Group","New group name:",QLineEdit::Normal,name,&ok);if(ok)changed=AccountsBackend::renameGroup(name,value,&error);}
            else if(action=="Add Member"||action=="Remove Member"){bool ok=false;QStringList users;for(const auto &u:AccountsBackend::users())users<<u.name;const QString user=QInputDialog::getItem(this,action,"User:",users,0,false,&ok);if(ok)changed=action=="Add Member"?AccountsBackend::addUserToGroup(user,name,&error):AccountsBackend::removeUserFromGroup(user,name,&error);}
        }else{const LocalUser user=m_userItems.value(row);const QString name=user.name;
            if(action=="Delete User"){QMessageBox box(QMessageBox::Warning,"Delete User",QString("Delete the Linux account '%1'?\n\nThe home folder is preserved by default.").arg(name),QMessageBox::Yes|QMessageBox::No,this);QCheckBox remove("Also delete the home folder and its files");box.setCheckBox(&remove);if(box.exec()==QMessageBox::Yes)changed=AccountsBackend::deleteUser(name,remove.isChecked(),&error);}
            else if(action=="Rename User"){bool ok=false;const QString value=QInputDialog::getText(this,"Rename User","New user name:",QLineEdit::Normal,name,&ok);if(ok)changed=AccountsBackend::renameUser(name,value,&error);}
            else if(action=="Change Full Name"){bool ok=false;const QString value=QInputDialog::getText(this,"Change Full Name","Full name:",QLineEdit::Normal,user.fullName,&ok);if(ok)changed=AccountsBackend::changeFullName(name,value,&error);}
            else if(action=="Change Password")changed=changePassword(name,&error);
            else if(action=="Lock Account")changed=AccountsBackend::setLocked(name,true,&error);
            else if(action=="Unlock Account")changed=AccountsBackend::setLocked(name,false,&error);
            else if(action=="Add to Group"||action=="Remove from Group"){bool ok=false;QStringList groups;if(action=="Add to Group"){const auto current=AccountsBackend::groupsForUser(name);for(const auto &g:AccountsBackend::groups())if(!current.contains(g.name))groups<<g.name;}else groups=AccountsBackend::groupsForUser(name);const QString group=QInputDialog::getItem(this,action,"Group:",groups,0,false,&ok);if(ok&&!group.isEmpty())changed=action=="Add to Group"?AccountsBackend::addUserToGroup(name,group,&error):AccountsBackend::removeUserFromGroup(name,group,&error);}
        }
        if (!changed && !error.isEmpty())
            QMessageBox::warning(this, action, error);
        refresh();
    }
private:
    void properties(){const int row=selectedSourceRow(m_table);if(row<0)return;if(m_groups){const auto g=m_groupItems.value(row);showProperties(this,g.name+" Properties",{{"Group name",g.name},{"Description",g.description},{"GID",QString::number(g.gid)},{"Members",g.members.join(", ")}});}else{const auto u=m_userItems.value(row);showProperties(this,u.name+" Properties",{{"Username",u.name},{"Full name",u.fullName},{"UID",QString::number(u.uid)},{"Primary group ID",QString::number(u.gid)},{"Home folder",u.home},{"Login shell",u.shell},{"Account type",u.systemAccount?"System account":"Local account"},{"Locked",u.lockKnown?yesNo(u.locked):"Not readable for this user"},{"Member of",AccountsBackend::groupsForUser(u.name).join(", ")}});}}
    void newGroup(){bool ok=false;const QString name=QInputDialog::getText(this,"New Group","Group name:",QLineEdit::Normal,{},&ok);if(!ok)return;QString error;if(!AccountsBackend::createGroup(name,&error))QMessageBox::warning(this,"New Group",error);refresh();}
    void newUser(){QDialog dialog(this);dialog.setWindowTitle("New User");auto *layout=new QFormLayout(&dialog);QLineEdit name,full,home,shell("/bin/bash"),password;password.setEchoMode(QLineEdit::Password);layout->addRow("User name:",&name);layout->addRow("Full name:",&full);layout->addRow("Home folder:",&home);layout->addRow("Login shell:",&shell);layout->addRow("Initial password:",&password);connect(&name,&QLineEdit::textChanged,&dialog,[&](const QString &text){if(home.text().isEmpty()||home.text().startsWith("/home/"))home.setText("/home/"+text);});auto *buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);layout->addRow(buttons);connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);if(dialog.exec()!=QDialog::Accepted)return;QString error;if(!AccountsBackend::createUser(name.text(),full.text(),home.text(),shell.text(),&error))QMessageBox::warning(this,"New User",error);else if(!password.text().isEmpty()&&!AccountsBackend::changePassword(name.text(),password.text(),&error))QMessageBox::warning(this,"Set Password",error);refresh();}
    bool changePassword(const QString &user,QString *error){QDialog dialog(this);dialog.setWindowTitle("Change Password");auto *layout=new QFormLayout(&dialog);QLineEdit one,two;one.setEchoMode(QLineEdit::Password);two.setEchoMode(QLineEdit::Password);layout->addRow("New password:",&one);layout->addRow("Confirm password:",&two);auto *buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);layout->addRow(buttons);connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);if(dialog.exec()!=QDialog::Accepted)return false;if(one.text().isEmpty()||one.text()!=two.text()){*error="The passwords do not match or are empty.";return false;}return AccountsBackend::changePassword(user,one.text(),error);}
    bool m_groups;QCheckBox *m_showSystem=nullptr;QTableWidget *m_table;QList<LocalUser> m_userItems;QList<LocalGroup> m_groupItems;
};

class PerformancePage final : public ManagementPage {
public:
    explicit PerformancePage(QWidget *parent) : ManagementPage("performance", parent)
    {
        auto *layout=new QVBoxLayout(this);layout->addWidget(heading("Performance Monitor"));
        auto *counterRow=new QHBoxLayout;for(const QString &name:QStringList{"CPU usage","Memory used","Swap used","Disk throughput","Network throughput"}){auto *check=new QCheckBox(name);check->setChecked(name=="CPU usage"||name=="Memory used");m_checks.insert(name,check);counterRow->addWidget(check);connect(check,&QCheckBox::toggled,this,&PerformancePage::updateVisible);}counterRow->addStretch();layout->addLayout(counterRow);
        m_graph=new PerformanceGraph;layout->addWidget(m_graph);m_values=new QLabel;m_values->setObjectName("performanceSummary");m_values->setTextInteractionFlags(Qt::TextSelectableByMouse);m_values->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);m_values->setMinimumWidth(0);layout->addWidget(m_values);
        connect(&m_watcher,&QFutureWatcher<PerformanceSnapshot>::finished,this,&PerformancePage::sampleReady);m_elapsed.start();auto *timer=new QTimer(this);connect(timer,&QTimer::timeout,this,&PerformancePage::refresh);timer->start(1000);updateVisible();refresh();
    }
    QStringList actions() const override{return{"Refresh","Open Task Manager","Properties"};}
    void refresh() override{if(!m_watcher.isRunning())m_watcher.setFuture(QtConcurrent::run([]{return PerformanceBackend::snapshot();}));}
    void triggerAction(const QString &action) override{if(action=="Refresh")refresh();else if(action=="Open Task Manager"){const QString app=executable({"aero7-task-manager","aero7-taskmgr","taskmgr","plasma-systemmonitor","ksysguard"});if(app.isEmpty())QMessageBox::information(this,"Task Manager","No supported task manager is installed.");else QProcess::startDetached(app,{});}else if(action=="Properties")showProperties(this,"Performance Monitor Properties",{{"Sources","/proc/stat, /proc/meminfo, /proc/diskstats, /proc/net/dev"},{"Sampling","Asynchronous, once per second"},{"History","Last 120 samples"}});}
private:
    void updateVisible(){QStringList names;for(auto it=m_checks.cbegin();it!=m_checks.cend();++it)if(it.value()->isChecked())names<<it.key();m_graph->setVisibleCounters(names);}
    void sampleReady(){const auto s=m_watcher.result();const double seconds=qMax(0.001,m_elapsed.restart()/1000.0);const auto delta=[](quint64 now,quint64 before){return now>=before?now-before:quint64(0);};double cpu=0,disk=0,net=0;if(m_hasPrevious){const quint64 total=delta(s.cpuTotalTicks,m_previous.cpuTotalTicks),idle=delta(s.cpuIdleTicks,m_previous.cpuIdleTicks);if(total>=idle&&total)cpu=100.0*(total-idle)/total;disk=(delta(s.diskReadBytes,m_previous.diskReadBytes)+delta(s.diskWriteBytes,m_previous.diskWriteBytes))/seconds;net=(delta(s.networkReceiveBytes,m_previous.networkReceiveBytes)+delta(s.networkTransmitBytes,m_previous.networkTransmitBytes))/seconds;}const double memory=s.memoryTotal?100.0*(s.memoryTotal-s.memoryAvailable)/s.memoryTotal:0;const double swap=s.swapTotal?100.0*(s.swapTotal-s.swapFree)/s.swapTotal:0;m_graph->addSample({{"CPU usage",cpu},{"Memory used",memory},{"Swap used",swap},{"Disk throughput",qMin(100.0,disk/(100.0*1024*1024)*100)},{"Network throughput",qMin(100.0,net/(100.0*1024*1024)*100)}});m_values->setText(QString("CPU: %1%   Memory: %2 / %3 (%4%)   Swap: %5%   Disk: %6/s   Network: %7/s   Processes: %8   Context switches: %9")
            .arg(cpu,0,'f',1).arg(Format::bytes(s.memoryTotal-s.memoryAvailable),Format::bytes(s.memoryTotal)).arg(memory,0,'f',1).arg(swap,0,'f',1).arg(Format::bytes(disk),Format::bytes(net)).arg(s.processCount).arg(s.contextSwitches));m_previous=s;m_hasPrevious=true;emit statusMessage("Live Linux performance counters");}
    PerformanceGraph *m_graph;QLabel *m_values;QMap<QString,QCheckBox*>m_checks;QFutureWatcher<PerformanceSnapshot>m_watcher;PerformanceSnapshot m_previous;bool m_hasPrevious=false;QElapsedTimer m_elapsed;
};

class DeviceManagerPage final : public ManagementPage {
public:
    explicit DeviceManagerPage(QWidget *parent):ManagementPage("device-manager",parent){auto *layout=new QVBoxLayout(this);layout->addWidget(heading("Device Manager"));m_state=new QLabel;layout->addWidget(m_state);auto *open=new QPushButton(QIcon::fromTheme("preferences-system-devices"),"Open Device Manager");connect(open,&QPushButton::clicked,this,[this]{triggerAction("Open Device Manager");});layout->addWidget(open,0,Qt::AlignLeft);layout->addStretch();refresh();}
    QStringList actions()const override{return{"Open Device Manager","Refresh","Properties"};}void refresh()override{m_state->setText(executable({"devmgmt"}).isEmpty()?"The external Device Manager (devmgmt) is not installed.":"View and configure hardware devices installed on this computer.");emit statusMessage(m_state->text());}
    void triggerAction(const QString&a)override{if(a=="Open Device Manager"){const QString app=executable({"devmgmt"});if(app.isEmpty())QMessageBox::information(this,"Device Manager","devmgmt is not installed.");else QProcess::startDetached(app,{});}else if(a=="Properties")showProperties(this,"Device Manager Integration",{{"Application","linux-devmgmt"},{"Executable","devmgmt"},{"Hardware backend","sysfs, procfs, udev"}});else ManagementPage::triggerAction(a);}private:QLabel*m_state;
};

class DisksPage final : public ManagementPage {
public:
    explicit DisksPage(QWidget *parent)
        : ManagementPage("disk-management", parent),
          m_table(table({"Volume", "Layout", "Type", "File System", "Status",
                         "Capacity", "Free Space", "% Free"}))
    {
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        auto *splitter = new QSplitter(Qt::Vertical);
        splitter->setChildrenCollapsible(false);

        m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
        m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
        m_table->setContextMenuPolicy(Qt::CustomContextMenu);
        splitter->addWidget(m_table);

        auto *lowerPane = new QWidget;
        auto *lowerLayout = new QVBoxLayout(lowerPane);
        lowerLayout->setContentsMargins(0, 0, 0, 0);
        m_diskMap = new DiskMapWidget;
        auto *scroll = new QScrollArea;
        scroll->setWidget(m_diskMap);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::StyledPanel);
        lowerLayout->addWidget(scroll);

        auto *legend = new QHBoxLayout;
        auto addLegend = [legend](const QString &color, const QString &text) {
            auto *swatch = new QLabel;
            swatch->setFixedSize(14, 9);
            swatch->setStyleSheet(QString("background:%1;border:1px solid #777;").arg(color));
            legend->addWidget(swatch);
            legend->addWidget(new QLabel(text));
        };
        addLegend("#103a89", "Primary partition");
        addLegend("#000000", "Unallocated");
        legend->addStretch();
        auto *notice = new QLabel("Disk changes use UDisks2 and require administrator authorization.");
        notice->setStyleSheet("color:#666;");
        notice->setToolTip("Aero7 revalidates disk identity and protects the active system and boot disk before every change.");
        legend->addWidget(notice);
        lowerLayout->addLayout(legend);
        splitter->addWidget(lowerPane);
        splitter->setStretchFactor(0, 2);
        splitter->setStretchFactor(1, 3);
        splitter->setSizes({245, 330});
        layout->addWidget(splitter);

        connect(m_table, &QTableWidget::itemSelectionChanged, this, [this] {
            if (m_syncingSelection) return;
            selectDevice(selectedSourceRow(m_table), false);
        });
        connect(m_table, &QTableWidget::itemDoubleClicked, this, [this] { properties(); });
        connect(m_table, &QWidget::customContextMenuRequested, this, [this](const QPoint &position) {
            if (QTableWidgetItem *item = m_table->itemAt(position)) {
                m_table->selectRow(item->row());
                showContextMenu(m_table->viewport()->mapToGlobal(position));
            }
        });
        connect(m_diskMap, &DiskMapWidget::mapSelectionChanged, this,
                [this](const DiskMapSelection &selection) {
            if (selection.kind == DiskMapSelection::Unallocated) {
                m_selectedFreeRegion = selection.freeRegion;
                m_selectedDeviceIndex = -1;
                m_table->clearSelection();
                announceActions();
            } else if (selection.kind == DiskMapSelection::Device) {
                m_selectedFreeRegion.reset();
                selectDevice(selection.deviceIndex, true);
            }
        });
        connect(m_diskMap, &DiskMapWidget::deviceActivated, this, [this](int index) {
            selectDevice(index, true);
            properties();
        });
        connect(m_diskMap, &DiskMapWidget::mapContextMenuRequested, this,
                [this](const DiskMapSelection &, const QPoint &globalPosition) {
            showContextMenu(globalPosition);
        });
        auto *timer = new QTimer(this);
        connect(timer, &QTimer::timeout, this, &DisksPage::refresh);
        timer->start(20000);
        refresh();
    }

    QStringList actions() const override
    {
        QStringList result{"Refresh", "Rescan Disks"};
        if (m_selectedFreeRegion) {
            result.prepend("New Simple Volume...");
            return result;
        }
        const BlockDevice *device = selectedDevice();
        if (!device) return result;
        if (!device->partition && device->partitionable && device->partitionTable.isEmpty()
            && !device->critical && !device->readOnly)
            result.prepend("Initialize Disk");
        result << "Properties" << "Help";
        if (canFormat(*device)) result.prepend("Format...");
        if (canExtend(*device)) result.prepend("Extend Volume...");
        if (canShrink(*device)) result.prepend("Shrink Volume...");
        if (canDelete(*device)) result.prepend("Delete Volume...");
        if (canManageStartupMount(*device))
            result << (device->aeroManagedStartupMount
                ? "Disable Mount at Startup" : "Mount at Startup...");
        if (device->mountable)
            result << (device->mountPoint.isEmpty() ? "Mount" : "Unmount");
        if (!device->mountPoint.isEmpty()) result << "Open" << "Explore";
        return result;
    }

    void refresh() override
    {
        const QString selectedPath = selectedDevice() ? selectedDevice()->objectPath : QString{};
        const std::optional<DiskFreeRegion> selectedRegion = m_selectedFreeRegion;
        QString error;
        m_devices = UDisksBackend::devices(&error);
        m_volumeIndexes.clear();
        for (int index = 0; index < m_devices.size(); ++index) {
            const BlockDevice &device = m_devices[index];
            if (device.partition || device.mountable || !device.fileSystem.isEmpty())
                m_volumeIndexes << index;
        }

        m_syncingSelection = true;
        m_table->setSortingEnabled(false);
        m_table->setRowCount(m_volumeIndexes.size());
        for (int row = 0; row < m_volumeIndexes.size(); ++row) {
            const int index = m_volumeIndexes[row];
            const BlockDevice &device = m_devices[index];
            setCell(m_table, row, 0, displayName(device), index);
            setCell(m_table, row, 1, device.partition ? "Simple" : "Simple");
            setCell(m_table, row, 2, device.removable ? "Removable" : "Basic");
            setCell(m_table, row, 3, device.fileSystem);
            setCell(m_table, row, 4, status(device));
            setCell(m_table, row, 5, Format::bytes(device.size));
            setCell(m_table, row, 6, device.freeSpaceKnown ? Format::bytes(device.freeBytes) : "—");
            setCell(m_table, row, 7, device.freeSpaceKnown && device.size
                ? QString::number(100.0 * device.freeBytes / device.size, 'f', 0) + '%' : "—");
        }
        m_table->setSortingEnabled(true);
        m_diskMap->setDevices(m_devices);

        m_selectedDeviceIndex = -1;
        m_selectedFreeRegion.reset();
        if (!selectedPath.isEmpty()) {
            for (int index = 0; index < m_devices.size(); ++index) {
                if (m_devices[index].objectPath == selectedPath) {
                    m_selectedDeviceIndex = index;
                    break;
                }
            }
        }
        if (m_selectedDeviceIndex >= 0) {
            m_diskMap->selectDevice(m_selectedDeviceIndex);
            selectTableRow(m_selectedDeviceIndex);
        } else if (selectedRegion) {
            for (const DiskFreeRegion &region : UDisksBackend::freeRegions(m_devices)) {
                if (region.diskObjectPath == selectedRegion->diskObjectPath
                    && region.offset == selectedRegion->offset && region.size == selectedRegion->size) {
                    m_selectedFreeRegion = region;
                    m_diskMap->selectFreeRegion(region);
                    break;
                }
            }
            m_table->clearSelection();
        } else {
            m_table->clearSelection();
            m_diskMap->selectDevice(-1);
        }
        m_syncingSelection = false;
        emit statusMessage(error.isEmpty()
            ? QString("%1 disk and volume object(s) — UDisks2").arg(m_devices.size()) : error);
        announceActions();
    }

    void triggerAction(const QString &action) override
    {
        if (action == "Refresh") {
            refresh();
            return;
        }
        if (action == "Rescan Disks") {
            QStringList errors;
            QSet<QString> rescanned;
            for (const BlockDevice &device : std::as_const(m_devices)) {
                if (device.partition || rescanned.contains(device.objectPath)) continue;
                rescanned.insert(device.objectPath);
                QString error;
                if (!UDisksBackend::rescan(device.objectPath, &error) && !error.isEmpty())
                    errors << QString("%1: %2").arg(device.device, error);
            }
            if (!errors.isEmpty()) QMessageBox::warning(this, "Rescan Disks", errors.join('\n'));
            refresh();
            return;
        }
        if (action == "New Simple Volume...") {
            newSimpleVolume();
            return;
        }
        const BlockDevice *device = selectedDevice();
        if (!device) return;
        const QString objectPath = device->objectPath;
        if (action == "Properties") {
            properties();
            return;
        }
        if (action == "Initialize Disk") {
            initializeDisks();
            return;
        }
        if (action == "Format...") {
            formatVolume();
            return;
        }
        if (action == "Extend Volume...") {
            extendVolume();
            return;
        }
        if (action == "Shrink Volume...") {
            shrinkVolume();
            return;
        }
        if (action == "Delete Volume...") {
            deleteVolume();
            return;
        }
        if (action == "Mount at Startup..." || action == "Disable Mount at Startup") {
            setMountAtStartup(action == "Mount at Startup...");
            return;
        }
        if (action == "Help") {
            QMessageBox::information(this, "Disk Management Help",
                "Initialize creates an empty GPT or MBR partition table. New Simple Volume creates and optionally formats a partition. Format erases a selected volume. Extend and Shrink resize compatible filesystems and partitions. Delete Volume releases a partition as unallocated space. Mount at Startup adds a persistent Aero7 mount entry.");
            return;
        }
        if (action == "Open" || action == "Explore") {
            if (device->mountPoint.isEmpty()
                || !QDesktopServices::openUrl(QUrl::fromLocalFile(device->mountPoint)))
                QMessageBox::warning(this, action, "The selected volume has no accessible mount point.");
            return;
        }
        QString error;
        bool succeeded = false;
        if (action == "Mount") succeeded = UDisksBackend::mount(objectPath, &error);
        else if (action == "Unmount") succeeded = UDisksBackend::unmount(objectPath, &error);
        if (!succeeded && !error.isEmpty()) QMessageBox::warning(this, action, error);
        refresh();
    }

private:
    static QString displayName(const BlockDevice &device)
    {
        return device.label.isEmpty() ? device.device : device.label;
    }

    static QString status(const BlockDevice &device)
    {
        if (device.readOnly) return "Healthy (Read-only)";
        if (device.mountPoint == "/") return "Healthy (System, Mounted)";
        if (device.mountPoint == "/boot" || device.mountPoint == "/boot/efi")
            return "Healthy (EFI System Partition)";
        if (!device.mountPoint.isEmpty()) return "Healthy (Mounted)";
        return "Healthy";
    }

    const BlockDevice *selectedDevice() const
    {
        return m_selectedDeviceIndex >= 0 && m_selectedDeviceIndex < m_devices.size()
            ? &m_devices[m_selectedDeviceIndex] : nullptr;
    }

    void selectDevice(int deviceIndex, bool syncTable)
    {
        if (deviceIndex < 0 || deviceIndex >= m_devices.size()) return;
        m_selectedDeviceIndex = deviceIndex;
        m_selectedFreeRegion.reset();
        m_diskMap->selectDevice(deviceIndex);
        if (syncTable) {
            m_syncingSelection = true;
            selectTableRow(deviceIndex);
            m_syncingSelection = false;
        }
        announceActions();
    }

    void selectTableRow(int deviceIndex)
    {
        m_table->clearSelection();
        for (int row = 0; row < m_table->rowCount(); ++row) {
            const QTableWidgetItem *item = m_table->item(row, 0);
            if (item && item->data(Qt::UserRole).toInt() == deviceIndex) {
                m_table->selectRow(row);
                m_table->scrollToItem(item);
                return;
            }
        }
    }

    void showContextMenu(const QPoint &globalPosition)
    {
        QMenu menu(this);
        const QStringList available = actions();
        for (const QString &action : available) {
            if (action == "Refresh" || action == "Rescan Disks") continue;
            QAction *entry = menu.addAction(action);
            connect(entry, &QAction::triggered, this, [this, action] { triggerAction(action); });
        }
        if (!menu.actions().isEmpty()) menu.addSeparator();
        QAction *refreshAction = menu.addAction("Refresh");
        connect(refreshAction, &QAction::triggered, this, [this] { refresh(); });
        menu.exec(globalPosition);
    }

    int diskNumber(const QString &diskObjectPath) const
    {
        int number = 0;
        QSet<QString> seen;
        for (const BlockDevice &candidate : m_devices) {
            if (candidate.partition) continue;
            const QString key = candidate.objectPath;
            if (seen.contains(key)) continue;
            if (key == diskObjectPath) return number;
            seen.insert(key);
            ++number;
        }
        return number;
    }

    const BlockDevice *diskForRegion(const DiskFreeRegion &region) const
    {
        for (const BlockDevice &device : m_devices)
            if (!device.partition && device.objectPath == region.diskObjectPath) return &device;
        return nullptr;
    }

    bool canFormat(const BlockDevice &device) const
    {
        return device.partition && !device.readOnly && !device.critical
            && (device.mountable || !device.fileSystem.isEmpty());
    }

    bool canExtend(const BlockDevice &device) const
    {
        if (!device.partition || device.readOnly || device.critical || device.fileSystem.isEmpty()
            || !UDisksBackend::adjacentFreeRegion(m_devices, device)) return false;
        const auto capability = UDisksBackend::capabilityFor(device.fileSystem);
        return capability && (capability->canGrowMounted || capability->canGrowUnmounted);
    }

    bool canShrink(const BlockDevice &device) const
    {
        if (!device.partition || device.readOnly || device.critical || device.fileSystem.isEmpty())
            return false;
        const auto capability = UDisksBackend::capabilityFor(device.fileSystem);
        return capability && (capability->canShrinkMounted || capability->canShrinkUnmounted);
    }

    static bool canDelete(const BlockDevice &device)
    {
        return device.partition && !device.readOnly && !device.critical;
    }

    static bool canManageStartupMount(const BlockDevice &device)
    {
        return device.mountable && !device.uuid.isEmpty()
            && !device.readOnly && !device.critical
            && (!device.mountAtStartup || device.aeroManagedStartupMount);
    }

    struct BusyCursor {
        BusyCursor() { QApplication::setOverrideCursor(Qt::WaitCursor); }
        ~BusyCursor() { QApplication::restoreOverrideCursor(); }
    };

    void initializeDisks()
    {
        QList<QPair<int, BlockDevice>> candidates;
        int number = 0;
        for (const BlockDevice &disk : m_devices) {
            if (disk.partition) continue;
            if (disk.partitionable && disk.partitionTable.isEmpty() && !disk.critical
                && !disk.readOnly && !disk.optical)
                candidates << qMakePair(number, disk);
            ++number;
        }
        if (candidates.isEmpty()) return;
        InitializeDiskDialog dialog(candidates, this);
        if (dialog.exec() != QDialog::Accepted) return;
        if (!confirm(this, "Initialize Disk",
            "Initializing replaces partition-table metadata on the selected disk(s). Existing data may become inaccessible.\n\nDo you want to continue?")) return;
        QStringList errors;
        BusyCursor cursor;
        for (const int selectedNumber : dialog.selectedDiskIndexes()) {
            for (const auto &[numberValue, disk] : candidates) {
                if (numberValue != selectedNumber) continue;
                QString error;
                if (!UDisksBackend::initializeDisk(UDisksBackend::targetFor(disk),
                                                   dialog.partitionTableType(), &error))
                    errors << QString("Disk %1: %2").arg(numberValue).arg(error);
            }
        }
        refresh();
        if (!errors.isEmpty()) QMessageBox::warning(this, "Initialize Disk", errors.join("\n\n"));
    }

    void newSimpleVolume()
    {
        if (!m_selectedFreeRegion) return;
        const DiskFreeRegion region = *m_selectedFreeRegion;
        const BlockDevice *selectedDisk = diskForRegion(region);
        if (!selectedDisk) return;
        // Modal wizards keep the event loop running, so the periodic refresh timer can replace
        // m_devices while a wizard is open. Keep an identity snapshot instead of retaining a
        // pointer into that container across wizard.exec(). The backend revalidates this snapshot
        // against the live UDisks2 inventory immediately before changing the disk.
        const BlockDevice disk = *selectedDisk;
        if (disk.partitionTable.isEmpty()) {
            QMessageBox::information(this, "New Simple Volume", "Initialize the disk first.");
            return;
        }
        QString capabilityError;
        const auto capabilities = UDisksBackend::fileSystemCapabilities(&capabilityError);
        NewSimpleVolumeWizard wizard(region, capabilities, this);
        if (wizard.exec() != QDialog::Accepted) return;
        const NewVolumeOptions options = wizard.options();
        if (!confirm(this, "New Simple Volume",
            QString("Create a %1 MB simple volume on Disk %2%3?\n\nDisk changes cannot be undone.")
                .arg(options.sizeBytes / (1024ULL * 1024ULL))
                .arg(diskNumber(region.diskObjectPath))
                .arg(options.format ? QString(" and format it as %1").arg(options.fileSystem) : QString{})))
            return;
        CreateVolumeRequest request;
        request.disk = UDisksBackend::targetFor(disk);
        request.regionOffset = region.offset;
        request.regionSize = region.size;
        request.requestedSize = options.sizeBytes;
        request.fileSystem = options.fileSystem;
        request.label = options.label;
        request.format = options.format;
        request.quickFormat = options.quickFormat;
        request.mount = options.format;
        DiskOperationResult operation;
        {
            BusyCursor cursor;
            operation = UDisksBackend::createVolume(request);
        }
        refresh();
        if (operation.success) QMessageBox::information(this, "New Simple Volume", operation.message);
        else QMessageBox::warning(this, operation.partial ? "New Simple Volume — Partially Completed"
                                                        : "New Simple Volume", operation.message);
    }

    void formatVolume()
    {
        const BlockDevice *device = selectedDevice();
        if (!device || !canFormat(*device)) return;
        const DiskOperationTarget target = UDisksBackend::targetFor(*device);
        const QString name = displayName(*device);
        const auto capabilities = UDisksBackend::fileSystemCapabilities();
        FormatVolumeDialog dialog(name, device->label, device->fileSystem, capabilities, this);
        if (dialog.exec() != QDialog::Accepted) return;
        const FormatVolumeOptions options = dialog.options();
        if (QMessageBox::warning(this, "Format " + name,
            "Formatting this volume will erase all data on it.\n\n"
            "Back up any data you want to keep before formatting.\n\nDo you want to continue?",
            QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Ok) return;
        FormatVolumeRequest request{target, options.fileSystem, options.label,
                                    options.quickFormat, true};
        DiskOperationResult operation;
        {
            BusyCursor cursor;
            operation = UDisksBackend::formatVolume(request);
        }
        refresh();
        if (operation.success) QMessageBox::information(this, "Format", operation.message);
        else QMessageBox::warning(this, operation.partial ? "Format — Partially Completed" : "Format",
                                  operation.message);
    }

    void extendVolume()
    {
        const BlockDevice *device = selectedDevice();
        if (!device || !canExtend(*device)) return;
        const DiskOperationTarget target = UDisksBackend::targetFor(*device);
        const QString volumeName = displayName(*device);
        const auto region = UDisksBackend::adjacentFreeRegion(m_devices, *device);
        if (!region) return;
        ExtendVolumeWizard wizard(diskNumber(region->diskObjectPath), device->size, region->size, this);
        if (wizard.exec() != QDialog::Accepted) return;
        const quint64 additional = wizard.additionalBytes();
        if (!confirm(this, "Extend Volume",
            QString("Extend %1 by %2?\n\nThe partition boundary and filesystem will both be enlarged.")
                .arg(volumeName, Format::bytes(additional)))) return;
        ExtendVolumeRequest request{target, additional, region->offset, region->size};
        DiskOperationResult operation;
        {
            BusyCursor cursor;
            operation = UDisksBackend::extendVolume(request);
        }
        refresh();
        if (operation.success) QMessageBox::information(this, "Extend Volume", operation.message);
        else QMessageBox::warning(this, operation.partial ? "Extend Volume — Partially Completed"
                                                        : "Extend Volume", operation.message);
    }

    void shrinkVolume()
    {
        const BlockDevice *device = selectedDevice();
        if (!device || !canShrink(*device)) return;
        const DiskOperationTarget target = UDisksBackend::targetFor(*device);
        const QString volumeName = displayName(*device);
        QString error;
        quint64 maximum = 0;
        {
            BusyCursor cursor;
            maximum = UDisksBackend::maximumShrinkBytes(target, &error);
        }
        refresh();
        if (!maximum) {
            QMessageBox::warning(this, "Shrink Volume", error.isEmpty()
                ? "This volume does not have enough verified free space to shrink." : error);
            return;
        }
        ShrinkVolumeDialog dialog(volumeName, target.size, maximum, this);
        if (dialog.exec() != QDialog::Accepted) return;
        const quint64 amount = dialog.shrinkBytes();
        if (!confirm(this, "Shrink Volume",
            QString("Shrink %1 by %2?\n\nThe released space will become unallocated. "
                    "The volume may be temporarily unavailable while it is resized. "
                    "Compatible filesystems are checked and repaired first when required.")
                .arg(volumeName, Format::bytes(amount)))) return;
        DiskOperationResult operation;
        {
            BusyCursor cursor;
            operation = UDisksBackend::shrinkVolume({target, amount, true});
        }
        refresh();
        if (operation.success) QMessageBox::information(this, "Shrink Volume", operation.message);
        else QMessageBox::warning(this, operation.partial ? "Shrink Volume — Partially Completed"
                                                        : "Shrink Volume", operation.message);
    }

    void deleteVolume()
    {
        const BlockDevice *device = selectedDevice();
        if (!device || !canDelete(*device)) return;
        const DiskOperationTarget target = UDisksBackend::targetFor(*device);
        const QString volumeName = displayName(*device);
        if (QMessageBox::warning(this, "Delete Simple Volume",
            QString("Deleting %1 will erase all data on it.\n\n"
                    "The partition will be removed and its space will become unallocated. "
                    "This action cannot be undone.\n\nDo you want to continue?").arg(volumeName),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
        DiskOperationResult operation;
        {
            BusyCursor cursor;
            operation = UDisksBackend::deleteVolume(target);
        }
        refresh();
        if (operation.success) QMessageBox::information(this, "Delete Volume", operation.message);
        else QMessageBox::warning(this, operation.partial ? "Delete Volume — Partially Completed"
                                                        : "Delete Volume", operation.message);
    }

    void setMountAtStartup(bool enabled)
    {
        const BlockDevice *device = selectedDevice();
        if (!device || !canManageStartupMount(*device)) return;
        const DiskOperationTarget target = UDisksBackend::targetFor(*device);
        QString folder;
        if (enabled) {
            bool accepted = false;
            folder = QInputDialog::getText(this, "Mount at Startup", "Mount point:",
                QLineEdit::Normal, UDisksBackend::recommendedStartupMountPoint(*device), &accepted);
            if (!accepted) return;
            folder = folder.trimmed();
            if (!confirm(this, "Mount at Startup",
                QString("Add this volume to Aero7 startup mounting at %1?\n\n"
                        "This writes a persistent system mount entry and requires administrator authorization.")
                    .arg(folder))) return;
        } else if (!confirm(this, "Disable Mount at Startup",
            QString("Stop mounting this volume automatically at %1?")
                .arg(device->startupMountPoint))) return;
        DiskOperationResult operation;
        {
            BusyCursor cursor;
            operation = UDisksBackend::setMountAtStartup(target, enabled, folder);
        }
        refresh();
        const QString title = enabled ? "Mount at Startup" : "Disable Mount at Startup";
        if (operation.success) QMessageBox::information(this, title, operation.message);
        else QMessageBox::warning(this, operation.partial ? title + " — Partially Completed" : title,
                                  operation.message);
    }

    void properties()
    {
        const BlockDevice *device = selectedDevice();
        if (!device) return;
        showProperties(this, displayName(*device) + " Properties",
            {{"Device", device->device},
             {"Drive", QString("%1 %2").arg(device->driveVendor, device->driveModel).trimmed()},
             {"Connection", device->connectionBus},
             {"Serial", device->serial},
             {"Capacity", Format::bytes(device->size)},
             {"Partition table", device->partitionTable},
             {"Partition number", device->partition ? QString::number(device->partitionNumber) : "—"},
             {"File system", device->fileSystem},
             {"Label", device->label},
             {"UUID", device->uuid},
             {"PARTUUID", device->partUuid},
             {"Mount point", device->mountPoints.join(", ")},
             {"Mount at startup", device->mountAtStartup
                ? QString("Yes (%1)").arg(device->startupMountPoint) : "No"},
             {"Free space", device->freeSpaceKnown ? Format::bytes(device->freeBytes) : "—"},
             {"System device", yesNo(device->systemDevice)},
             {"Removable", yesNo(device->removable)},
             {"Read-only", yesNo(device->readOnly)},
             {"UDisks2 object", device->objectPath}});
    }

    QTableWidget *m_table = nullptr;
    DiskMapWidget *m_diskMap = nullptr;
    QList<BlockDevice> m_devices;
    QList<int> m_volumeIndexes;
    int m_selectedDeviceIndex = -1;
    std::optional<DiskFreeRegion> m_selectedFreeRegion;
    bool m_syncingSelection = false;
};

class ServicesPage final : public ManagementPage {
public:
    explicit ServicesPage(QWidget *parent):ManagementPage("services",parent),m_table(table({"Name","Description","Status","Startup Type","Unit Scope"}))
    {auto*layout=new QVBoxLayout(this);layout->addWidget(heading("Services"));layout->addWidget(description("System and user background services managed through systemd's D-Bus API. System changes use systemd's polkit authorization."));layout->addWidget(m_table);connect(m_table,&QTableWidget::itemSelectionChanged,this,&ServicesPage::announceActions);connect(m_table,&QTableWidget::itemDoubleClicked,this,[this]{properties();});auto*timer=new QTimer(this);connect(timer,&QTimer::timeout,this,&ServicesPage::refresh);timer->start(15000);refresh();}
    QStringList actions()const override{QStringList a{"Refresh"};const int r=selectedSourceRow(m_table);if(r>=0&&r<m_items.size()){const auto&s=m_items[r];a<<"Properties";if(s.active=="active")a<<"Stop"<<"Restart"<<"Reload";else a<<"Start";if(s.startup=="enabled")a<<"Disable";else if(s.startup!="static"&&s.startup!="generated")a<<"Enable";if(s.startup=="masked")a<<"Unmask";else a<<"Mask";}return a;}
    void refresh()override{QString error;m_items=SystemdBackend::services(&error);m_table->setSortingEnabled(false);m_table->setRowCount(m_items.size());for(int r=0;r<m_items.size();++r){const auto&s=m_items[r];setCell(m_table,r,0,s.unit,r);setCell(m_table,r,1,s.description);QString status=s.active=="active"?"Running":s.active=="inactive"?"Stopped":s.active=="activating"?"Starting":s.active=="deactivating"?"Stopping":s.active=="failed"?"Failed":s.active;setCell(m_table,r,2,status);QString startup=s.startup=="enabled"?"Automatic":s.startup=="disabled"?"Disabled / Manual":s.startup=="masked"?"Disabled (Masked)":s.startup=="static"?"Static":s.startup=="generated"?"Generated":s.startup=="indirect"?"Indirect":s.startup;setCell(m_table,r,3,startup);setCell(m_table,r,4,SystemdBackend::scopeName(s.scope));}m_table->setSortingEnabled(true);emit statusMessage(error.isEmpty()?QString("%1 service(s)").arg(m_items.size()):error);announceActions();}
    void triggerAction(const QString&a)override{if(a=="Refresh"){refresh();return;}const int r=selectedSourceRow(m_table);if(r<0||r>=m_items.size())return;const auto s=m_items[r];if(a=="Properties"){properties();return;}QString error;bool ok=false;if(a=="Start")ok=SystemdBackend::startUnit(s.unit,s.scope,&error);else if(a=="Stop")ok=SystemdBackend::stopUnit(s.unit,s.scope,&error);else if(a=="Restart")ok=SystemdBackend::restartUnit(s.unit,s.scope,&error);else if(a=="Reload")ok=SystemdBackend::reloadUnit(s.unit,s.scope,&error);else if(a=="Enable")ok=SystemdBackend::setUnitEnabled(s.unit,s.scope,true,&error);else if(a=="Disable")ok=SystemdBackend::setUnitEnabled(s.unit,s.scope,false,&error);else if(a=="Mask"&&confirm(this,"Mask Service",QString("Mask %1? It cannot be started until it is unmasked.").arg(s.unit)))ok=SystemdBackend::setUnitMasked(s.unit,s.scope,true,&error);else if(a=="Unmask")ok=SystemdBackend::setUnitMasked(s.unit,s.scope,false,&error);if(!ok&&!error.isEmpty())QMessageBox::warning(this,a,error);refresh();}
private:void properties(){const int r=selectedSourceRow(m_table);if(r<0||r>=m_items.size())return;const auto s=m_items[r];QString error;const auto d=SystemdBackend::details(s.unit,s.scope,&error);QString journalError;const auto logs=JournalBackend::forUnit(s.unit,s.scope==UnitScope::User,&journalError,80);QStringList text;for(const auto&e:logs)text<<QString("%1  %2  %3").arg(e.time,e.priority,e.message);showProperties(this,s.unit+" Properties",{{"Service name",d.unit},{"Description",d.description},{"Unit path",d.fragmentPath},{"Loaded state",d.loadState},{"Active state",d.activeState},{"Sub state",d.subState},{"Startup state",d.startupState},{"Scope",SystemdBackend::scopeName(s.scope)},{"Requires",d.requiredUnits.join(", ")},{"Wants",d.wants.join(", ")},{"After",d.after.join(", ")},{"Before",d.before.join(", ")}},text.join('\n'));}QTableWidget*m_table;QList<ServiceInfo>m_items;
};
}

namespace Pages {
ManagementPage *create(const QString &id,QWidget *parent)
{
    if(id=="overview")return new OverviewPage(parent);
    if(id=="system-tools")return new ContainerPage(id,"System Tools","Administrative tools for schedules, logs, sharing, accounts, performance, and hardware.",{{"Task Scheduler","task-scheduler"},{"Event Viewer","event-viewer"},{"Shared Folders","shared-folders"},{"Local Users and Groups","local-users-groups"},{"Performance Monitor","performance"},{"Device Manager","device-manager"}},parent);
    if(id=="task-scheduler")return new TimersPage(parent);
    if(id=="event-viewer")return new EventsPage(parent);
    if(id=="shared-folders")return new SharesPage(parent);
    if(id=="local-users-groups")return new LocalAccountsPage(parent);
    if(id=="users")return new AccountsPage(id,false,parent);
    if(id=="groups")return new AccountsPage(id,true,parent);
    if(id=="performance")return new PerformancePage(parent);
    if(id=="device-manager")return new DeviceManagerPage(parent);
    if(id=="storage")return new ContainerPage(id,"Storage","View physical disks, mounted filesystems, and volumes through UDisks2.",{{"Disk Management","disk-management"}},parent);
    if(id=="disk-management")return new DisksPage(parent);
    if(id=="services-applications")return new ContainerPage(id,"Services and Applications","Manage systemd operating-system and application services.",{{"Services","services"}},parent);
    if(id=="services")return new ServicesPage(parent);
    return new OverviewPage(parent);
}
}
