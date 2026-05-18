#ifndef AUDIO_MIXER_H
#define AUDIO_MIXER_H

#include <QObject>
#include <QString>
#include <QTimer>
#include <QAudioInput>
#include <QAudioFormat>
#include <QAudioDeviceInfo>
#include <QIODevice>
#include <vector>
#include <stdint.h>

// FFmpeg forward declaration (full include in .cpp)
struct SwrContext;

// ---------------------------------------------------------------------------
// Constants — identical to Audio_Read
// ---------------------------------------------------------------------------
#define OneAudioSize           4096   // bytes per channel per frame (S16: 1024smp*2ch*2B)
#define AudioCollectFrequency  44100  // sample rate, Hz
#define AUDIO_INTERVAL         20     // timer interval, ms
#define AudioChannelCount      2      // stereo

// ---------------------------------------------------------------------------
// Audio_Mixer — dual-device capture + S16-domain mix + single swr_convert
// ---------------------------------------------------------------------------
class Audio_Mixer : public QObject
{
    Q_OBJECT

signals:
    // Emitted for every complete mixed frame.
    // Buffer layout: LLLL…RRRR…  (OneAudioSize * AudioChannelCount = 8192 bytes).
    // Caller MUST free(buf).
    void SIG_sendAudioFrameData(uint8_t* buf, int buffer_size);

public:
    // Device names are configurable; defaults target the user's hardware.
    explicit Audio_Mixer(const QString& systemAudioDevice,
                         const QString& micDevice,
                         QObject *parent = nullptr);

    ~Audio_Mixer();
    void setEnableSystemAudio(bool enable);

public slots:
    void slot_openAudio();   // open both devices, create SwrContext, start timer
    void slot_readMore();    // timer callback — read, mix, convert, emit
    void slot_closeAudio();  // pause capture (stop timer + devices)
    void UnInit();           // stop timer + devices (same lifecycle as Audio_Read)

private:
    // Per-device state — mirrors the single-device pattern in Audio_Read
    struct DeviceStream
    {
        QString                deviceName;   // human-readable name for lookup
        QAudioFormat           format;
        QAudioInput*           audio_in    = nullptr;
        QIODevice*             myBuffer_in = nullptr;
        std::vector<uint8_t>   m_audiobuff;  // accumulation buffer
        int                    m_buffPos   = 0;   // offset of unconsumed data
    };

    enum ENUM_AUDIO_STATE { state_stop, state_play, state_pause };

    DeviceStream  m_systemDev;   // Device A – system audio (loopback)
    DeviceStream  m_micDev;      // Device B – microphone
    SwrContext*   swr = nullptr;
    QTimer*       timer;
    int           m_playState = state_stop;
    bool          m_enableSystemAudio = true;

    // ---- helpers ----------------------------------------------------------
    QAudioDeviceInfo findDeviceByName(const QString& name);
    bool             openDevice(DeviceStream& dev);
    void             closeDevice(DeviceStream& dev);
    void             readFromDevice(DeviceStream& dev);
    void             updateBufferPos(DeviceStream& dev, int consumedBytes);
};

#endif // AUDIO_MIXER_H
