#include "audio_read.h"
#include <QDebug>
Audio_Read::Audio_Read()
{
    //声卡采样格式
    format.setSampleRate(AudioCollectFrequency);
    format.setChannelCount(2);
    format.setSampleSize(16);
    format.setCodec("audio/pcm");
    format.setByteOrder(QAudioFormat::LittleEndian);
    format.setSampleType(QAudioFormat::SignedInt);
    QAudioDeviceInfo info = QAudioDeviceInfo::defaultInputDevice();
    if (!info.isFormatSupported(format)) {
        format = info.nearestFormat(format);
    }
    audio_in = NULL;
    m_playState = state_stop;
    swr = nullptr;
    timer = new QTimer;
    connect( timer , SIGNAL(timeout())
             , this , SLOT(slot_readMore()) );
    m_buffPos = 0;
}
Audio_Read::~Audio_Read()
{
    this->UnInit();
    delete audio_in;
}
void Audio_Read::slot_readMore()
{
    if (!audio_in || !myBuffer_in)
        return;

    // 1. 读取并追加到缓冲区
    QByteArray data = myBuffer_in->readAll();
    if (data.isEmpty()) return;
    size_t oldSize = m_audiobuff.size();
    m_audiobuff.resize(oldSize + data.size());
    memcpy(m_audiobuff.data() + oldSize, data.constData(), data.size());

    // 2. 计算可处理帧数
    int nFrameCount = static_cast<int>(m_audiobuff.size()) / OneAudioSize;
    if (nFrameCount <= 0) return;

    int totalBytes = OneAudioSize * nFrameCount;

    // 3. swr_convert S16 → FLTP
    uint8_t *outL = (uint8_t *)malloc(totalBytes);
    uint8_t *outR = (uint8_t *)malloc(totalBytes);
    memset(outL, 0, totalBytes);
    memset(outR, 0, totalBytes);
    uint8_t *out_planes[2] = { outL, outR };

    const uint8_t *in_data = m_audiobuff.data();
    int out_samples = nFrameCount * OneAudioSize / 4;
    swr_convert(swr, out_planes, out_samples, &in_data, out_samples);

    // 4. 逐帧发出
    for (int n = 0; n < nFrameCount; n++) {
        uint8_t *buf = (uint8_t *)malloc(OneAudioSize * AudioChannelCount);
        memcpy(buf,                   outL + n * OneAudioSize, OneAudioSize);
        memcpy(buf + OneAudioSize,    outR + n * OneAudioSize, OneAudioSize);
        Q_EMIT SIG_sendAudioFrameData(buf, OneAudioSize * AudioChannelCount);
    }

    free(outL);
    free(outR);

    // 5. 消费已处理数据
    if (totalBytes >= static_cast<int>(m_audiobuff.size())) {
        m_audiobuff.clear();
    } else {
        size_t rem = m_audiobuff.size() - totalBytes;
        memmove(m_audiobuff.data(), m_audiobuff.data() + totalBytes, rem);
        m_audiobuff.resize(rem);
    }
}
void Audio_Read::UnInit()
{
    if (myBuffer_in)
        disconnect(myBuffer_in, &QIODevice::readyRead, this, &Audio_Read::slot_readMore);
    if(timer)
        timer->stop();
    if(audio_in)
        audio_in->stop();
    if(swr) {
        swr_free(&swr);
        swr = nullptr;
    }
}

void Audio_Read:: slot_openAudio()
{
    if( m_playState == state_stop )
    {
        audio_in = new QAudioInput(format, this);
        myBuffer_in = audio_in->start();
    }
    else
    {
        if(audio_in)
        {
            delete audio_in;
            audio_in = new QAudioInput(format, this);
            myBuffer_in = audio_in->start();
        }
    }
    m_playState = state_play;

    enum AVSampleFormat in_sample_fmt;
    enum AVSampleFormat out_sample_fmt;
    int in_sample_rate;
    int out_sample_rate;
    int in_ch_layout;
    int out_ch_layout;

    in_sample_fmt = AV_SAMPLE_FMT_S16;
    out_sample_fmt = AV_SAMPLE_FMT_FLTP;

    in_sample_rate = AudioCollectFrequency;
    out_sample_rate = 44100;
    in_ch_layout = AV_CH_LAYOUT_STEREO;
    out_ch_layout = AV_CH_LAYOUT_STEREO;
    if(swr) {
        swr_free(&swr);
        swr = nullptr;
    }
    swr = swr_alloc_set_opts(nullptr, out_ch_layout, out_sample_fmt, out_sample_rate,
                             in_ch_layout, in_sample_fmt, in_sample_rate,
                             0, nullptr);
    swr_init(swr);
    connect(myBuffer_in, &QIODevice::readyRead, this, &Audio_Read::slot_readMore);
}
void Audio_Read:: slot_closeAudio()
{
    if (myBuffer_in)
        disconnect(myBuffer_in, &QIODevice::readyRead, this, &Audio_Read::slot_readMore);
    if(timer){
        timer->stop();
    }
    if( m_playState == state_play )
    {
        m_playState = state_pause;
        if(audio_in)
            audio_in->stop();
    }
}
