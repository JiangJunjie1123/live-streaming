#ifndef PICINPIC_READ_H
#define PICINPIC_READ_H

#include <QObject>
#include <QImage>
#include <QTimer>
#include <QMessageBox>
#include <QScreen>
#include <QApplication>
#include <QDesktopWidget>
#include <QPixmap>

// OpenCV 头文件
#include <opencv2/core/core.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/videoio/videoio.hpp>

using namespace cv;

extern "C" {
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"
#include "libavutil/imgutils.h"
}

#define FRAME_RATE 25

class PicInPic_Read : public QObject
{
    Q_OBJECT
public:
    explicit PicInPic_Read(QObject *parent = nullptr);

signals:
    void SIG_sendPicInPic(QImage image);
    void SIG_sendVideoFrameData(uint8_t *buffer, int buffer_size);
    void SIG_sendVideoFrame(QImage image);

public:
    void setTargetSize(int w, int h) { m_targetW = w; m_targetH = h; }

public slots:
    void slot_openVideo();
    void slot_closeVideo();
    void slot_getVideoFrame();

private:
    QTimer *timer;
    VideoCapture cap;
    int m_targetW = 0;
    int m_targetH = 0;
    int ImageToYuvBuffer(QImage& image, uint8_t **buffer);
};

#endif // PICINPIC_READ_H
