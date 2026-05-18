#ifndef LOGINDIALOG_H
#define LOGINDIALOG_H

#include <QDialog>
#include "INetMediator.h"
#include "TcpClientMediator.h"
#include "packdef.h"

namespace Ui { class LoginDialog; }

class LoginDialog : public QDialog
{
    Q_OBJECT
public:
    explicit LoginDialog(QWidget *parent = nullptr);
    ~LoginDialog();

private slots:
    void on_pb_login_clicked();
    void on_pb_register_clicked();
    void slot_readyData(unsigned int ip, const QByteArray& data);
    void slot_disconnected();

private:
    void connectToServer();
    Ui::LoginDialog *ui;
    TcpClientMediator *m_mediator;
    QString m_serverIp;
};

#endif
