#ifndef CUDA_HELPER_H
#define CUDA_HELPER_H

extern "C" {
#include "libavcodec/avcodec.h"
#include "libavutil/hwcontext.h"
#include "libavutil/pixfmt.h"
}

// 选择合适的硬件编码器 (h264_nvenc → h264_amf → nullptr)
AVCodec *hw_encoder_select();

// 为硬件编码器创建 hw_device_ctx 和 hw_frames_ctx
// 成功返回 >= 0，失败返回 < 0
// out_hw_pix_fmt 返回实际使用的硬件像素格式 (CUDA 或 D3D11)
int hw_encoder_setup(AVCodecContext *enc_ctx, AVBufferRef **hw_device_ctx,
                      int width, int height, AVPixelFormat *out_hw_pix_fmt);

// 获取一个 GPU 帧用于编码输入
AVFrame *hw_encoder_get_frame(AVCodecContext *enc_ctx);

// 清理 HW 编码资源
void hw_encoder_cleanup(AVBufferRef **hw_device_ctx);

#endif
