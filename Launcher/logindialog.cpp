#include "logindialog.h"
#include "ui_logindialog.h"
#include "role_select_dialog.h"
#include <QMessageBox>
#include <QProcess>
#include <QProcessEnvironment>
#include <QApplication>
#include <QLibraryInfo>
#include <cstdio>
#include "server_config.h"

LoginDialog::LoginDialog(QWidget *parent)
    : QDialog(parent), ui(new Ui::LoginDialog), m_mediator(nullptr)
{
    ui->setupUi(this);
    m_serverIp = SERVER_HOST;
    connectToServer();
}

LoginDialog::~LoginDialog()
{
    if (m_mediator) { m_mediator->CloseNet(); delete m_mediator; }
    delete ui;
}

void LoginDialog::connectToServer()
{
    m_mediator = new TcpClientMediator;
    connect(m_mediator, SIGNAL(SIG_ReadyData(uint,QByteArray)),
            this, SLOT(slot_readyData(uint,QByteArray)), Qt::QueuedConnection);
    connect(m_mediator, SIGNAL(SIG_disConnect()),
            this, SLOT(slot_disconnected()), Qt::QueuedConnection);
    bool ok = m_mediator->OpenNet(m_serverIp.toUtf8().constData(), 8000);
    fprintf(stderr, "OpenNet result: %d server: %s\n", ok, m_serverIp.toUtf8().constData());
}

void LoginDialog::on_pb_login_clicked()
{
    if (ui->le_tel->text().isEmpty() || ui->le_password->text().isEmpty()) {
        QMessageBox::warning(this, "提示", "请输入手机号和密码");
        return;
    }
    STRU_LOGIN_RQ rq;
    QByteArray telBytes = ui->le_tel->text().toUtf8();
    QByteArray pwdBytes = ui->le_password->text().toUtf8();
    strncpy(rq.tel, telBytes.constData(), _MAX_SIZE - 1);
    rq.tel[_MAX_SIZE - 1] = '\0';
    strncpy(rq.password, pwdBytes.constData(), _MAX_SIZE - 1);
    rq.password[_MAX_SIZE - 1] = '\0';
    fprintf(stderr, "Sending LOGIN_RQ tel: %s type: %d\n", rq.tel, rq.type);
    m_mediator->SendData(0, (char*)&rq, sizeof(rq));
}

void LoginDialog::on_pb_register_clicked()
{
    STRU_REGISTER_RQ rq;
    QByteArray telBytes = ui->le_tel->text().toUtf8();
    QByteArray nameBytes = ui->le_name->text().toUtf8();
    QByteArray pwdBytes = ui->le_password->text().toUtf8();
    strncpy(rq.tel, telBytes.constData(), _MAX_SIZE - 1);
    rq.tel[_MAX_SIZE - 1] = '\0';
    strncpy(rq.name, nameBytes.constData(), _MAX_SIZE - 1);
    rq.name[_MAX_SIZE - 1] = '\0';
    strncpy(rq.password, pwdBytes.constData(), _MAX_SIZE - 1);
    rq.password[_MAX_SIZE - 1] = '\0';
    m_mediator->SendData(0, (char*)&rq, sizeof(rq));
}

void LoginDialog::slot_readyData(unsigned int ip, const QByteArray& data)
{
    fprintf(stderr, "slot_readyData called, data size: %d\n", data.size());
    PackType type = *(PackType*)data.constData();
    fprintf(stderr, "Packet type: %d expected LOGIN_RS: %d REGISTER_RS: %d\n", type, _DEF_PACK_LOGIN_RS, _DEF_PACK_REGISTER_RS);

    if (type == _DEF_PACK_LOGIN_RS) {
        const STRU_LOGIN_RS* rs = (const STRU_LOGIN_RS*)data.constData();
        fprintf(stderr, "LOGIN_RS result=%d userid=%d role=%d token=%.32s\n", rs->result, rs->userid, rs->role, rs->token);
        if (rs->result == login_success) {
            QString token = QString::fromUtf8(rs->token);
            int userId = rs->userid;
            int role = rs->role;

            // 显示角色选择对话框
            fprintf(stderr, "Showing RoleSelectDialog, role=%d\n", role);
            RoleSelectDialog dlg(role, this);
            if (dlg.exec() == QDialog::Accepted) {
                int chosenRole = dlg.selectedRole();
                fprintf(stderr, "Role selected: %d\n", chosenRole);

                // 根据角色选择对应的可执行文件路径
                QString appPath;
                if (chosenRole == 1) {
                    appPath = QApplication::applicationDirPath()
                              + "/../../VideoRecoder/VideoRecoder/debug/VideoRecoder.exe";
                } else {
                    appPath = QApplication::applicationDirPath()
                              + "/../../videoplayer/MedieaPlayer/debug/MedieaPlayer.exe";
                }
                fprintf(stderr, "Launching: %s\n", appPath.toUtf8().constData());

                // 通过环境变量传递 token 和服务器信息（不暴露在命令行）
                QProcess proc;
                QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
                env.insert("PATH", QLibraryInfo::location(QLibraryInfo::BinariesPath)
                                  + ";" + env.value("PATH"));
                env.insert("LIVE_TOKEN", token);
                env.insert("LIVE_SERVER", m_serverIp);
                env.insert("LIVE_USERID", QString::number(userId));
                proc.setProcessEnvironment(env);
                proc.setProgram(appPath);
                bool started = proc.startDetached();
                fprintf(stderr, "startDetached result: %d\n", started);
                close();  // 关闭 Launcher
            } else {
                fprintf(stderr, "RoleSelectDialog rejected\n");
            }
        } else if (rs->result == password_error) {
            fprintf(stderr, "Login failed: password error\n");
            QMessageBox::warning(this, "登录失败", "密码错误");
        } else {
            fprintf(stderr, "Login failed: user not exist\n");
            QMessageBox::warning(this, "登录失败", "用户不存在");
        }
    }
    else if (type == _DEF_PACK_REGISTER_RS) {
        const STRU_REGISTER_RS* rs = (const STRU_REGISTER_RS*)data.constData();
        fprintf(stderr, "REGISTER_RS result=%d (success=%d exist=%d)\n", rs->result, register_success, user_is_exist);
        if (rs->result == register_success) {
            QMessageBox::information(this, "注册成功", "请登录");
        } else {
            QMessageBox::warning(this, "注册失败", "手机号已存在");
        }
    }
}

void LoginDialog::slot_disconnected()
{
    QMessageBox::warning(this, "连接断开", "与服务器断开连接");
    close();
}
