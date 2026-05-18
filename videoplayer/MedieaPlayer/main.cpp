#include "playerdialog.h"

#include <QApplication>
#include <QProcessEnvironment>
#include <QByteArray>
#include <iostream>
#include "server_config.h"

using namespace std;

extern "C"
{
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"
#include "libavdevice/avdevice.h"
}

#undef main
int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    //这里简单的输出一个版本号
    cout << "Hello FFmpeg!" << endl;
    av_register_all();
    unsigned version = avcodec_version();
    cout << "version is:" << version << endl;;

    qRegisterMetaType<QByteArray>("QByteArray");

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    QString token = env.value("LIVE_TOKEN", "");
    QString serverIp = env.value("LIVE_SERVER", SERVER_HOST);
    int userId = env.value("LIVE_USERID", "0").toInt();

    PlayerDialog w;
    w.show();

    if (!token.isEmpty()) {
        w.initNetwork(serverIp, token, userId);
    }

    return a.exec();
}
