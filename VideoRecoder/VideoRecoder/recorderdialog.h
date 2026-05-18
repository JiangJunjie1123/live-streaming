#ifndef RECORDERDIALOG_H
#define RECORDERDIALOG_H

#include <QDialog>
#include <QTimer>
#include "picturewidget.h"
#include "savevideofilethread.h"
#include "TcpClientMediator.h"
#include "packdef.h"

class PicInPic_Read;
class QPushButton;

QT_BEGIN_NAMESPACE
namespace Ui { class RecorderDialog; }
QT_END_NAMESPACE

class RecorderDialog : public QDialog
{
    Q_OBJECT

public:
    RecorderDialog(QWidget *parent = nullptr);
    ~RecorderDialog();

    void initNetwork(const QString& serverIp, const QString& token, int userId);

private slots:
    void on_pb_start_clicked();

    void on_pb_stop_clicked();

    void on_pb_setUrl_clicked();

    void slot_setImage(QImage img);
    void slot_EncodeError(const QString &msg);

    void slot_readyData(unsigned int ip, const QByteArray& data);
    void slot_disconnected();
    void on_pb_startStream_clicked();
    void slot_sendHeartbeat();

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    Ui::RecorderDialog *ui;
    PictureWidget * m_pictureWidget;
    SaveVideoFileThread * m_saveFileThread;
    PicInPic_Read * m_picInPicRead;
    QString m_saveUrl;

    TcpClientMediator* m_mediator;
    QString m_token;
    int m_userId;
    int m_roomId;
    QString m_streamKey;
    bool m_isStreaming;
    QTimer* m_heartbeatTimer;
    QPushButton* m_pbStartStreamBtn;
};
#endif // RECORDERDIALOG_H
