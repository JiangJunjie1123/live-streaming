#include "picinpic_read.h"
#include <QDebug>

PicInPic_Read::PicInPic_Read(QObject  *parent): QObject(parent)
{
    timer = new QTimer(this);
    connect( timer , SIGNAL(timeout()) , this , SLOT(slot_getVideoFrame()));
}
void PicInPic_Read::slot_openVideo()
{
    cap.open(0);
    // 摄像头不可用不是致命错误——桌面采集仍然可以继续
    if(!cap.isOpened()){
        qDebug() << "Camera not available, continuing with desktop capture only";
    }
    // 始终启动定时器，确保桌面采集不受摄像头影响
    timer->start(1000 / FRAME_RATE);
}
void PicInPic_Read::slot_closeVideo()
{
    timer->stop();

    if(cap.isOpened())
        cap.release();
}


void PicInPic_Read::slot_getVideoFrame()
{
    // 摄像头采集（可选，失败不影响桌面采集）
    if (cap.isOpened()) {
        Mat frame;
        if (cap.read(frame)) {
            cvtColor(frame, frame, cv::COLOR_BGR2RGB);
            QImage iconImage((unsigned const char*)frame.data,
                             frame.cols, frame.rows, QImage::Format_RGB888);
            iconImage = iconImage.scaled(QSize(320, 240), Qt::KeepAspectRatio);
            Q_EMIT SIG_sendPicInPic(iconImage);
        }
    }

    // 获取桌面截图（始终执行）
    QScreen *src = QApplication::primaryScreen();
    QPixmap map = src->grabWindow(QApplication::desktop()->winId());
    QImage image = map.toImage().convertToFormat(QImage::Format_RGB888);

    // 缩放到目标分辨率（由 recorderdialog 统一计算，保证与编码器一致）
    if (m_targetW > 0 && m_targetH > 0) {
        if (image.width() != m_targetW || image.height() != m_targetH) {
            image = image.scaled(m_targetW, m_targetH, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            // scaled() 可能改变格式，防御性转回 RGB888
            image = image.convertToFormat(QImage::Format_RGB888);
        }
    }

    // 发送桌面画面：QImage 隐式共享，可安全传递给多个消费者
    Q_EMIT SIG_sendVideoFrame(image);
}

int PicInPic_Read::ImageToYuvBuffer( QImage& image , uint8_t ** buffer )
{
    int w = image.width();
    int h = image.height();
    int y_size = w * h;
    // image.invertPixels(QImage::InvertRgb);
    //==================== 创建 RGB 对应的空间 =========================
    AVFrame *pFrameRGB = av_frame_alloc();
    // Determine required buffer size and allocate buffer
    int numBytes1 = avpicture_get_size(AV_PIX_FMT_RGB24, w, h);
    uint8_t *buffer1 = (uint8_t *)av_malloc(numBytes1*sizeof(uint8_t));
    avpicture_fill((AVPicture *)pFrameRGB, buffer1, AV_PIX_FMT_RGB24, w, h);
    // 直接使用 QImage 内部数据避免一次拷贝，但必须同步更新 linesize
    pFrameRGB->data[0] = image.bits();
    pFrameRGB->linesize[0] = image.bytesPerLine();
    //==================== 创建 YUV 对应的空间 =========================
    AVFrame *pFrameYUV = av_frame_alloc();
    // Determine required buffer size and allocate buffer
    int numBytes2 = avpicture_get_size(AV_PIX_FMT_YUV420P, w, h);
    uint8_t *buffer2 = (uint8_t *)av_malloc(numBytes2*sizeof(uint8_t));
    avpicture_fill((AVPicture *)pFrameYUV, buffer2, AV_PIX_FMT_YUV420P, w, h);
    //  qWarning() << "numBytes2 " << numBytes2;
    //==================== 创建转化器 ================================
    SwsContext *  rgb_to_yuv_ctx = sws_getContext(w,h, AV_PIX_FMT_RGB24,
                                                  w,h, AV_PIX_FMT_YUV420P,
                                                  SWS_BICUBIC, NULL,NULL,NULL);
    sws_scale(rgb_to_yuv_ctx, pFrameRGB->data, pFrameRGB->linesize, 0,
              h, pFrameYUV->data, pFrameYUV->linesize);
    //使用 av_image_copy_to_buffer 正确处理 YUV 平面布局和对齐
    uint8_t * picture_buf = (uint8_t *)av_malloc(numBytes2);
    av_image_copy_to_buffer(picture_buf, numBytes2,
                            (const uint8_t * const *)pFrameYUV->data,
                            pFrameYUV->linesize,
                            AV_PIX_FMT_YUV420P, w, h, 1);
    //写返回空间
    *buffer = picture_buf;
    //qWarning() << rc << endl;
    sws_freeContext(rgb_to_yuv_ctx);
    av_free(buffer1);
    av_free(buffer2);
    av_frame_free(&pFrameRGB);
    av_frame_free(&pFrameYUV);
    return y_size*3/2;
}
