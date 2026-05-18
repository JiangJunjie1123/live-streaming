#include "systemaudioread.h"
#include <cstring>
#include <objbase.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <initguid.h>

// KSDATAFORMAT_SUBTYPE_IEEE_FLOAT
static const GUID SUBTYPE_IEEE_FLOAT =
    {0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71}};

#define SAFE_RELEASE(p) do { if (p) { (p)->Release(); (p) = nullptr; } } while(0)

SystemAudioRead::SystemAudioRead(QObject *parent)
    : QObject(parent)
    , m_timer(nullptr)
    , m_pEnumerator(nullptr)
    , m_pDevice(nullptr)
    , m_pAudioClient(nullptr)
    , m_pCaptureClient(nullptr)
    , m_bufferFrameCount(0)
    , m_inputChannels(2)
    , m_inputSampleRate(44100)
    , m_swr(nullptr)
    , m_nbSamples(1024)
    , m_bytesPerSample(4)
    , m_state(state_stop)
{
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        QMessageBox::information(NULL, QString::fromUtf8("提示"),
                                 QString::fromUtf8("COM 初始化失败"));
    }
    m_timer = new QTimer(this);
    connect(m_timer, SIGNAL(timeout()), this, SLOT(slot_readMore()));
}

SystemAudioRead::~SystemAudioRead()
{
    UnInit();
}

void SystemAudioRead::UnInit()
{
    if (m_timer) m_timer->stop();
    releaseWASAPI();
    if (m_swr) { swr_free(&m_swr); m_swr = nullptr; }
}

static AVSampleFormat detectFormat(WAVEFORMATEX *pwfx)
{
    bool isFloat = false;
    if (pwfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        WAVEFORMATEXTENSIBLE *pwfxe = (WAVEFORMATEXTENSIBLE *)pwfx;
        isFloat = IsEqualGUID(pwfxe->SubFormat, SUBTYPE_IEEE_FLOAT);
    } else {
        isFloat = (pwfx->wFormatTag == WAVE_FORMAT_IEEE_FLOAT);
    }
    if (isFloat) return AV_SAMPLE_FMT_FLT;
    if (pwfx->wBitsPerSample == 32) return AV_SAMPLE_FMT_S32;
    if (pwfx->wBitsPerSample == 16) return AV_SAMPLE_FMT_S16;
    return AV_SAMPLE_FMT_S16;
}

bool SystemAudioRead::initWASAPI()
{
    HRESULT hr;

    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
                          __uuidof(IMMDeviceEnumerator), (void**)&m_pEnumerator);
    if (FAILED(hr)) {
        QMessageBox::information(NULL, QString::fromUtf8("提示"),
                                 QString::fromUtf8("无法创建音频设备枚举器"));
        return false;
    }

    hr = m_pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &m_pDevice);
    if (FAILED(hr)) {
        QMessageBox::information(NULL, QString::fromUtf8("提示"),
                                 QString::fromUtf8("未找到扬声器设备"));
        return false;
    }

    // 打印设备 ID
    LPWSTR pwszId = NULL;
    if (SUCCEEDED(m_pDevice->GetId(&pwszId))) {
        char szId[256];
        WideCharToMultiByte(CP_UTF8, 0, pwszId, -1, szId, sizeof(szId), NULL, NULL);
        fprintf(stderr, "[SysAudio] Device: %s\n", szId);
        CoTaskMemFree(pwszId);
    }

    hr = m_pDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL,
                             NULL, (void**)&m_pAudioClient);
    if (FAILED(hr)) {
        QMessageBox::information(NULL, QString::fromUtf8("提示"),
                                 QString::fromUtf8("激活音频客户端失败"));
        return false;
    }

    WAVEFORMATEX *pwfx = NULL;
    hr = m_pAudioClient->GetMixFormat(&pwfx);
    if (FAILED(hr)) return false;

    m_inputChannels = pwfx->nChannels;
    m_bytesPerSample = pwfx->wBitsPerSample / 8;
    AVSampleFormat in_fmt = detectFormat(pwfx);

    // 尝试请求 44100Hz，这样无需重采样，和麦克风逻辑完全一致
    WAVEFORMATEX reqFmt = *pwfx;
    reqFmt.nSamplesPerSec = 44100;
    reqFmt.nAvgBytesPerSec = reqFmt.nChannels * 44100 * (reqFmt.wBitsPerSample / 8);
    reqFmt.nBlockAlign = reqFmt.nChannels * (reqFmt.wBitsPerSample / 8);

    REFERENCE_TIME hnsDuration = 1000000; // 100ms（原 20ms 太小导致数据到达不均匀、卡顿）
    hr = m_pAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                     AUDCLNT_STREAMFLAGS_LOOPBACK,
                                     hnsDuration, 0, &reqFmt, NULL);
    if (SUCCEEDED(hr)) {
        m_inputSampleRate = 44100;
        fprintf(stderr, "[SysAudio] Using 44100Hz (no resampling needed)\n");
    } else {
        // 回退到混音格式（通常是 48000Hz）
        hr = m_pAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                         AUDCLNT_STREAMFLAGS_LOOPBACK,
                                         hnsDuration, 0, pwfx, NULL);
        if (FAILED(hr)) {
            CoTaskMemFree(pwfx);
            QMessageBox::information(NULL, QString::fromUtf8("提示"),
                                     QString::fromUtf8("初始化音频客户端失败"));
            return false;
        }
        m_inputSampleRate = pwfx->nSamplesPerSec;
        fprintf(stderr, "[SysAudio] Using mix format %d Hz (will resample)\n", m_inputSampleRate);
    }
    CoTaskMemFree(pwfx);

    hr = m_pAudioClient->GetBufferSize(&m_bufferFrameCount);
    hr = m_pAudioClient->GetService(__uuidof(IAudioCaptureClient),
                                    (void**)&m_pCaptureClient);
    if (FAILED(hr)) return false;

    // 设置 swr_convert：与 Audio_Read 完全一致的模式
    if (m_swr) { swr_free(&m_swr); m_swr = nullptr; }
    int64_t in_layout = av_get_default_channel_layout(m_inputChannels);
    m_swr = swr_alloc_set_opts(nullptr,
        AV_CH_LAYOUT_STEREO, AV_SAMPLE_FMT_FLTP, SYS_SampleRate,  // out: FLTP 44100
        in_layout,           in_fmt,              m_inputSampleRate, // in
        0, nullptr);
    if (!m_swr || swr_init(m_swr) < 0) {
        QMessageBox::information(NULL, QString::fromUtf8("提示"),
                                 QString::fromUtf8("初始化音频转换器失败"));
        return false;
    }

    return true;
}

void SystemAudioRead::releaseWASAPI()
{
    SAFE_RELEASE(m_pCaptureClient);
    SAFE_RELEASE(m_pAudioClient);
    SAFE_RELEASE(m_pDevice);
    SAFE_RELEASE(m_pEnumerator);
}

void SystemAudioRead::slot_openAudio()
{
    if (m_state == state_play) {
        m_timer->stop();
        if (m_pAudioClient) m_pAudioClient->Stop();
        releaseWASAPI();
    }
    m_state = state_stop;
    m_outbuffL.clear();
    m_outbuffR.clear();

    if (!initWASAPI()) return;

    if (FAILED(m_pAudioClient->Start())) {
        QMessageBox::information(NULL, QString::fromUtf8("提示"),
                                 QString::fromUtf8("启动音频捕获失败"));
        return;
    }
    m_state = state_play;
    m_timer->start(SYS_AUDIO_INTERVAL);
}

void SystemAudioRead::slot_closeAudio()
{
    if (m_timer) m_timer->stop();
    if (m_pAudioClient && m_state == state_play) m_pAudioClient->Stop();
    m_state = state_pause;
}

void SystemAudioRead::slot_readMore()
{
    if (m_state != state_play || !m_pCaptureClient) return;

    // —— 1. 读取本次所有 WASAPI 数据包到本地缓冲区 ——
    UINT32 packetLength = 0;
    if (FAILED(m_pCaptureClient->GetNextPacketSize(&packetLength)) || packetLength == 0)
        return;

    int bytesPerFrame = m_inputChannels * m_bytesPerSample;
    std::vector<uint8_t> inputBuff;

    while (packetLength > 0) {
        BYTE *pData = NULL;
        UINT32 numFrames = 0;
        DWORD flags = 0;
        if (FAILED(m_pCaptureClient->GetBuffer(&pData, &numFrames, &flags, NULL, NULL))) break;

        int dataSize = numFrames * bytesPerFrame;
        size_t oldSize = inputBuff.size();
        inputBuff.resize(oldSize + dataSize);

        if (flags & AUDCLNT_BUFFERFLAGS_SILENT)
            memset(&inputBuff[oldSize], 0, dataSize);
        else
            memcpy(&inputBuff[oldSize], pData, dataSize);

        m_pCaptureClient->ReleaseBuffer(numFrames);
        if (FAILED(m_pCaptureClient->GetNextPacketSize(&packetLength))) break;
    }

    int in_samples = inputBuff.size() / (m_inputChannels * m_bytesPerSample);
    if (in_samples == 0) return;

    // —— 2. 重采样：全部输入 → swr_convert → 临时输出 ——
    // 输出采样数 ≈ in_samples * (out_rate / in_rate) + 余量
    int max_out = av_rescale_rnd(in_samples + 256, SYS_SampleRate, m_inputSampleRate, AV_ROUND_UP);
    uint8_t *tmpL = (uint8_t *)malloc(max_out * sizeof(float));
    uint8_t *tmpR = (uint8_t *)malloc(max_out * sizeof(float));
    uint8_t *tmp[2] = { tmpL, tmpR };

    const uint8_t *in_data = &inputBuff[0];
    int out_count = swr_convert(m_swr, tmp, max_out, &in_data, in_samples);

    // —— 3. 追加入输出累积缓冲区 ——
    if (out_count > 0) {
        size_t old = m_outbuffL.size();
        m_outbuffL.resize(old + out_count * sizeof(float));
        m_outbuffR.resize(old + out_count * sizeof(float));
        memcpy(&m_outbuffL[old], tmpL, out_count * sizeof(float));
        memcpy(&m_outbuffR[old], tmpR, out_count * sizeof(float));
    }

    free(tmpL);
    free(tmpR);

    // —— 4. 发出完整帧（每帧 1024 采样 × 4 字节 = 4096 字节/声道）——
    while ((int)m_outbuffL.size() >= SYS_OneAudioSize) {
        uint8_t *buf = (uint8_t *)malloc(SYS_OneAudioSize * 2);
        memcpy(buf, &m_outbuffL[0], SYS_OneAudioSize);
        memcpy(buf + SYS_OneAudioSize, &m_outbuffR[0], SYS_OneAudioSize);
        Q_EMIT SIG_sendAudioFrameData(buf, SYS_OneAudioSize * 2);

        m_outbuffL.erase(m_outbuffL.begin(), m_outbuffL.begin() + SYS_OneAudioSize);
        m_outbuffR.erase(m_outbuffR.begin(), m_outbuffR.begin() + SYS_OneAudioSize);
    }
}
