#include "recorderdialog.h"

#include <QApplication>
#include <QProcessEnvironment>
#include <QByteArray>
#include "TcpClientMediator.h"
#include "packdef.h"
#include "server_config.h"

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    qRegisterMetaType<QByteArray>("QByteArray");

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    QString token = env.value("LIVE_TOKEN", "");
    QString serverIp = env.value("LIVE_SERVER", SERVER_HOST);
    int userId = env.value("LIVE_USERID", "0").toInt();

    RecorderDialog w;
    w.show();

    if (!token.isEmpty()) {
        w.initNetwork(serverIp, token, userId);
    }

    return a.exec();
}
