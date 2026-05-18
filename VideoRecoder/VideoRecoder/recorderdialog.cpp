#include "recorderdialog.h"
#include "ui_recorderdialog.h"
#include "picinpic_read.h"
#include <QMessageBox>
#include <QPushButton>
#include <QCheckBox>
#include <QCloseEvent>
#include <QProcessEnvironment>
#include "server_config.h"

RecorderDialog::RecorderDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::RecorderDialog)
{
    ui->setupUi(this);

    m_pictureWidget = new PictureWidget;
    m_pictureWidget->hide();
    m_pictureWidget->move(0,0);

    this->setWindowFlags(Qt::WindowMinMaxButtonsHint|Qt::WindowCloseButtonHint);

    m_picInPicRead = new PicInPic_Read;
    m_saveFileThread = new SaveVideoFileThread;
    
    // 连接画中画信号到显示窗口
    connect(m_picInPicRead,SIGNAL(SIG_sendPicInPic(QImage)),m_pictureWidget,SLOT(slot_setImage(QImage)));
    // 连接视频帧到主界面显示
    connect(m_picInPicRead,SIGNAL(SIG_sendVideoFrame(QImage)),this,SLOT(slot_setImage(QImage)));
    // 连接视频帧到编码线程（RGB→YUV 在 run() 线程中完成）
    connect(m_picInPicRead, SIGNAL(SIG_sendVideoFrame(QImage)),
            m_saveFileThread, SLOT(slot_queueVideoFrame(QImage)));

    // 连接编码错误信号
    connect(m_saveFileThread, SIGNAL(SIG_EncodeError(QString)),
            this, SLOT(slot_EncodeError(QString)));

    // 默认 RTMP 地址 — "Go Live" 按钮会通过服务器分配 stream key 后自动更新此 URL
    ui->le_url->setText(RTMP_LIVE_URL);
    m_saveUrl = ui->le_url->text();

    // 初始化网络相关成员
    m_mediator = nullptr;
    m_userId = 0;
    m_roomId = 0;
    m_isStreaming = false;
    m_heartbeatTimer = nullptr;

    // 创建 "Go Live" 按钮（不存在于 .ui 文件中）
    m_pbStartStreamBtn = new QPushButton(QString::fromUtf8("Go Live"), ui->wdg_content);
    m_pbStartStreamBtn->setEnabled(false);
    connect(m_pbStartStreamBtn, SIGNAL(clicked()),
            this, SLOT(on_pb_startStream_clicked()));

    // "系统音频" 复选框 — 代码创建，不修改 .ui
    QCheckBox* cbSysAudio = new QCheckBox(QString::fromUtf8("系统音频"), ui->wdg_content);
    cbSysAudio->setChecked(false);  // 默认关闭，避免同机测试闭环
    QVBoxLayout* vLayout = qobject_cast<QVBoxLayout*>(ui->wdg_content->layout());
    if (vLayout) {
        // 插入到 pb_start 之前（spacer 之后）
        int idx = vLayout->indexOf(ui->pb_start);
        vLayout->insertWidget(idx, cbSysAudio);
        vLayout->addWidget(m_pbStartStreamBtn);
    }
    cbSysAudio->setObjectName("cb_sysAudio");
}

RecorderDialog::~RecorderDialog()
{
    if (m_heartbeatTimer) {
        m_heartbeatTimer->stop();
    }
    if (m_mediator) {
        m_mediator->CloseNet();
        delete m_mediator;
        m_mediator = nullptr;
    }
    delete ui;
    delete m_pictureWidget;
    delete m_saveFileThread;
    delete m_picInPicRead;
}

void RecorderDialog::closeEvent(QCloseEvent *event)
{
    m_pictureWidget->hide();
    on_pb_stop_clicked();
    event->accept();
}


void RecorderDialog::on_pb_start_clicked()
{
    this->showMinimized();
    m_pictureWidget->show();

    STRU_AV_FORMAT format;
    format.fileName = m_saveUrl;
    format.frame_rate = FRAME_RATE;
    // 根据复选框状态决定是否录制音频
    format.hasAudio = ui->cb_mic->isChecked();                  // 麦克风
    format.hasSysAudio = ui->wdg_content->findChild<QCheckBox*>("cb_sysAudio")->isChecked();
    format.hasCamera = ui->cb_camera->isChecked();
    format.hasDesk = ui->cb_desktop->isChecked();
    format.videoBitRate = 8000000;  // 8 Mbps — 提升清晰度
    QScreen *src = QApplication::primaryScreen();
    QRect rect = src->geometry();
    int srcW = rect.width();
    int srcH = rect.height();
    // 缩放到 720p 以内 (与 picinpic_read 保持同步)
    if (srcW > 1280 || srcH > 720) {
        double scale = qMin(1280.0 / srcW, 720.0 / srcH);
        srcW = qRound(srcW * scale);
        srcH = qRound(srcH * scale);
        // H.264 YUV420P 要求宽高为偶数
        srcW &= ~1;
        srcH &= ~1;
    }
    format.width = srcW;
    format.height = srcH;

    m_saveFileThread->slot_setInfo( format );

    // 启动视频采集
    if (format.hasCamera || format.hasDesk) {
        m_picInPicRead->setTargetSize(srcW, srcH);  // 与编码器分辨率一致
        m_picInPicRead->slot_openVideo();
    }

    // 启动音频采集（麦克风 或 系统声音任意勾选即启动）
    if (format.hasAudio) {
        m_saveFileThread->slot_openAudio();
    }

    // 启动编码线程（FFmpeg 初始化 + avio_open2 RTMP 连接在此线程中执行）
    m_saveFileThread->start();
}


void RecorderDialog::on_pb_stop_clicked()
{
    m_pictureWidget->hide();
    m_picInPicRead->slot_closeVideo();
    m_saveFileThread->slot_closeVideo();
}


void RecorderDialog::on_pb_setUrl_clicked()
{
    m_saveUrl = ui->le_url->text();
    // Do NOT replace / with \ — breaks RTMP URLs (rtmp://server/app/stream)
    // FFmpeg handles both forward and backslashes on Windows
}

void RecorderDialog::slot_EncodeError(const QString &msg)
{
    QMessageBox::warning(this, QString::fromUtf8("编码/推流错误"), msg);
}

void RecorderDialog::slot_setImage(QImage img)
{
    QPixmap pixmap;
    if(!img.isNull()){
        pixmap = QPixmap::fromImage(img.scaled(ui->lb_showImage->size(),Qt::KeepAspectRatio));
    }else{
        pixmap = QPixmap::fromImage(img);
    }
    ui->lb_showImage->setPixmap(pixmap);
}

// ==================== 网络相关方法 ====================

void RecorderDialog::initNetwork(const QString& serverIp, const QString& token, int userId)
{
    m_token = token;
    m_userId = userId;

    m_mediator = new TcpClientMediator;
    connect(m_mediator, SIGNAL(SIG_ReadyData(uint,QByteArray)),
            this, SLOT(slot_readyData(uint,QByteArray)), Qt::QueuedConnection);
    connect(m_mediator, SIGNAL(SIG_disConnect()),
            this, SLOT(slot_disconnected()), Qt::QueuedConnection);
    m_mediator->OpenNet(serverIp.toUtf8().constData(), 8000);

    // 发送 Token 验证
    STRU_AUTH_TOKEN_RQ rq;
    QByteArray tokenBytes = token.toUtf8();
    strncpy(rq.token, tokenBytes.constData(), sizeof(rq.token) - 1);
    rq.token[sizeof(rq.token) - 1] = '\0';
    m_mediator->SendData(0, (char*)&rq, sizeof(rq));

    m_heartbeatTimer = new QTimer(this);
    connect(m_heartbeatTimer, &QTimer::timeout, this, &RecorderDialog::slot_sendHeartbeat);
}

void RecorderDialog::slot_readyData(unsigned int ip, const QByteArray& data)
{
    Q_UNUSED(ip);

    PackType type = *(PackType*)data.constData();

    if (type == _DEF_PACK_LOGIN_RS) {
        // Token 验证响应
        const STRU_LOGIN_RS* rs = (const STRU_LOGIN_RS*)data.constData();
        if (rs->result == login_success) {
            m_userId = rs->userid;
            m_heartbeatTimer->start(10000);  // 每10秒心跳
            if (m_pbStartStreamBtn) {
                m_pbStartStreamBtn->setEnabled(true);
            }
        } else {
            QMessageBox::warning(this, "Auth failed", "Token validation failed");
        }
    }
    else if (type == _DEF_PACK_CREATE_ROOM_RS) {
        const STRU_CREATE_ROOM_RS* rs = (const STRU_CREATE_ROOM_RS*)data.constData();
        if (rs->result == create_room_success) {
            m_roomId = rs->room_id;
            m_streamKey = QString::fromUtf8(rs->stream_key);
            // 构造 RTMP URL
            QString rtmpUrl = QString(RTMP_LIVE_URL "%1").arg(m_streamKey);
            m_saveUrl = rtmpUrl;
            ui->le_url->setText(rtmpUrl);
            m_isStreaming = true;
            on_pb_start_clicked();  // 开始录制+推流
            if (m_pbStartStreamBtn) {
                m_pbStartStreamBtn->setText(QString::fromUtf8("Stop Live"));
            }
        } else {
            QMessageBox::warning(this, "Failed", "Failed to create room (may already have an active room)");
        }
    }
    else if (type == _DEF_PACK_CLOSE_ROOM_RS) {
        m_isStreaming = false;
        m_roomId = 0;
        if (m_pbStartStreamBtn) {
            m_pbStartStreamBtn->setText(QString::fromUtf8("Go Live"));
        }
    }
}

void RecorderDialog::on_pb_startStream_clicked()
{
    if (m_isStreaming) {
        // 下播
        on_pb_stop_clicked();  // 停止录制
        STRU_CLOSE_ROOM_RQ rq;
        rq.userid = m_userId;
        rq.room_id = m_roomId;
        if (m_mediator) m_mediator->SendData(0, (char*)&rq, sizeof(rq));
    } else {
        // 开播
        STRU_CREATE_ROOM_RQ rq;
        rq.userid = m_userId;
        strncpy(rq.title, "live stream", sizeof(rq.title) - 1);
        rq.title[sizeof(rq.title) - 1] = '\0';
        if (m_mediator) m_mediator->SendData(0, (char*)&rq, sizeof(rq));
    }
}

void RecorderDialog::slot_sendHeartbeat()
{
    STRU_HEARTBEAT_RQ rq;
    rq.userid = m_userId;
    if (m_mediator) m_mediator->SendData(0, (char*)&rq, sizeof(rq));
}

void RecorderDialog::slot_disconnected()
{
    QMessageBox::warning(this, "disconnected", "Server connection lost");
    if (m_heartbeatTimer) m_heartbeatTimer->stop();
    m_isStreaming = false;
}
