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
    if (!dev.audio_in)
        return;

    qint64 len = dev.audio_in->bytesReady();
    if (len <= 0)
        return;

    if (dev.m_buffPos == 0) {
        dev.m_audiobuff.resize(static_cast<size_t>(len));
    } else {
        int nSize = static_cast<int>(dev.m_audiobuff.size());
        uint8_t* pBegin = dev.m_audiobuff.data();

        std::vector<uint8_t> tempbuff(nSize - dev.m_buffPos, 0);
        memcpy(tempbuff.data(), pBegin + dev.m_buffPos,
               tempbuff.size());

        dev.m_audiobuff.resize(tempbuff.size() + static_cast<size_t>(len));
        pBegin = dev.m_audiobuff.data();

        memcpy(pBegin, tempbuff.data(), tempbuff.size());
        dev.m_buffPos = static_cast<int>(tempbuff.size());
    }

    uint8_t* pDst = dev.m_audiobuff.data() + dev.m_buffPos;
    int remaining = static_cast<int>(len);

    while (remaining > 0) {
        qint64 n = dev.myBuffer_in->read(
            reinterpret_cast<char*>(pDst), remaining);
        if (n > 0) {
            remaining -= static_cast<int>(n);
            pDst      += n;
        } else {
            if (n < 0)
                qWarning() << "Audio_Mixer: read error on" << dev.deviceName;
            break;
        }
    }
}

void Audio_Mixer::updateBufferPos(DeviceStream& dev, int consumedBytes)
{
    dev.m_buffPos = consumedBytes;

    if (dev.m_buffPos >= static_cast<int>(dev.m_audiobuff.size())) {
        dev.m_audiobuff.clear();
        dev.m_buffPos = 0;
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

    if (!openDevice(m_systemDev)) {
        qWarning() << "Audio_Mixer: failed to open system audio device";
        return;
    }
    if (!openDevice(m_micDev)) {
        qWarning() << "Audio_Mixer: failed to open microphone device";
        closeDevice(m_systemDev);
        return;
    }

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

    timer->start(AUDIO_INTERVAL);
}

void Audio_Mixer::slot_readMore()
{
    if (m_playState != state_play)
        return;
    if (!m_systemDev.audio_in || !m_micDev.audio_in)
        return;

    // ---- 1. Read from both devices ----
    readFromDevice(m_systemDev);
    readFromDevice(m_micDev);

    // ---- 2. Determine available frames ----
    int framesSys = static_cast<int>(m_systemDev.m_audiobuff.size())
                    / OneAudioSize;
    int framesMic = static_cast<int>(m_micDev.m_audiobuff.size())
                    / OneAudioSize;

    if (framesSys <= 0 || framesMic <= 0)
        return;   // wait for both devices to have data

    // ---- 3. Process all available frames from both (like Audio_Read) ----
    int nFrameCount = std::min(framesSys, framesMic);

    int mixedBytes   = OneAudioSize * nFrameCount;
    int totalSamples = mixedBytes / static_cast<int>(sizeof(int16_t));

    // ---- 4. Mix in S16 domain: system*0.8 + mic*1.0 -> clamped ----
    std::vector<uint8_t> mixedBuf(mixedBytes);
    int16_t* pMixed = reinterpret_cast<int16_t*>(mixedBuf.data());
    const int16_t* pSys = reinterpret_cast<const int16_t*>(
        m_systemDev.m_audiobuff.data());
    const int16_t* pMic = reinterpret_cast<const int16_t*>(
        m_micDev.m_audiobuff.data());

    // Diagnostic every ~2 seconds
    static int diagFrameCount = 0;
    diagFrameCount += nFrameCount;
    if (diagFrameCount >= 100) {
        diagFrameCount = 0;
        int16_t peakSys = 0, peakMic = 0;
        for (int i = 0; i < totalSamples; ++i) {
            int16_t s = pSys[i] > 0 ? pSys[i] : (int16_t)(-pSys[i]);
            int16_t m = pMic[i] > 0 ? pMic[i] : (int16_t)(-pMic[i]);
            if (s > peakSys) peakSys = s;
            if (m > peakMic) peakMic = m;
        }
        qDebug() << "[Audio_Mixer diag] systemPeak:" << peakSys
                 << "micPeak:" << peakMic
                 << "framesSys:" << framesSys << "framesMic:" << framesMic;
    }

    for (int i = 0; i < totalSamples; ++i) {
        float mix = static_cast<float>(pSys[i]) * 0.8f
                  + static_cast<float>(pMic[i]) * 1.0f;

        if (mix > static_cast<float>(INT16_MAX))
            mix = static_cast<float>(INT16_MAX);
        else if (mix < static_cast<float>(INT16_MIN))
            mix = static_cast<float>(INT16_MIN);

        pMixed[i] = static_cast<int16_t>(mix);
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
    const uint8_t* in_plane   = mixedBuf.data();

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

    // ---- 7. Consume from both buffers ----
    int consumedBytes = nFrameCount * OneAudioSize;
    updateBufferPos(m_systemDev, consumedBytes);
    updateBufferPos(m_micDev,    consumedBytes);
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
