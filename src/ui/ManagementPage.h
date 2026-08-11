#pragma once

#include <QWidget>

class ManagementPage : public QWidget {
    Q_OBJECT
public:
    explicit ManagementPage(QString nodeId, QWidget *parent = nullptr);
    QString nodeId() const { return m_nodeId; }
    virtual QStringList actions() const;

public slots:
    virtual void refresh();
    virtual void triggerAction(const QString &action);

signals:
    void actionsChanged(const QStringList &actions);
    void statusMessage(const QString &message);
    void navigationRequested(const QString &nodeId);

protected:
    void announceActions();
    void navigateTo(const QString &nodeId) { emit navigationRequested(nodeId); }

private:
    QString m_nodeId;
};
