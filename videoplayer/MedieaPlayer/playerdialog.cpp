#include "playerdialog.h"
#include "ui_playerdialog.h"
#include<QDebug>
#include<QThread>
#include<QMenu>
#include<QVBoxLayout>
#include<QPushButton>
#include<QHeaderView>
#include "server_config.h"
//#define _DEF_PATH "D:/KuGou/101.flv"

//点播路径（旧）
//#define _DEF_PATH "rtmp://192.168.136.135:1935/vod/101.mp4"

//直播测试（旧 - 外部 IPTV）
//#define _DEF_PATH "http://111.40.196.9/PLTV/88888888/224/3221225494/index.m3u8"

// VM nginx-rtmp 测试路径（IP 由 common/server_config.h 集中管理）
#define _DEF_PATH RTMP_VOD_URL "101"        // RTMP 点播
//#define _DEF_PATH "http://localhost/hls/CDAB435FC68FA359603BBE5ADC96AF5A/output.m3u8"  // HLS 点播

//直播路径
#define _DEF_PATH_LIVE RTMP_VIDEOTEST_URL "user=100"

PlayerDialog::PlayerDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::PlayerDialog)
{
    // 添加 Windows 最小化、最大化按钮，必须在 setupUi 之前设置避免影响 OpenGL
    this->setWindowFlags(this->windowFlags() | Qt::WindowMinMaxButtonsHint);
    ui->setupUi(this);

    m_player = new VideoPlayer;
    connect(m_player,SIGNAL(SIG_getOneImage(QImage)),this,SLOT(slot_setImage(QImage)));
    slot_PlayerStateChanged(PlayerState::Stop);
    //测试
    //m_player->setFileName( _DEF_PATH );
    //connect(&m_timer,SIGNAL(timeout()),this,SLOT());
    connect(m_player,SIGNAL(SIG_PlayerStateChanged(int)),this,SLOT(slot_PlayerStateChanged(int)));
    connect(m_player,SIGNAL(SIG_TotalTime(qint64)),this,SLOT(slot_getTotalTime(qint64)));
    connect(&m_timer,SIGNAL(timeout()),this,SLOT(slot_TimerTimeOut()));
    m_timer.setInterval(500); //超时时间 500ms
    ui->slider_progress->installEventFilter(this);//安装事件过滤器，让该对象成为被观察的的对象，this 去执行函数
    m_volume = 50; //初始音量 50%

    //初始化音量滑块
    ui->slider_volume->setRange(0, 100);
    ui->slider_volume->setValue(m_volume);
    ui->slider_volume->setFocusPolicy(Qt::NoFocus); //不接受键盘焦点
    ui->lb->setText(QString::number(m_volume) + "%");

    //设置进度条不接受键盘焦点，防止方向键被捕获
    ui->slider_progress->setFocusPolicy(Qt::NoFocus);
    //设置按钮不接受空格键快捷键
    ui->pb_start->setFocusPolicy(Qt::NoFocus);
    ui->pb_pause->setFocusPolicy(Qt::NoFocus);
    ui->pb_resume->setFocusPolicy(Qt::NoFocus);
    ui->pb_stop->setFocusPolicy(Qt::NoFocus);
    ui->pb_stream_service->setFocusPolicy(Qt::NoFocus);

    //设置对话框接受焦点
    this->setFocusPolicy(Qt::StrongFocus);
    this->setFocus();

    //初始化拖动标志
    m_isDraggingSlider = false;

    //初始化全屏标志
    m_isFullscreen = false;

    //初始化网络成员
    m_mediator = nullptr;
    m_serverIp = "";
    m_userId = 0;
    m_currentRoomId = 0;
    m_heartbeatTimer = nullptr;
}

PlayerDialog::~PlayerDialog()
{
    delete ui;
    delete m_player;
}

//Qt 线程
//Qthread 定义子类 start() -> run();

//打开文件
#include<QFileDialog>
void PlayerDialog::on_pb_start_clicked()
{
    //m_player->start();

    //首先，先关闭 判断当前状态 stop
    if(m_player->playerState() != PlayerState::Stop){
        m_player->stop(true);
    }
    QThread::msleep(50);  // ensure read thread has fully exited
//    //打开浏览选择文件
//    QString path = QFileDialog::getOpenFileName(this, "打开文件" , "./" , "视频文件 (*.flv *.rmvb *.avi *.MP4 *.mkv);; 所有文件(*.*);;");
//    //判断
//    if(path.isEmpty()) return;
//    //设置 m_play fileName
//    m_player->setFileName( path );
      //点播
      m_player->setFileName(_DEF_PATH);
      //直播
      //m_player->setFileName(_DEF_PATH_LIVE);

    //play
    m_player->start();
    slot_PlayerStateChanged(PlayerState::Playing);
}

void PlayerDialog::slot_setImage(QImage img)
{   //pixmap image

    //实现视频加速渲染 OpenGL
    ui->wdg_show->slot_setImage(img);
}


void PlayerDialog::on_pb_resume_clicked()
{
    if(m_player->playerState()!= PlayerState::Pause) return;
    m_player->play();
    //切换
    ui->pb_resume->hide();
    ui->pb_pause->show();
}


void PlayerDialog::on_pb_pause_clicked()
{
    if(m_player->playerState()!= PlayerState::Playing) return;
    m_player->pause();
    //切换
    ui->pb_resume->show();
    ui->pb_pause->hide();
}


void PlayerDialog::on_pb_stop_clicked()
{
    m_player->stop(true);
}

//播放状态切换槽函数
void PlayerDialog::slot_PlayerStateChanged(int state)
{
    switch( state )
    {
    case PlayerState::Stop:
        qDebug()<< "VideoPlayer::Stop";
        m_timer.stop();
        ui->slider_progress->setValue(0);
        ui->lb_totalTime->setText("00:00:00");
        ui->lb_curTime->setText("00:00:00");
        ui->pb_pause->hide();
        ui->pb_resume->show();
    {
        QImage img;
        img.fill( Qt::black);
        slot_setImage( img );
    }
        this->update();
        isStop = true;
        break;
    case PlayerState::Playing:
        qDebug()<< "VideoPlayer::Playing";
        ui->pb_resume->hide();
        ui->pb_pause->show();
        m_timer.start();
        this->update();
        isStop = false;
        break;
    }
}

//获取视频全部时间
void PlayerDialog::slot_getTotalTime(qint64 uSec)
{
    // 直播流总时间为 0，显示"直播"
    if (uSec <= 0) {
        ui->lb_totalTime->setText("直播");
        ui->slider_progress->setRange(0, 0);
        return;
    }

    qint64 Sec = uSec/1000000;
    ui->slider_progress->setRange(0,Sec);//精确到秒
    QString hStr = QString("00%1").arg(Sec/3600);
    QString mStr = QString("00%1").arg(Sec/60);
    QString sStr = QString("00%1").arg(Sec%60);
    QString str =
            QString("%1:%2:%3").arg(hStr.right(2)).arg(mStr.right(2)).arg(sStr.right(2));
    ui->lb_totalTime->setText(str);
}

//获取当前视频时间定时器
void PlayerDialog::slot_TimerTimeOut()
{
    if (QObject::sender() == &m_timer)
    {
        // 获取当前时间（只获取一次）
        qint64 Sec = m_player->getCurrentTime()/1000000;

        // 如果正在拖动进度条，不更新进度条位置，避免闪回
        if (!m_isDraggingSlider) {
            ui->slider_progress->setValue(Sec);
        }

        // 更新时间显示
        QString hStr = QString("00%1").arg(Sec/3600);
        QString mStr = QString("00%1").arg(Sec/60%60);
        QString sStr = QString("00%1").arg(Sec%60);
        QString str =
                QString("%1:%2:%3").arg(hStr.right(2)).arg(mStr.right(2)).arg(sStr.right(2));
        ui->lb_curTime->setText(str);

        // 检查播放结束
        if(ui->slider_progress->value() == ui->slider_progress->maximum()
                && m_player->playerState() == PlayerState::Stop)
        {
            slot_PlayerStateChanged( PlayerState::Stop );
        }else if(ui->slider_progress->value() + 1  ==
                 ui->slider_progress->maximum()
                 && m_player->playerState() == PlayerState::Stop)
        {
            slot_PlayerStateChanged( PlayerState::Stop );
        }
    }
}

//音量滑块值改变槽函数
void PlayerDialog::on_slider_volume_valueChanged(int value)
{
    m_volume = value;
    m_player->setVolume(m_volume);
    ui->lb->setText(QString::number(m_volume) + "%");
}

//URL 设置按钮槽函数
#include <QInputDialog>
#include <QMessageBox>
#include <QFile>
void PlayerDialog::on_pb_url_clicked()
{
    bool ok;
    QString url = QInputDialog::getText(this,
                                        tr("设置"),
                                        tr("请输入链接地址:"),
                                        QLineEdit::Normal,
                                        QString(),
                                        &ok);
    if (ok && !url.isEmpty()) {
        // 本地路径：标准化斜杠方向，方便 FFmpeg 打开
        if (!url.contains("://")) {
            url.replace('\\', '/');
            if (!QFile::exists(url)) {
                QMessageBox::warning(this, tr("错误"),
                    tr("文件不存在:\n%1").arg(url));
                return;
            }
        }

        // 停止当前播放
        if(m_player->playerState() != PlayerState::Stop){
            m_player->stop(true);
        }
        QThread::msleep(50);  // ensure read thread has fully exited
        // 设置 URL 并播放
        m_player->setFileName(url);
        m_player->start();
        slot_PlayerStateChanged(PlayerState::Playing);
    }
}

//推流服务下拉菜单
void PlayerDialog::on_pb_stream_service_clicked()
{
    QMenu menu(this);
    QAction *actLive = menu.addAction("RTMP 直播推流");
    QAction *actVod  = menu.addAction("RTMP 点播");
    QAction *actHls  = menu.addAction("HLS 点播");

    QAction *chosen = menu.exec(ui->pb_stream_service->mapToGlobal(
        QPoint(0, ui->pb_stream_service->height())));

    if (!chosen) return;

    QString url;
    bool ok = false;
    const QString server = SERVER_HOST;

    if (chosen == actLive) {
        QString key = QInputDialog::getText(this, "RTMP 直播推流",
            QString("推流服务器: rtmp://%1:1935/videotest/\n请输入推流码:").arg(server),
            QLineEdit::Normal, "user=100", &ok);
        if (ok && !key.isEmpty())
            url = QString("rtmp://%1:1935/videotest/%2").arg(server, key);
    }
    else if (chosen == actVod) {
        QString name = QInputDialog::getText(this, "RTMP 点播",
            QString("点播服务器: rtmp://%1:1935/vod/\n请输入文件名:").arg(server),
            QLineEdit::Normal, "101", &ok);
        if (ok && !name.isEmpty())
            url = QString("rtmp://%1:1935/vod/%2").arg(server, name);
    }
    else if (chosen == actHls) {
        url = QInputDialog::getText(this, "HLS 点播",
            QString("HLS 服务器: http://%1/hls/\n请输入完整 HLS 地址（含 .m3u8）:").arg(server),
            QLineEdit::Normal,
            QString("http://%1/hls/CDAB435FC68FA359603BBE5ADC96AF5A/output.m3u8").arg(server),
            &ok);
        if (!ok || url.isEmpty()) return;
    }

    if (url.isEmpty()) return;

    //停止当前播放
    if (m_player->playerState() != PlayerState::Stop)
        m_player->stop(true);

    m_player->setFileName(url);
    m_player->start();
    slot_PlayerStateChanged(PlayerState::Playing);
}

#include<QStyle>
#include<QMouseEvent>
#include<QKeyEvent>

//键盘事件处理
void PlayerDialog::keyPressEvent(QKeyEvent *event)
{
    switch(event->key()){
    case Qt::Key_Space:
        //空格键暂停/播放
        event->accept(); //接受事件，阻止传递给按钮
        if(m_player->playerState() == PlayerState::Playing){
            on_pb_pause_clicked();
        }else if(m_player->playerState() == PlayerState::Pause){
            on_pb_resume_clicked();
        }
        break;
    case Qt::Key_Left:
        //左箭头键快退 10 秒
        event->accept();
        {
            qint64 curTime = m_player->getCurrentTime();
            // 如果当前时间小于1秒，直接重新播放
            if (curTime < 1000000) {
                m_player->stop(true);
                QThread::msleep(100); // 等待一下
                m_player->start();
            } else {
                qint64 newPos = curTime - 10000000; //10 秒，单位微秒
                if(newPos < 0) newPos = 0;
                m_player->seek(newPos);
                qDebug() << "Seek to:" << newPos / 1000000 << "seconds";
            }
        }
        break;
    case Qt::Key_Right:
        //右箭头键快进 10 秒
        event->accept();
        {
            qint64 curTime = m_player->getCurrentTime();
            qint64 totalTime = m_player->getTotalTime();
            qint64 newPos = curTime + 10000000; //10 秒，单位微秒
            if(totalTime > 0 && newPos > totalTime) newPos = totalTime;
            m_player->seek(newPos);
            qDebug() << "Seek to:" << newPos / 1000000 << "seconds";
        }
        break;
    case Qt::Key_Up:
        //上箭头键增加音量
        event->accept();
        m_volume += 5;
        if(m_volume > 100) m_volume = 100;
        ui->slider_volume->setValue(m_volume); //通过滑块更新，会触发槽函数
        break;
    case Qt::Key_Down:
        //下箭头键降低音量
        event->accept();
        m_volume -= 5;
        if(m_volume < 0) m_volume = 0;
        ui->slider_volume->setValue(m_volume); //通过滑块更新，会触发槽函数
        break;
    case Qt::Key_Escape:
        //Esc 退出全屏
        if (m_isFullscreen) {
            event->accept();
            on_pb_fullscreen_clicked();
            return;
        }
        QDialog::keyPressEvent(event);
        break;
    case Qt::Key_F:
        //F 键切换全屏
        event->accept();
        on_pb_fullscreen_clicked();
        break;
    default:
        QDialog::keyPressEvent(event);
        break;
    }
}

//窗口状态改变 — 最大化按钮 → 全屏
void PlayerDialog::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::WindowStateChange) {
        if (this->windowState() & Qt::WindowMaximized) {
            // 点击最大化 → 进入全屏
            this->setWindowState(this->windowState() & ~Qt::WindowMaximized);
            if (!m_isFullscreen) {
                on_pb_fullscreen_clicked();
            }
        }
    }
    QDialog::changeEvent(event);
}

bool PlayerDialog::eventFilter(QObject *obj, QEvent *event)
{
    if(obj == ui->slider_progress){
        if(event->type()== QEvent::MouseButtonPress){
            QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
            if(mouseEvent->button() == Qt::LeftButton){
                m_isDraggingSlider = true; //开始拖动
                int min = ui->slider_progress->minimum();
                int max = ui->slider_progress->maximum();
                int value = QStyle::sliderValueFromPosition(
                min, max, mouseEvent->pos().x(), ui->slider_progress->width());
                ui->slider_progress->setValue(value);
                ui->slider_progress->grabMouse(); //捕获鼠标，即使移出控件也能跟踪
                return true;
            }
        }
        else if(event->type() == QEvent::MouseMove){
            QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
            if(mouseEvent->buttons() & Qt::LeftButton){
                int min = ui->slider_progress->minimum();
                int max = ui->slider_progress->maximum();
                int x = mouseEvent->pos().x();
                //限制 x 在有效范围内
                if(x < 0) x = 0;
                if(x > ui->slider_progress->width()) x = ui->slider_progress->width();
                int value = QStyle::sliderValueFromPosition(
                min, max, x, ui->slider_progress->width());
                ui->slider_progress->setValue(value);
                return true;
            }
        }
        else if(event->type() == QEvent::MouseButtonRelease){
            QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
            if(mouseEvent->button() == Qt::LeftButton){
                ui->slider_progress->releaseMouse(); //释放鼠标捕获
                m_isDraggingSlider = false; //先结束拖动标志，再seek
                qint64 pos = ui->slider_progress->value();
                m_player->seek(pos * 1000000);  //pos 秒，转换为微秒
                return true;
            }
        }
        else if(event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease){
            //拦截进度条的键盘事件，传递给对话框处理
            QKeyEvent *keyEvent = static_cast<QKeyEvent*>(event);
            this->keyPressEvent(keyEvent);
            return true;
        }
        else{
            return false;
        }
    }else{
        //pass the event on to the parent class
        return QDialog::eventFilter(obj,event);
    }
}

void PlayerDialog::initNetwork(const QString& serverIp, const QString& token, int userId)
{
    m_serverIp = serverIp;
    m_userId = userId;

    m_mediator = new TcpClientMediator;
    connect(m_mediator, SIGNAL(SIG_ReadyData(uint,QByteArray)),
            this, SLOT(slot_readyData(uint,QByteArray)), Qt::QueuedConnection);
    connect(m_mediator, SIGNAL(SIG_disConnect()),
            this, SLOT(slot_disconnected()), Qt::QueuedConnection);
    m_mediator->OpenNet(serverIp.toUtf8().constData(), 8000);

    STRU_AUTH_TOKEN_RQ rq;
    QByteArray tokenBytes = token.toUtf8();
    strncpy(rq.token, tokenBytes.constData(), sizeof(rq.token) - 1);
    rq.token[sizeof(rq.token) - 1] = '\0';
    m_mediator->SendData(0, (char*)&rq, sizeof(rq));

    m_heartbeatTimer = new QTimer(this);
    connect(m_heartbeatTimer, &QTimer::timeout, this, &PlayerDialog::slot_sendHeartbeat);
}

void PlayerDialog::requestRoomList()
{
    STRU_ROOM_LIST_RQ rq;
    if (m_mediator) m_mediator->SendData(0, (char*)&rq, sizeof(rq));
}

void PlayerDialog::slot_readyData(unsigned int ip, const QByteArray& data)
{
    PackType type = *(PackType*)data.constData();

    if (type == _DEF_PACK_LOGIN_RS) {
        // Token 验证响应
        const STRU_LOGIN_RS* rs = (const STRU_LOGIN_RS*)data.constData();
        if (rs->result == login_success) {
            m_userId = rs->userid;
            m_heartbeatTimer->start(10000);
            requestRoomList();  // 验证成功后自动拉取房间列表
        }
    }
    else if (type == _DEF_PACK_ROOM_LIST_RS) {
        const STRU_ROOM_LIST_RS* rs = (const STRU_ROOM_LIST_RS*)data.constData();
        QByteArray jsonData(rs->content, rs->json_length);
        QJsonDocument doc = QJsonDocument::fromJson(jsonData);
        QJsonArray arr = doc.array();

        showRoomListDialog(arr);  // 调用弹窗展示列表
    }
}

void PlayerDialog::showRoomListDialog(const QJsonArray& rooms)
{
    QDialog dlg(this);
    dlg.setWindowTitle("Live Rooms");
    dlg.resize(500, 400);

    QVBoxLayout* layout = new QVBoxLayout(&dlg);
    QTableWidget* table = new QTableWidget(&dlg);
    table->setColumnCount(2);
    table->setHorizontalHeaderLabels({"Room", "Viewers"});
    table->horizontalHeader()->setStretchLastSection(false);
    table->setColumnWidth(0, 380);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);

    for (int i = 0; i < rooms.size(); i++) {
        QJsonObject room = rooms[i].toObject();
        table->insertRow(i);
        QString title = room["title"].toString();
        QString anchor = room["anchor"].toString();
        int viewers = room["viewer_count"].toInt();
        int roomId = room["id"].toInt();
        QString streamKey = room["stream_key"].toString();

        QTableWidgetItem* titleItem = new QTableWidgetItem(
            QString("%1 (by %2)").arg(title, anchor));
        // 把 stream_key 隐藏在 UserRole，room_id 在 UserRole+1
        titleItem->setData(Qt::UserRole, streamKey);
        titleItem->setData(Qt::UserRole + 1, roomId);
        table->setItem(i, 0, titleItem);
        table->setItem(i, 1, new QTableWidgetItem(QString::number(viewers)));
    }

    layout->addWidget(table);

    QPushButton* btnRefresh = new QPushButton("Refresh", &dlg);
    layout->addWidget(btnRefresh);
    connect(btnRefresh, &QPushButton::clicked, [this]() {
        requestRoomList();
    });

    connect(table, &QTableWidget::cellDoubleClicked, [&](int row, int) {
        QString streamKey = table->item(row, 0)->data(Qt::UserRole).toString();
        int roomId = table->item(row, 0)->data(Qt::UserRole + 1).toInt();
        dlg.accept();
        playRoom(streamKey, roomId);
    });

    dlg.exec();
}

void PlayerDialog::playRoom(const QString& streamKey, int roomId)
{
    // 离开之前的房间
    if (m_currentRoomId > 0) {
        STRU_LEAVE_ROOM_RQ lrq;
        lrq.userid = m_userId;
        lrq.room_id = m_currentRoomId;
        if (m_mediator) m_mediator->SendData(0, (char*)&lrq, sizeof(lrq));
    }

    m_currentRoomId = roomId;

    // 停止当前播放
    if (m_player->playerState() != PlayerState::Stop) {
        m_player->stop(true);
        QThread::msleep(50);
    }

    // 用 stream_key 构造正确的 RTMP URL
    QString rtmpUrl = QString("rtmp://%1:1935/live/%2").arg(m_serverIp, streamKey);

    m_player->setFileName(rtmpUrl);
    m_player->start();
    slot_PlayerStateChanged(PlayerState::Playing);

    // 通知服务器进房
    STRU_JOIN_ROOM_RQ jrq;
    jrq.userid = m_userId;
    jrq.room_id = roomId;
    if (m_mediator) m_mediator->SendData(0, (char*)&jrq, sizeof(jrq));
}

void PlayerDialog::on_pb_roomList_clicked()
{
    requestRoomList();
}

void PlayerDialog::slot_sendHeartbeat()
{
    STRU_HEARTBEAT_RQ rq;
    rq.userid = m_userId;
    if (m_mediator) m_mediator->SendData(0, (char*)&rq, sizeof(rq));
}

void PlayerDialog::on_pb_fullscreen_clicked()
{
    if (m_isFullscreen) {
        // 退出全屏
        this->showNormal();
        ui->wdg_controls->show();
        m_isFullscreen = false;
    } else {
        // 进入全屏
        this->showFullScreen();
        ui->wdg_controls->hide();
        m_isFullscreen = true;
    }
}

void PlayerDialog::slot_disconnected()
{
    QMessageBox::warning(this, "disconnected", "Server connection lost");
    if (m_heartbeatTimer) m_heartbeatTimer->stop();
}
