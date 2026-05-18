#ifndef SAVEVIDEOFILETHREAD_H
#define SAVEVIDEOFILETHREAD_H

#include <QThread>
#include <QImage>
#include <QString>
#include <QDateTime>
#include <QList>
#include <QMutex>
#include "picinpic_read.h"

extern "C" {
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"
#include "libswresample/swresample.h"
#include "libavutil/imgutils.h"
}

#define STREAM_PIX_FMT    AV_PIX_FMT_YUV420P

//优化 队列节点结构
struct BufferDataNode {
    uint8_t *buffer;      // 数据指针
    int bufferSize;       // 数据大小
    int64_t time;         // 时间戳（用于稳帧）
    int width;            // 视频宽度（仅视频节点使用）
    int height;           // 视频高度（仅视频节点使用）
    int stride;           // 行跨度（bytesPerLine，含对齐填充）
};

typedef struct OutputStream {
    AVStream *st;
    AVCodecContext *enc;
    int64_t next_pts;
    int samples_count;
    AVFrame *frame;
    AVFrame *tmp_frame;
    uint8_t *frameBuffer;
    int frameBufferSize;
    struct SwsContext *sws_ctx;
    struct SwrContext *swr_ctx;
} OutputStream;

typedef struct STRU_AV_FORMAT {
    QString fileName;
    int width;
    int height;
    int frame_rate;
    int videoBitRate;
    bool hasCamera;
    bool hasDesk;
    bool hasAudio;
    bool hasSysAudio;
} STRU_AV_FORMAT;

class Audio_Mixer;

class SaveVideoFileThread : public QThread
{
    Q_OBJECT

    friend int interrupt_cb(void*);

signals:
    void SIG_sendPicInPic(QImage image);
    void SIG_sendVideoFrame(QImage image);
    void SIG_EncodeError(const QString &msg);

public:
    explicit SaveVideoFileThread();
    ~SaveVideoFileThread();  //优化 添加析构函数
    const STRU_AV_FORMAT &avFormat() const;

public slots:
    void slot_writeVideoFrame(QImage image);
    void slot_writeVideoFrameData(uint8_t *buf, int buffer_size);  //优化 视频帧数据写入
    void slot_queueVideoFrame(QImage image);  // RGB 队列化，由编码线程做 YUV 转换
    void slot_writeAudioFrameData(uint8_t *buf, int buffer_size);
    void slot_setInfo(STRU_AV_FORMAT& avFormat);
    void slot_openVideo();
    void slot_closeVideo();
    void slot_openAudio();   // 开始音频采集
    void slot_closeAudio();  // 停止音频采集

private:
    void add_Video_stream(OutputStream *ost, AVFormatContext *oc,
                          AVCodec **codec, enum AVCodecID codec_id);
    void add_audio_stream(OutputStream *ost, AVFormatContext *oc,
                          AVCodec **codec, enum AVCodecID codec_id);  // 添加音频流
    void open_video(AVFormatContext *oc, AVCodec *codec, OutputStream *ost);
    void open_audio(AVFormatContext *oc, AVCodec *codec, OutputStream *ost);  // 打开音频编码器
    void close_stream(AVFormatContext *oc, OutputStream *ost);

    //优化 队列操作方法
    void videoDataQueue_Input(uint8_t *buffer, int size, int64_t time,
                              int width = 0, int height = 0, int stride = 0);
    BufferDataNode* videoDataQueue_get(int64_t time);
    void audioDataQueue_Input(const uint8_t *buffer, int size);
    BufferDataNode* audioDataQueue_get();
    //优化 写线程方法
    bool write_video_frame_from_queue();
    bool write_audio_frame_from_queue();

    //采集音频 视频 归写线程来管理
    PicInPic_Read *m_picInPicRead;
    Audio_Mixer *m_audioRead;

    // FFmpeg相关
    AVFormatContext *oc;
    AVOutputFormat *fmt;
    AVCodec *video_codec;
    AVCodec *audio_codec;  // 音频编码器
    OutputStream video_st;
    OutputStream audio_st;  // 音频流
    int have_video;
    int have_audio;  // 是否有音频（麦克风或系统音频）
    int encode_video;
    int encode_audio;  // 是否编码音频
    STRU_AV_FORMAT m_avFormat;
    bool m_isRecording;  // 录制状态标志
    qint64 m_startTime;  // 录制开始时间戳
    int mAudioOneFrameSize;  // 音频一帧大小

    //优化 队列相关成员
    QList<BufferDataNode*> m_videoDataList;    // 视频队列
    QList<BufferDataNode*> m_audioDataList;    // 音频队列
    BufferDataNode* lastVideoNode;             // 上一帧视频（用于补帧）
    QMutex m_videoMutex;                       // 视频队列锁
    QMutex m_audioMutex;                       // 音频队列锁
    bool isStop;                               // 停止标志
    int64_t video_pts;                         // 视频时间戳（毫秒）
    int64_t audio_pts;                         // 音频时间戳（毫秒）
    bool m_videoBeginFlag;                     // 视频开始标志
    qint64 m_videoBeginTime;                   // 视频开始时间

    AVBufferRef *hw_device_ctx;
    bool use_hw_encode;

    //优化 队列大小限制
    static const int MAX_VIDEO_QUEUE_SIZE = 20;  // 约0.8秒视频 — 降低延迟
    static const int MAX_AUDIO_QUEUE_SIZE = 30;  // 约0.6秒音频 — 降低延迟

protected:
    void run() override;  //优化 重写线程运行函数
};

#endif // SAVEVIDEOFILETHREAD_H
