#include <QApplication>
#include "logindialog.h"

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    // 注册 QByteArray 元类型（跨线程 QueuedConnection 需要）
    qRegisterMetaType<QByteArray>("QByteArray");

    LoginDialog w;
    w.show();

    return a.exec();
}
