#ifndef SYSTEMAUDIOREAD_H
#define SYSTEMAUDIOREAD_H

#include <QObject>
#include <QTimer>
#include <QMessageBox>
#include <vector>

extern "C" {
#include "libswresample/swresample.h"
}

// 与 Audio_Read 完全一致的参数
#define SYS_OneAudioSize   (4096)   // 4096 字节/声道
#define SYS_SampleRate      (44100)
#define SYS_AUDIO_INTERVAL  (20)     // 20ms，与麦克风一致

// Windows COM 接口前向声明（避免在头文件中引入 windows.h，与 OpenCV 的 ACCESS_MASK 冲突）
struct IMMDeviceEnumerator;
struct IMMDevice;
struct IAudioClient;
struct IAudioCaptureClient;

class SystemAudioRead : public QObject
{
    Q_OBJECT

signals:
    void SIG_sendAudioFrameData(uint8_t* buf, int buffer_size);

public:
    explicit SystemAudioRead(QObject *parent = nullptr);
    ~SystemAudioRead();

public slots:
    void slot_openAudio();
    void slot_closeAudio();
    void UnInit();

private slots:
    void slot_readMore();

private:
    bool initWASAPI();
    void releaseWASAPI();

    QTimer *m_timer;

    // WASAPI COM 接口
    IMMDeviceEnumerator *m_pEnumerator;
    IMMDevice *m_pDevice;
    IAudioClient *m_pAudioClient;
    IAudioCaptureClient *m_pCaptureClient;
    unsigned int m_bufferFrameCount;
    int m_inputChannels;         // WASAPI 实际声道数
    int m_inputSampleRate;       // WASAPI 实际采样率

    // 音频重采样
    SwrContext *m_swr;
    int m_nbSamples;             // 每帧采样点数 (1024)

    int m_bytesPerSample;        // 输入采样字节数 (2=S16, 4=float)

    // 重采样输出累积缓冲区（FLTP planar，左右声道分开）
    std::vector<uint8_t> m_outbuffL;
    std::vector<uint8_t> m_outbuffR;

    enum State { state_stop, state_play, state_pause };
    State m_state;
};

#endif // SYSTEMAUDIOREAD_H
