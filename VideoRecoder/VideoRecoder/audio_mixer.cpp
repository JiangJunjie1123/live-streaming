#include "audio_mixer.h"

#include <QDebug>
#include <cstring>    // memcpy, memset
#include <cstdlib>    // malloc, free
#include <algorithm>  // std::min
#include <climits>    // INT16_MIN, INT16_MAX

extern "C" {
#include <libswresample/swresample.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
}

// ===================================================================
// Construction / Destruction
// ===================================================================

Audio_Mixer::Audio_Mixer(const QString& systemAudioDevice,
                         const QString& micDevice,
                         QObject *parent)
    : QObject(parent)
{
    m_systemDev.deviceName = systemAudioDevice;
    m_micDev.deviceName    = micDevice;

    timer = new QTimer(this);
    timer->setTimerType(Qt::PreciseTimer);
    connect(timer, SIGNAL(timeout()), this, SLOT(slot_readMore()));

    m_playState = state_stop;
}

Audio_Mixer::~Audio_Mixer()
{
    UnInit();

    closeDevice(m_systemDev);
    closeDevice(m_micDev);

    if (swr) {
        swr_free(&swr);
        swr = nullptr;
    }
}

void Audio_Mixer::setEnableSystemAudio(bool enable)
{
    m_enableSystemAudio = enable;
}

// ===================================================================
// Device helpers
// ===================================================================

QAudioDeviceInfo Audio_Mixer::findDeviceByName(const QString& name)
{
    QList<QAudioDeviceInfo> devices =
        QAudioDeviceInfo::availableDevices(QAudio::AudioInput);

    for (const QAudioDeviceInfo& dev : devices) {
        if (dev.deviceName() == name) {
            qDebug() << "[Audio_Mixer] found device:" << dev.deviceName();
            return dev;
        }
    }

    qWarning() << "Audio_Mixer: device not found:" << name
               << "— falling back to default input device.";
    QAudioDeviceInfo fallback = QAudioDeviceInfo::defaultInputDevice();
    qWarning() << "Audio_Mixer: fallback device:" << fallback.deviceName();
    return fallback;
}

bool Audio_Mixer::openDevice(DeviceStream& dev)
{
    QAudioDeviceInfo info = findDeviceByName(dev.deviceName);

    dev.format.setSampleRate(AudioCollectFrequency);
    dev.format.setChannelCount(AudioChannelCount);
    dev.format.setSampleSize(16);
    dev.format.setCodec("audio/pcm");
    dev.format.setByteOrder(QAudioFormat::LittleEndian);
    dev.format.setSampleType(QAudioFormat::SignedInt);

    if (!info.isFormatSupported(dev.format)) {
        qWarning() << "Audio_Mixer: requested format not supported for"
                   << dev.deviceName << "— using nearest.";
        dev.format = info.nearestFormat(dev.format);
    }

    dev.audio_in = new QAudioInput(info, dev.format, this);

    if (!dev.audio_in) {
        qWarning() << "Audio_Mixer: failed to create QAudioInput for"
                   << dev.deviceName;
        return false;
    }

    dev.myBuffer_in = dev.audio_in->start();
    if (!dev.myBuffer_in) {
        qWarning() << "Audio_Mixer: QAudioInput::start() failed for"
                   << dev.deviceName;
        closeDevice(dev);
        return false;
    }

    dev.m_audiobuff.clear();
    dev.m_buffPos = 0;

    return true;
}

void Audio_Mixer::closeDevice(DeviceStream& dev)
{
    if (dev.audio_in) {
        dev.audio_in->stop();
        delete dev.audio_in;
        dev.audio_in    = nullptr;
        dev.myBuffer_in = nullptr;
    }
    dev.m_audiobuff.clear();
    dev.m_buffPos = 0;
}

// ===================================================================
// Buffer management — mirrors Audio_Read::slot_readMore
// ===================================================================

void Audio_Mixer::readFromDevice(DeviceStream& dev)
{
    if (!dev.myBuffer_in)
        return;

    // 文献标准做法：用 readAll() 一次性读走全部可用数据，追加到 buffer 末尾
    QByteArray data = dev.myBuffer_in->readAll();
    if (data.isEmpty())
        return;

    size_t oldSize = dev.m_audiobuff.size();
    dev.m_audiobuff.resize(oldSize + data.size());
    memcpy(dev.m_audiobuff.data() + oldSize, data.constData(), data.size());
}

void Audio_Mixer::updateBufferPos(DeviceStream& dev, int consumedBytes)
{
    if (consumedBytes >= static_cast<int>(dev.m_audiobuff.size())) {
        dev.m_audiobuff.clear();
    } else {
        size_t remaining = dev.m_audiobuff.size() - consumedBytes;
        memmove(dev.m_audiobuff.data(),
                dev.m_audiobuff.data() + consumedBytes,
                remaining);
        dev.m_audiobuff.resize(remaining);
    }
}

// ===================================================================
// Lifecycle slots
// ===================================================================

void Audio_Mixer::slot_openAudio()
{
    if (m_playState == state_play || m_playState == state_pause) {
        slot_closeAudio();
    }

    closeDevice(m_systemDev);
    closeDevice(m_micDev);
    if (swr) {
        swr_free(&swr);
        swr = nullptr;
    }

    bool sysOpened = false;
    if (m_enableSystemAudio) {
        if (openDevice(m_systemDev)) {
            sysOpened = true;
        }
    }
    if (!openDevice(m_micDev)) {
        if (sysOpened) closeDevice(m_systemDev);
        return;
    }
    if (!sysOpened && m_enableSystemAudio)
        qWarning() << "Audio_Mixer: system audio not available, mic only";

    m_playState = state_play;

    int64_t in_ch_layout  = AV_CH_LAYOUT_STEREO;
    int64_t out_ch_layout = AV_CH_LAYOUT_STEREO;

    swr = swr_alloc_set_opts(
        nullptr,
        out_ch_layout, AV_SAMPLE_FMT_FLTP, AudioCollectFrequency,
        in_ch_layout,  AV_SAMPLE_FMT_S16,  AudioCollectFrequency,
        0, nullptr);

    if (!swr || swr_init(swr) < 0) {
        qWarning() << "Audio_Mixer: swr_init failed";
        if (swr) {
            swr_free(&swr);
            swr = nullptr;
        }
        closeDevice(m_systemDev);
        closeDevice(m_micDev);
        return;
    }

    // 文献 2.1.2：用 readyRead 事件驱动代替定时器轮询
    timer->start(AUDIO_INTERVAL);
}

void Audio_Mixer::slot_readMore()
{
    if (m_playState != state_play)
        return;
    if (!m_micDev.audio_in)
        return;
    bool sysActive = (m_systemDev.audio_in && m_systemDev.myBuffer_in);

    // ---- 1. Read from both devices ----
    if (sysActive) readFromDevice(m_systemDev);
    readFromDevice(m_micDev);

    // ---- 2. Determine available frames ----
    int framesMic = static_cast<int>(m_micDev.m_audiobuff.size()) / OneAudioSize;
    if (framesMic <= 0) return;

    int framesSys = sysActive ? static_cast<int>(m_systemDev.m_audiobuff.size()) / OneAudioSize : 0;
    int nFrameCount = sysActive ? std::min(framesSys, framesMic) : framesMic;
    if (nFrameCount <= 0) return;

    int mixedBytes   = OneAudioSize * nFrameCount;
    int totalSamples = mixedBytes / static_cast<int>(sizeof(int16_t));

    const int16_t* pMic = reinterpret_cast<const int16_t*>(m_micDev.m_audiobuff.data());
    const int16_t* pSys = sysActive ? reinterpret_cast<const int16_t*>(m_systemDev.m_audiobuff.data()) : nullptr;

    std::vector<uint8_t> mixedBuf;
    const uint8_t* in_plane;

    if (sysActive) {
        mixedBuf.resize(mixedBytes);
        int16_t* pMixed = reinterpret_cast<int16_t*>(mixedBuf.data());
        for (int i = 0; i < totalSamples; ++i) {
            float mix = static_cast<float>(pSys[i]) * 0.8f + static_cast<float>(pMic[i]) * 1.0f;
            if (mix > static_cast<float>(INT16_MAX)) mix = static_cast<float>(INT16_MAX);
            else if (mix < static_cast<float>(INT16_MIN)) mix = static_cast<float>(INT16_MIN);
            pMixed[i] = static_cast<int16_t>(mix);
        }
        in_plane = mixedBuf.data();
    } else {
        in_plane = reinterpret_cast<const uint8_t*>(pMic);
    }

    // ---- 5. Single swr_convert: S16 -> FLTP planar ----
    uint8_t* outL = static_cast<uint8_t*>(malloc(
        static_cast<size_t>(OneAudioSize) * nFrameCount));
    uint8_t* outR = static_cast<uint8_t*>(malloc(
        static_cast<size_t>(OneAudioSize) * nFrameCount));

    if (!outL || !outR) {
        free(outL);
        free(outR);
        qWarning() << "Audio_Mixer: malloc failed for FLTP buffers";
        return;
    }

    memset(outL, 0, static_cast<size_t>(OneAudioSize) * nFrameCount);
    memset(outR, 0, static_cast<size_t>(OneAudioSize) * nFrameCount);

    uint8_t*  out_planes[2] = { outL, outR };

    int out_samples = nFrameCount * OneAudioSize / 4;

    int ret = swr_convert(swr,
                          out_planes,
                          out_samples,
                          &in_plane,
                          out_samples);

    if (ret < 0) {
        qWarning() << "Audio_Mixer: swr_convert failed, ret =" << ret;
        free(outL);
        free(outR);
        return;
    }

    // ---- 6. Emit each frame in LLLL...RRRR... layout ----
    for (int n = 0; n < nFrameCount; ++n) {
        const int frameBytes = OneAudioSize * AudioChannelCount;

        uint8_t* buf = static_cast<uint8_t*>(malloc(frameBytes));
        if (!buf) {
            qWarning() << "Audio_Mixer: malloc failed for emit buffer";
            continue;
        }
        memset(buf, 0, frameBytes);

        const uint8_t* left  = outL + n * OneAudioSize;
        const uint8_t* right = outR + n * OneAudioSize;

        memcpy(buf,                   left,  OneAudioSize);
        memcpy(buf + OneAudioSize,    right, OneAudioSize);

        Q_EMIT SIG_sendAudioFrameData(buf, frameBytes);
    }

    free(outL);
    free(outR);

    // ---- 7. Consume from buffers ----
    int consumedBytes = nFrameCount * OneAudioSize;
    if (sysActive) updateBufferPos(m_systemDev, consumedBytes);
    updateBufferPos(m_micDev, consumedBytes);
}

void Audio_Mixer::slot_closeAudio()
{
    if (timer)
        timer->stop();

    if (m_playState == state_play) {
        m_playState = state_pause;

        if (m_systemDev.audio_in)
            m_systemDev.audio_in->stop();
        if (m_micDev.audio_in)
            m_micDev.audio_in->stop();
    }
}

void Audio_Mixer::UnInit()
{
    if (timer)
        timer->stop();

    if (m_systemDev.audio_in)
        m_systemDev.audio_in->stop();
    if (m_micDev.audio_in)
        m_micDev.audio_in->stop();
}
