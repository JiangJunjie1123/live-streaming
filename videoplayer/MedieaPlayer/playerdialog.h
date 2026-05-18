#ifndef PLAYERDIALOG_H
#define PLAYERDIALOG_H

#include <QDialog>
#include"videoplayer.h"
#include<QTimer>
#include "myopenglwidget.h"
#include "TcpClientMediator.h"
#include "packdef.h"
#include <QTableWidget>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QMessageBox>

QT_BEGIN_NAMESPACE
namespace Ui { class PlayerDialog; }
QT_END_NAMESPACE



class PlayerDialog : public QDialog
{
    Q_OBJECT

public:
    PlayerDialog(QWidget *parent = nullptr);
    ~PlayerDialog();

    void initNetwork(const QString& serverIp, const QString& token, int userId);
    void requestRoomList();
    void setUserInfo(const QString& name, const QString& tel);

private slots:
    void on_pb_start_clicked();
    void slot_setImage(QImage img);

    void on_pb_resume_clicked();

    void on_pb_pause_clicked();

    void on_pb_stop_clicked();

    void slot_PlayerStateChanged(int state);

    void slot_getTotalTime(qint64 uSec);
    void slot_TimerTimeOut();

    //音量滑块槽函数
    void on_slider_volume_valueChanged(int value);

    //自定义服务按钮槽函数
    void on_pb_custom_clicked();
    //网络直播按钮槽函数
    void on_pb_network_live_clicked();

    //全屏切换槽函数
    void on_pb_fullscreen_clicked();

    //网络相关槽函数
    void slot_readyData(unsigned int ip, const QByteArray& data);
    void slot_disconnected();
    void on_pb_roomList_clicked();
    void slot_sendHeartbeat();

    //事件过滤器
    bool eventFilter(QObject * obj, QEvent * event);//虚函数

protected:
    //键盘事件
    void keyPressEvent(QKeyEvent *event) override;
    //窗口状态改变（最大化按钮 → 全屏）
    void changeEvent(QEvent *event) override;

private:
    Ui::PlayerDialog *ui;
    VideoPlayer * m_player;
    QTimer m_timer;
    //停止的状态
    bool isStop;
    //音量 0-100
    int m_volume;
    //是否正在拖动进度条
    bool m_isDraggingSlider;

    //是否全屏
    bool m_isFullscreen;

    //网络相关成员
    TcpClientMediator* m_mediator;
    QString m_serverIp;
    int m_userId;
    int m_currentRoomId;
    QTimer* m_heartbeatTimer;

    QString m_userName;
    QString m_userTel;

    //网络相关私有方法
    void showRoomListDialog(const QJsonArray& rooms);
    void playRoom(const QString& streamKey, int roomId);
    void showNetworkLiveDialog();
};
#endif // PLAYERDIALOG_H
