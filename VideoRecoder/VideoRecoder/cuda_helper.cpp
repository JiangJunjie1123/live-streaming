#include "cuda_helper.h"

AVCodec *hw_encoder_select()
{
    AVCodec *codec = nullptr;
    codec = avcodec_find_encoder_by_name("h264_nvenc");
    if (codec) return codec;
    codec = avcodec_find_encoder_by_name("h264_amf");
    if (codec) return codec;
    return nullptr;
}

int hw_encoder_setup(AVCodecContext *enc_ctx, AVBufferRef **hw_device_ctx,
                      int width, int height, AVPixelFormat *out_hw_pix_fmt)
{
    int ret = av_hwdevice_ctx_create(hw_device_ctx, AV_HWDEVICE_TYPE_CUDA,
                                      nullptr, nullptr, 0);
    AVPixelFormat hw_pix_fmt = AV_PIX_FMT_CUDA;
    if (ret < 0) {
        ret = av_hwdevice_ctx_create(hw_device_ctx, AV_HWDEVICE_TYPE_D3D11VA,
                                      nullptr, nullptr, 0);
        hw_pix_fmt = AV_PIX_FMT_D3D11;
        if (ret < 0) return ret;
    }

    AVBufferRef *hw_frames_ref = av_hwframe_ctx_alloc(*hw_device_ctx);
    if (!hw_frames_ref) {
        av_buffer_unref(hw_device_ctx);
        return -1;
    }

    AVHWFramesContext *fctx = (AVHWFramesContext *)hw_frames_ref->data;
    fctx->format    = hw_pix_fmt;
    fctx->sw_format = AV_PIX_FMT_YUV420P;
    fctx->width     = width;
    fctx->height    = height;
    fctx->initial_pool_size = 20;

    ret = av_hwframe_ctx_init(hw_frames_ref);
    if (ret < 0) {
        av_buffer_unref(&hw_frames_ref);
        av_buffer_unref(hw_device_ctx);
        return ret;
    }

    enc_ctx->hw_frames_ctx = av_buffer_ref(hw_frames_ref);
    enc_ctx->pix_fmt = hw_pix_fmt;
    *out_hw_pix_fmt = hw_pix_fmt;

    av_buffer_unref(&hw_frames_ref);
    return 0;
}

AVFrame *hw_encoder_get_frame(AVCodecContext *enc_ctx)
{
    if (!enc_ctx || !enc_ctx->hw_frames_ctx) return nullptr;
    AVFrame *hw_frame = av_frame_alloc();
    if (!hw_frame) return nullptr;
    int ret = av_hwframe_get_buffer(enc_ctx->hw_frames_ctx, hw_frame, 0);
    if (ret < 0) {
        av_frame_free(&hw_frame);
        return nullptr;
    }
    return hw_frame;
}

void hw_encoder_cleanup(AVBufferRef **hw_device_ctx)
{
    if (*hw_device_ctx) {
        av_buffer_unref(hw_device_ctx);
        *hw_device_ctx = nullptr;
    }
}
