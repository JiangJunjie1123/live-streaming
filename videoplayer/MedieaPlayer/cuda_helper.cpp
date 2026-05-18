#include "cuda_helper.h"

int hw_device_init(AVBufferRef **hw_device_ctx, AVHWDeviceType *out_type)
{
    *hw_device_ctx = nullptr;
    *out_type = AV_HWDEVICE_TYPE_NONE;
    AVHWDeviceType types[] = {AV_HWDEVICE_TYPE_CUDA, AV_HWDEVICE_TYPE_D3D11VA};
    for (auto t : types) {
        int ret = av_hwdevice_ctx_create(hw_device_ctx, t, nullptr, nullptr, 0);
        if (ret >= 0) {
            *out_type = t;
            return ret;
        }
    }
    return -1;
}

void hw_decoder_check_support(const AVCodec *decoder, AVHWDeviceType hw_type,
                               AVPixelFormat *hw_pix_fmt)
{
    *hw_pix_fmt = AV_PIX_FMT_NONE;
    for (int i = 0;; i++) {
        const AVCodecHWConfig *config = avcodec_get_hw_config(decoder, i);
        if (!config) break;
        if ((config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX)
            && config->device_type == hw_type) {
            *hw_pix_fmt = config->pix_fmt;
            break;
        }
    }
}

AVPixelFormat get_hw_format(AVCodecContext *ctx, const AVPixelFormat *pix_fmts)
{
    AVPixelFormat hw_fmt = AV_PIX_FMT_NONE;
    if (ctx->opaque) {
        hw_fmt = *(AVPixelFormat *)ctx->opaque;
    }
    for (const AVPixelFormat *p = pix_fmts; *p != AV_PIX_FMT_NONE; p++) {
        if (*p == hw_fmt) return *p;
    }
    return pix_fmts[0];
}
