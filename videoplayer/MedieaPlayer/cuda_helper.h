#ifndef CUDA_HELPER_H
#define CUDA_HELPER_H

extern "C" {
#include "libavcodec/avcodec.h"
#include "libavutil/hwcontext.h"
#include "libavutil/pixfmt.h"
}

// 按优先级尝试创建 HW 设备上下文 (CUDA → D3D11VA → NONE)
// 成功返回 >= 0，失败返回 < 0
int hw_device_init(AVBufferRef **hw_device_ctx, AVHWDeviceType *out_type);

// 检查解码器是否支持指定 HW 类型，返回匹配的 HW 像素格式
void hw_decoder_check_support(const AVCodec *decoder, AVHWDeviceType hw_type,
                               AVPixelFormat *hw_pix_fmt);

// get_format 回调：让解码器输出 HW 像素格式
AVPixelFormat get_hw_format(AVCodecContext *ctx, const AVPixelFormat *pix_fmts);

#endif
