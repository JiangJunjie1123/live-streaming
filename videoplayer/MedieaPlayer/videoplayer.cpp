#include "videoplayer.h"
#include "cuda_helper.h"
#include<QDebug>
#include<QCoreApplication>
#include<iostream>



//AVFrame wanted_frame;
//PacketQueue audio_queue;
//int quit = 0;

//回调函数
void audio_callback(void *userdata, Uint8 *stream, int len);
//解码函数
int audio_decode_frame(VideoState *pcodec_ctx, uint8_t *audio_buf, int buf_size);
//找 auto_stream
int find_stream_index(AVFormatContext *pformat_ctx, int *video_stream, int *audio_stream);

#define FLUSH_DATA "FLUSH"

VideoPlayer::VideoPlayer()
{
    m_playerState = PlayerState::Stop;
    m_videoState.readThreadFinished = true; // 读线程初始为完成状态
    m_volume = SDL_MIX_MAXVOLUME; //默认最大音量 128

}

//时间补偿函数--视频延时
double synchronize_video(VideoState *is, AVFrame *src_frame, double pts) {
    double frame_delay; // 缓存帧和帧之间的延迟
    if (pts != 0) {
        /* if we have pts, set video clock to it */
        // 如果当前帧有PTS时间戳，那么使用它来更新视频时钟
        is->video_clock = pts;
    } else {
        /* if we aren't given a pts, set it to the clock */
        // 如果没有PTS时间戳，则采用视频时钟作为当前时间
        pts = is->video_clock;
    }
    /* update the video clock */
    // 计算当前帧和上一帧之间的时钟差
    frame_delay = av_q2d(is->pCodecCtx->time_base);
    /* if we are repeating a frame, adjust clock accordingly */
    // 如果当前帧是重复帧，需要根据重复数调整帧之间的时间差
    frame_delay += src_frame->repeat_pict * (frame_delay * 0.5);
    // 更新视频时钟
    is->video_clock += frame_delay;
    // 返回当前帧的PTS时间戳
    return pts;
}

//视频解码线程函数
int video_thread(void *arg)
{
    VideoState *is = (VideoState *) arg;
    AVPacket pkt1, *packet = &pkt1;
    int ret, got_picture, numBytes;
    double video_pts = 0; //当前视频的pts
    double audio_pts = 0; //音频pts
    ///解码视频相关
    AVFrame *pFrame, *pFrameRGB;
    uint8_t *out_buffer_rgb; //解码后的 rgb 数据
    struct SwsContext *img_convert_ctx;  //用于解码后的视频格式转换
    AVCodecContext *pCodecCtx = is->pCodecCtx; //视频解码器
    pFrame = av_frame_alloc();
    pFrameRGB = av_frame_alloc();
    AVFrame *sw_frame = nullptr;
    if (is->use_hw_decode) {
        sw_frame = av_frame_alloc();
    }
    ///这里我们改成了 将解码后的YUV数据转换成RGB32
    img_convert_ctx = sws_getContext(pCodecCtx->width, pCodecCtx->height,
                                     pCodecCtx->pix_fmt, pCodecCtx->width, pCodecCtx->height,
                                     AV_PIX_FMT_RGB32, SWS_BICUBIC, NULL, NULL, NULL);

    numBytes = avpicture_get_size(AV_PIX_FMT_RGB32,
                                  pCodecCtx->width,pCodecCtx->height);

    out_buffer_rgb = (uint8_t *) av_malloc(numBytes * sizeof(uint8_t));
    avpicture_fill((AVPicture *) pFrameRGB, out_buffer_rgb, AV_PIX_FMT_RGB32,
                   pCodecCtx->width, pCodecCtx->height);

    int empty_count = 0;
    const int MAX_EMPTY_COUNT = 1000; // 最大空队列等待次数
    
    while(1)
    {

        if(is->quit) break;
        if(is->isPause){
            SDL_Delay(5);
            continue;
        }
        //if (packet_queue_get(is->videoq, packet, 1) <= 0) break;//队列里面没有数据了  读取完毕了
        if (packet_queue_get(is->videoq, packet, 0) <= 0)
        {
            if( is->readFinished && is->audioq->nb_packets == 0)//播放到结束
            {//读线程完毕
                break;
            }else
            {
                SDL_Delay(1);
                empty_count++;
                // 防止无限等待，如果长时间没有数据则退出
                if (empty_count > MAX_EMPTY_COUNT && is->readFinished) {
                    qDebug() << "Video thread: max empty count reached, exiting";
                    break;
                }
                continue;
            }
            //只是队列里面暂时没有数据而已
        }
        empty_count = 0; // 重置计数
        if(strcmp((char*)packet->data,FLUSH_DATA) == 0)
        {
            avcodec_flush_buffers(is->pCodecCtx);
            av_packet_unref(packet);
            is->video_clock = 0;  //很关键 , 不清空 向左跳转, 视频帧会等待音频帧
            continue;
        }

        // 如果没有音频流，不需要同步，直接显示
        if (is->audioStream != -1) {
            int sync_wait_count = 0;
            const int MAX_SYNC_WAIT = 1000; // 最大等待5秒 (1000 * 5ms)
            while(1)
            {
                if(is->quit) break;
                if( is ->audioq->size == 0   ) break; //防止没声音无法退出同步
                audio_pts = is->audio_clock;
                video_pts = is->video_clock; //同步时发生跳转使用的
                // 如果音频时钟还没开始（为0），或者视频已经落后音频，就显示
                if (audio_pts == 0 || video_pts <= audio_pts) break;
                // 防止无限等待，添加超时机制
                sync_wait_count++;
                if (sync_wait_count > MAX_SYNC_WAIT) {
                    qDebug() << "Video sync timeout, forcing display";
                    break;
                }
                SDL_Delay(5);
            }
        }
        ret = avcodec_send_packet(pCodecCtx, packet);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Error sending packet for decoding\n");
            break;
        }
        ret = avcodec_receive_frame(pCodecCtx, pFrame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            av_packet_unref(packet);
            continue;
        } else if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Error receiving decoded frame\n");
            break;
        }

        AVFrame *display_frame = pFrame;
        if (is->use_hw_decode && pFrame->format == is->hw_pix_fmt) {
            av_hwframe_transfer_data(sw_frame, pFrame, 0);
            display_frame = sw_frame;
            // Recreate sws context if source format changed (HW→SW transfer changes pix_fmt)
            if (sw_frame->format != pCodecCtx->pix_fmt) {
                sws_freeContext(img_convert_ctx);
                img_convert_ctx = sws_getContext(pCodecCtx->width, pCodecCtx->height,
                                                 (AVPixelFormat)sw_frame->format,
                                                 pCodecCtx->width, pCodecCtx->height,
                                                 AV_PIX_FMT_RGB32, SWS_BICUBIC, NULL, NULL, NULL);
            }
        }

        got_picture = 1;
        //获取显示时间pts
        video_pts = pFrame->pts = pFrame->best_effort_timestamp;
        video_pts *= 1000000 *av_q2d(is->video_st->time_base);
        video_pts = synchronize_video(is, pFrame, video_pts);//视频时钟补偿

        if (is->seek_flag_video)
        {
            //发生了跳转 则跳过关键帧到目的时间的这几帧
            if (video_pts < is->seek_time)
            {
                av_packet_unref(packet);
                continue;
            }else
            {
                is->seek_flag_video = 0;
            }
        }

        if (got_picture) {
            sws_scale(img_convert_ctx,
                      (uint8_t const * const *) display_frame->data,
                      display_frame->linesize, 0, pCodecCtx->height, pFrameRGB->data,
                      pFrameRGB->linesize);

            //把这个RGB数据 用QImage加载
            QImage tmpImg((uchar
                           *)out_buffer_rgb,pCodecCtx->width,pCodecCtx->height,QImage::Format_RGB32);
            QImage image = tmpImg.copy(); //把图像复制一份 传递给界面显示 注意注释掉了
            is->m_player->SendGetOneImage(image); //调用激发信号的函数

        }
        av_packet_unref(packet);
    }
    if( !is->quit)
    {
        is->quit = true;
    }
    if (sw_frame)
        av_frame_free(&sw_frame);
    av_frame_free(&pFrame);
    av_frame_free(&pFrameRGB);
    av_free(out_buffer_rgb);
    sws_freeContext(img_convert_ctx);
    is->videoThreadFinished = true;

    // 清屏
    QImage img; //把图像复制一份 传递给界面显示
    img.fill(Qt::black);
    is->m_player->SendGetOneImage(img); //调用激发信号的函数

    return 0;
}

Uint32 timer_callback(Uint32 interval, void *param)
{
    VideoState *is = (VideoState *)param;
    AVPacket pkt1, *packet = &pkt1;
    int ret, got_picture, numBytes;
    double video_pts = 0; //当前视频的 pts
    double audio_pts = 0; //音频 pts
    ///解码视频相关
    AVFrame *pFrame, *pFrameRGB;
    uint8_t *out_buffer_rgb; //解码后的 rgb 数据
    struct SwsContext *img_convert_ctx; //用于解码后的视频格式转换
    AVCodecContext *pCodecCtx = is->pCodecCtx; //视频解码器
    pFrame = av_frame_alloc();
    pFrameRGB = av_frame_alloc();
    AVFrame *sw_frame = nullptr;
    if (is->use_hw_decode) {
        sw_frame = av_frame_alloc();
    }
    ///这里我们改成了 将解码后的 YUV 数据转换成 RGB32
    img_convert_ctx = sws_getContext(pCodecCtx->width, pCodecCtx->height,
                                     pCodecCtx->pix_fmt, pCodecCtx->width, pCodecCtx->height,
                                     AV_PIX_FMT_RGB32, SWS_BICUBIC, NULL, NULL, NULL);
    numBytes = avpicture_get_size(AV_PIX_FMT_RGB32,
                                  pCodecCtx->width,pCodecCtx->height);
    out_buffer_rgb = (uint8_t *) av_malloc(numBytes * sizeof(uint8_t));
    avpicture_fill((AVPicture *) pFrameRGB, out_buffer_rgb, AV_PIX_FMT_RGB32,
                   pCodecCtx->width, pCodecCtx->height);
    do
    {
        if (packet_queue_get(is->videoq, packet, 1) <= 0) break;//队列里面没有数据了
        //读取完毕了
        ret = avcodec_send_packet(pCodecCtx, packet);
        if (ret < 0) break;
        ret = avcodec_receive_frame(pCodecCtx, pFrame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            av_packet_unref(packet);
            continue;
        } else if (ret < 0) {
            break;
        }
        got_picture = 1;

        AVFrame *display_frame = pFrame;
        if (is->use_hw_decode && pFrame->format == is->hw_pix_fmt) {
            av_hwframe_transfer_data(sw_frame, pFrame, 0);
            display_frame = sw_frame;
        }

        //获取显示时间 pts
        video_pts = pFrame->pts = pFrame->best_effort_timestamp;
        video_pts *= 1000000 *av_q2d(is->video_st->time_base);
        video_pts = synchronize_video(is, pFrame, video_pts);//视频时钟补偿

        if (got_picture) {
            sws_scale(img_convert_ctx,
                      (uint8_t const * const *) display_frame->data,
                      display_frame->linesize, 0, pCodecCtx->height, pFrameRGB->data,
                      pFrameRGB->linesize);
            //把这个 RGB 数据 用 QImage 加载
            QImage tmpImg((uchar
                           *)out_buffer_rgb,pCodecCtx->width,pCodecCtx->height,QImage::Format_RGB32);
            QImage image = tmpImg.copy(); //把图像复制一份 传递给界面显示
            is->m_player->SendGetOneImage(image); //调用激发信号的函数

        }
        av_packet_unref(packet);
    }while(0);

    if (sw_frame)
        av_frame_free(&sw_frame);
    av_frame_free(&pFrame);
    av_frame_free(&pFrameRGB);
    av_free(out_buffer_rgb);
    sws_freeContext(img_convert_ctx);

    return interval;
}

//发送图片信号函数
void VideoPlayer::SendGetOneImage(QImage& img)
{
    emit SIG_getOneImage(img);  //发送信号
}

void VideoPlayer::play()
{
    //置位
    m_videoState.isPause = false;
    //维护状态
    if( m_playerState != Pause) return;
    m_playerState = Playing;
}

void VideoPlayer::pause()
{
    //置位
    m_videoState.isPause = true;
    //维护状态
    if( m_playerState != Playing ) return;
    m_playerState = Pause;
}

//跳转
void VideoPlayer::seek(int64_t pos) //精确到微秒
{
    // 如果正在 seek 中，忽略新的 seek 请求
    if (m_videoState.seeking) {
        return;
    }
    // 如果播放状态不是 Playing，忽略 seek
    if (m_playerState != Playing) {
        return;
    }
    // Live streams (totalTime == 0) cannot be seeked
    if (getTotalTime() <= 0) {
        return;
    }
    // 检查总时长，如果为0或负数（直播流），限制 seek 范围
    int64_t totalTime = getTotalTime();
    if (totalTime > 0) {
        // 限制 seek 位置在有效范围内
        if (pos < 0) pos = 0;
        if (pos > totalTime) pos = totalTime;
    }
    // 设置 seek 标志
    m_videoState.seeking = true;
    m_videoState.seek_pos = pos;
    m_videoState.seek_req = 1;
}

void VideoPlayer::stop(bool isWait)
{
    m_videoState.seeking = false; // 重置 seek 状态
    m_videoState.quit = 1;
    
    // 先关闭音频设备，避免音频回调还在运行
    if (m_videoState.audioID != 0)
    {
        SDL_LockAudio();
        SDL_PauseAudioDevice(m_videoState.audioID, 1); // 停止播放,即停止音频回调函数
        SDL_CloseAudioDevice(m_videoState.audioID);
        SDL_UnlockAudio();
        m_videoState.audioID = 0;
    }
    
    if (isWait) // 阻塞标志
    {
        int waitCount = 0;
        const int MAX_WAIT_COUNT = 500; // 最大等待5秒 (500 * 10ms)
        while (!m_videoState.readThreadFinished) // 等待读取线程退出（也覆盖纯音频流）
        {
            if (waitCount >= MAX_WAIT_COUNT)
            {
                qDebug() << "Stop wait timeout, forcing exit";
                break;
            }
            SDL_Delay(10);
            waitCount++;
        }
    }
    
    m_playerState = PlayerState::Stop;
    Q_EMIT SIG_PlayerStateChanged(PlayerState::Stop);
}

PlayerState VideoPlayer::playerState() const
{
    return m_playerState;
}

#define MAX_AUDIO_SIZE (1024*16*25*2) // 音频阈值 (~800KB，约1秒缓冲)
#define MAX_VIDEO_SIZE (1024*128*25*2) // 视频阈值 (~6.4MB，约1秒缓冲@25fps)
// 当队列里面的数据超过某个大小的时候 就暂停读取 防止一下子就把视频读完了，导致的空间分配不足
void VideoPlayer::run()
{
    qDebug() << "VideoPlayer::" << __func__ ;
    qDebug() << "File name:" << m_fileName;
//    QString path = QCoreApplication::applicationDirPath() + "/images/";
//    qDebug()<<path;
//    //循环获取图片  exe路径
//    for(int i=0;i<23;i++){
//    QString tmp = QString("%1%2.png").arg(path).arg(i);
//    //发送信号->图片
//    Q_EMIT SIG_getOneImage(QImage(tmp));
//    QThread::msleep(100);
//    }

    //音频
    //添加音频需要的变量
    int audioStream = -1; // 音频解码器需要的流的索引
    AVCodecContext *pAudioCodecCtx = NULL; // 音频解码器信息指针
    AVCodec *pAudioCodec = NULL; // 音频解码器
    //SDL
    SDL_AudioSpec wanted_spec; //SDL 音频设置
    SDL_AudioSpec spec; //SDL 音频设置



    //视频
    int videoStream = -1;
    //视频
    AVCodecContext    *pCodecCtx = nullptr;  //视频的解码器信息指针
    AVCodec    *pCodec    ; //视频解码器
    AVPacket    *packet = nullptr; //读取解码前的包
//    AVFrame    *pFrame,   *pFrameRGB; // 用来存解码后的数据
//    int numBytes; //帧数据大小
//    uint8_t * out_buffer; //存储转化为 RGB 格式数据的缓冲区
//    struct SwsContext *img_convert_ctx; //YUV 转 RGB 的结构


    int ret, got_picture;
    int DelayCount = 0;
    int maxDelayCount = 300;
    AVDictionary *opts = NULL;
    std::string path;
    const char* file_path = nullptr;

    //1.初始化
    //首先需要先初始化一下，使用如下函数：
    //1.初始化FFMPEG  调用了这个才能正常适用编码器和解码器 注册所用函数
    av_register_all();
    qDebug() << "FFmpeg initialized";

    // 将 pFormatCtx 声明提前，以便 error_cleanup 可以访问
    AVFormatContext *pFormatCtx = nullptr;

    //在ffmpeg 初始化之后, 添加SDL初始化
    //SDL 初始化
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER))
    {
        std::cerr << "ERROR: Couldn't init SDL: " << SDL_GetError() << std::endl;
        qDebug()<< "Couldn't init SDL: " << SDL_GetError() ;
        goto error_cleanup;
    }
    qDebug() << "SDL initialized";

    // Use proper C++ reset instead of memset to preserve sentinel values
    m_videoState = VideoState();
    m_videoState.readThreadFinished = false;  // ensure false during thread execution

    //使用这个函数完成编码器和解码器的初始化，只有初始化了编码器和解码器才能正常使用，否则会在
    //打开编解码器的时候失败。
    //2.分配AVFormatContext
    //接着需要分配一个AVFormatContext，FFMPEG所有操作都要通过AVFormatContext来进行.
    //2.需要分配一个AVFormatContext，FFMPEG 所有的操作都要通过这个AVFormatContext来进行 可
    //以理解为视频文件指针
    qDebug() << "Allocating format context...";
    pFormatCtx = avformat_alloc_context();
    qDebug() << "Format context allocated";
    
    //设置网络超时参数，仅对网络流生效，本地文件不需要
    opts = NULL;
    if (m_fileName.contains("://")) {
        av_dict_set(&opts, "rtsp_transport", "tcp", 0);
        av_dict_set(&opts, "stimeout", "10000000", 0); // 10秒超时（微秒）
        av_dict_set(&opts, "max_delay", "500000", 0); // 最大延迟500ms
        av_dict_set(&opts, "buffer_size", "65536", 0); // 增大缓冲区
        // HLS直播流设置：从头开始播放，不跳转到最新位置
        av_dict_set(&opts, "live_start_index", "0", 0);
        av_dict_set(&opts, "hls_allow_cache", "1", 0);
        av_dict_set(&opts, "hls_playlist_load_attempts", "3", 0);
        // HLS seek 支持
        av_dict_set(&opts, "seekable", "1", 0);
        av_dict_set(&opts, "enable_dash", "0", 0);

        // Adjust settings for RTMP live streams (not VOD)
        if (m_fileName.contains("rtmp://") && !m_fileName.contains("/vod/")) {
            av_dict_set(&opts, "seekable", "0", 0);  // Live RTMP is NOT seekable
            av_dict_set(&opts, "max_delay", "2000000", 0);  // 2 seconds
            av_dict_set(&opts, "buffer_size", "262144", 0); // 256 KB
        }
    }

    //3.打开视频文件并获取信息
    //接着调用打开视频文件
    //中文兼容
    path = m_fileName.toStdString();
    file_path = path.c_str();
    qDebug() << "Opening file:" << file_path;
    //打开视频文件
    //3. 打开视频文件
    if( avformat_open_input(&pFormatCtx, file_path, NULL, &opts) != 0 )
    {
        std::cerr << "ERROR: can't open file: " << file_path << std::endl;
        qDebug()<<"can't open file";
        av_dict_free(&opts);
        goto error_cleanup;
    }
    av_dict_free(&opts);
    qDebug() << "File opened, streams:" << pFormatCtx->nb_streams;
    //3.1 获取视频文件信息
    if (avformat_find_stream_info(pFormatCtx, NULL) < 0)
    {
        qDebug()<<"Could't find stream infomation.";
        goto error_cleanup;
    }
    //4.查找文件中的视频流
    //循环查找视频中包含的流信息，直到找到视频类型的流
    //便将其记录下来 保存到videoStream变量中
    //这里我们现在只处理视频流  音频流先不管他
    //4.读取视频流
//    int videoStream = -1;
//    int i;
//    for (  i = 0; i < pFormatCtx->nb_streams; i++) {
//    if (pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO)
//    {
//    videoStream = i;
//    }
//    }
    //查找音频视频流索引
    qDebug() << "Calling find_stream_index...";
    if (find_stream_index(pFormatCtx, &videoStream, &audioStream) == -1)
    {
        std::cerr << "ERROR: Couldn't find stream index in file: " << file_path << std::endl;
        qDebug()<<"Couldn't find stream index" ;
        goto error_cleanup;
    }
    qDebug() << "find_stream_index returned, videoStream:" << videoStream << "audioStream:" << audioStream;

    qDebug() << "Setting VideoState...";
    m_videoState.pFormatCtx = pFormatCtx;
    m_videoState.videoStream = videoStream;
    m_videoState.audioStream = audioStream;
    m_videoState.m_player = this;

    qDebug() << "Allocating packet...";
    packet = (AVPacket *) malloc(sizeof(AVPacket)); //分配一个 packet

    qDebug() << "Finding video decoder...";

    //如果videoStream 为-1 说明没有找到视频流
    //视频
    if(videoStream != -1){
        const AVCodecParameters *codecpar = pFormatCtx->streams[videoStream]->codecpar;
        pCodec = avcodec_find_decoder(codecpar->codec_id);
        if (pCodec == NULL) {
            qDebug()<< "Video Codec not found." ;
            goto error_cleanup;
        }
        qDebug() << "Video codec found:" << pCodec->name;

        pCodecCtx = avcodec_alloc_context3(pCodec);
        if (!pCodecCtx) {
            qDebug() << "Failed to allocate decoder context";
            goto error_cleanup;
        }
        avcodec_parameters_to_context(pCodecCtx, codecpar);

        // HW decode attempt (TEMP: if (0 && ...) to force SW decode)
        m_videoState.use_hw_decode = false;
        m_videoState.hw_pix_fmt = AV_PIX_FMT_NONE;
        m_videoState.hw_device_ctx = nullptr;

        AVHWDeviceType hw_type;
        if (0 && hw_device_init(&m_videoState.hw_device_ctx, &hw_type) >= 0) {
            hw_decoder_check_support(pCodec, hw_type, &m_videoState.hw_pix_fmt);
            if (m_videoState.hw_pix_fmt != AV_PIX_FMT_NONE) {
                pCodecCtx->hw_device_ctx = av_buffer_ref(m_videoState.hw_device_ctx);
                pCodecCtx->opaque = &m_videoState.hw_pix_fmt;
                pCodecCtx->get_format = get_hw_format;
                m_videoState.use_hw_decode = true;
                qDebug() << "HW decode enabled";
            } else {
                av_buffer_unref(&m_videoState.hw_device_ctx);
                qDebug() << "Decoder does not support HW, falling back to SW";
            }
        } else {
            qDebug() << "No HW device, using SW decode";
        }

        if(avcodec_open2(pCodecCtx, pCodec, NULL) < 0) {
            qDebug()<< "Could not open video codec." ;
            avcodec_free_context(&pCodecCtx);
            goto error_cleanup;
        }
        qDebug() << "Video codec opened, HW:" << m_videoState.use_hw_decode;
        m_videoState.video_st = pFormatCtx->streams[ videoStream ];
        m_videoState.pCodecCtx = pCodecCtx;
        //视频同步队列
        m_videoState.videoq = new PacketQueue;
        packet_queue_init( m_videoState.videoq);
        qDebug() << "Creating video thread...";
        //创建视频线程 - 无论是否有音频流都要创建视频线程
        m_videoState.video_tid = SDL_CreateThread( video_thread , "video_thread" , &m_videoState );
        qDebug() << "Video thread created";

    }
    qDebug() << "Initializing audio...";
    //音频
    if (audioStream != -1) {
        qDebug() << "Audio stream found, initializing audio...";
        //音频
        //找到对应的音频解码器
        pAudioCodecCtx = pFormatCtx->streams[audioStream]->codec;
        pAudioCodec = avcodec_find_decoder(pAudioCodecCtx ->codec_id);
        if (!pAudioCodec)
        {
            qDebug()<< "Couldn't find decoder";
            goto error_cleanup;
        }
        //打卡音频解码器
        qDebug() << "Opening audio codec...";
        avcodec_open2(pAudioCodecCtx, pAudioCodec, NULL);
        qDebug() << "Audio codec opened";

        m_videoState.audio_st = pFormatCtx->streams[audioStream];
        m_videoState.pAudioCodecCtx = pAudioCodecCtx;
        //6.设置音频信息, 用来打开音频设备。
        SDL_LockAudio();
        wanted_spec.freq
                = pAudioCodecCtx->sample_rate;
        switch (pFormatCtx->streams[audioStream]->codec->sample_fmt)
        {
        case AV_SAMPLE_FMT_U8:
            wanted_spec.format = AUDIO_S8;
            break;
        case AV_SAMPLE_FMT_S16:
            wanted_spec.format = AUDIO_S16SYS;
            break;
        default:
            wanted_spec.format = AUDIO_S16SYS;
            break;
        };
        //7. 设置音频信息，用来打开音频设备
        //设置音频信息，用来打开音频设备。
        wanted_spec.channels   = pAudioCodecCtx->channels;    //通道数
        wanted_spec.silence    = 0;    //设置静音值
        wanted_spec.samples    = SDL_AUDIO_BUFFER_SIZE;    //采样点
        wanted_spec.callback   = audio_callback; //回调函数
        wanted_spec.userdata   = &m_videoState; //回调函数参数
        //8. 打开音频设备
        //打开音频设备
        qDebug() << "Opening audio device...";
        m_videoState.audioID = SDL_OpenAudioDevice( NULL ,0,&wanted_spec, &spec,0);
        if( m_videoState.audioID < 0 ) //第二次打开 audio 会返回-1
        {
            qDebug() << "Couldn't open Audio: " << SDL_GetError();
            goto error_cleanup;
        }
        qDebug() << "Audio device opened";
        //设置参数，供解码时候用, swr_alloc_set_opts的in部分参数
        switch (pFormatCtx->streams[audioStream]->codec->sample_fmt)
        {
        case AV_SAMPLE_FMT_U8:
            m_videoState.out_frame.format = AV_SAMPLE_FMT_U8;
            break;
        case AV_SAMPLE_FMT_S16:
            m_videoState.out_frame.format = AV_SAMPLE_FMT_S16;
            break;
        default:
            m_videoState.out_frame.format = AV_SAMPLE_FMT_S16;
            break;
        };
        m_videoState.out_frame.sample_rate    = pAudioCodecCtx->sample_rate;
        m_videoState.out_frame.channel_layout =
                av_get_default_channel_layout(pAudioCodecCtx->channels);
        m_videoState.out_frame.channels       = pAudioCodecCtx->channels;

        m_videoState.audioq = new PacketQueue;
        //初始化队列
        qDebug() << "Initializing audio queue...";
        packet_queue_init(m_videoState.audioq);
        m_videoState.audioFrame = av_frame_alloc();
        SDL_UnlockAudio();
        // SDL播放声音 0播放
        qDebug() << "Starting audio playback...";
        SDL_PauseAudioDevice(m_videoState.audioID,0);
        qDebug() << "Audio playback started";

    }

    qDebug() << "Starting main loop...";
    Q_EMIT SIG_TotalTime(getTotalTime());
//    int64_t start_time = av_gettime();
//    int64_t pts = 0; //当前视频帧的pts
    //8.循环读取视频
    //8.循环读取视频帧, 转换为RGB格式, 抛出信号去控件显示
    ret = 0;
    got_picture = 0;
    DelayCount = 0;
    maxDelayCount = (getTotalTime() <= 0) ? 3000 : 300;  // 30s for live, 3s for VOD
    while(1)
    {
        if( m_videoState.quit ) break;
        //这里做了个限制  当队列里面的数据超过某个大小的时候 就暂停读取  防止一下子就把视频读完了，导致的空间分配不足
        /* 这里audioq.size是指队列中的所有数据包带的音频数据的总量或者视频数据总量，并
        不是包的数量 */
        //这个值可以稍微写大一些
        if( m_videoState.audioStream != -1 && m_videoState.audioq->size >
                MAX_AUDIO_SIZE ) {
            SDL_Delay(10);
            continue;
        }
        if ( m_videoState.videoStream != -1 &&m_videoState.videoq->size > MAX_VIDEO_SIZE) {
            SDL_Delay(10);
            continue;
        }

        //跳转

        if( m_videoState.seek_req )
            // 跳转标志位seek_req --> 1 清除队列里的缓存 3s --> 3min 3s里面的数据 存在 队列和解码器
            // 3s在解码器里面的数据和3min的会合在一起 引起花屏 --> 解决方案 清理解码器缓存 AV_flush_...
            //什么时候清理 -->要告诉它 , 所以要来标志包 FLUSH_DATA "FLUSH"
            //关键帧--比如10秒 --> 15秒 跳转关键帧 只能是10 或15 , 如果你要跳到13 , 做法是跳到10 然后10-13的包全扔掉
        {
            int stream_index = -1;
            int64_t seek_target = m_videoState.seek_pos;//微秒

            if (m_videoState.videoStream >= 0)
                stream_index = m_videoState.videoStream;
            else if (m_videoState.audioStream >= 0)
                stream_index = m_videoState.audioStream;

            AVRational aVRational = {1, AV_TIME_BASE};
            if (stream_index >= 0) {
                seek_target = av_rescale_q(seek_target, aVRational,
                                           pFormatCtx->streams[stream_index]->time_base); //跳转到的位置
            }
            // HLS流可能不支持精确seek，尝试多种方式
            // 首先尝试使用字节 seek（对HLS更友好）
            int seek_flags = AVSEEK_FLAG_BACKWARD;
            int seek_ret = av_seek_frame(m_videoState.pFormatCtx, stream_index, seek_target, seek_flags);
            if (seek_ret < 0) {
                // seek失败，尝试不指定flag
                seek_ret = av_seek_frame(m_videoState.pFormatCtx, stream_index, seek_target, 0);
                if (seek_ret < 0) {
                    // 尝试使用字节方式seek
                    seek_ret = av_seek_frame(m_videoState.pFormatCtx, -1, seek_target, AVSEEK_FLAG_BYTE);
                    if (seek_ret < 0) {
                        fprintf(stderr, "%s: seek failed\n", m_videoState.pFormatCtx->filename);
                        // HLS流可能不支持seek，直接清空队列继续播放
                    }
                }
            }
            
            // 无论seek是否成功，都清空队列
            if (m_videoState.audioStream >= 0) {
                AVPacket *packet = (AVPacket *) malloc(sizeof(AVPacket)); //分配一个 packet
                av_new_packet(packet, 10);
                strcpy((char*)packet->data, FLUSH_DATA);
                packet_queue_flush(m_videoState.audioq); //清除队列
                packet_queue_put(m_videoState.audioq, packet); //往队列中存入用来清除的包
            }
            if (m_videoState.videoStream >= 0) {
                AVPacket *packet = (AVPacket *) malloc(sizeof(AVPacket)); //分配一个 packet
                av_new_packet(packet, 10);
                strcpy((char*)packet->data, FLUSH_DATA);
                packet_queue_flush(m_videoState.videoq); //清除队列
                packet_queue_put(m_videoState.videoq, packet); //往队列中存入用来清除的包
                m_videoState.video_clock = 0; //考虑到向左快退  避免卡死
            }
            m_videoState.seek_req = 0;
            m_videoState.seek_time = m_videoState.seek_pos ; //精确到微妙 seek_time 是用来做视频音频的时钟调整 --关键帧
            m_videoState.seek_flag_audio = 1; //在视频音频循环中 , 判断, AVPacket 是FLUSH_DATA 清空解码器缓存
            m_videoState.seek_flag_video = 1;
            m_videoState.seeking = false; // seek 完成，允许下一次 seek
        }
        
        // 如果 seek 没有被处理（比如没有进入 if 块），也重置 seeking 标志
        if (m_videoState.seeking) {
            m_videoState.seeking = false;
        }

        //可以看出 av_read_frame读取的是一帧视频，并存入一个AVPacket的结构中
        int read_ret = av_read_frame(pFormatCtx, packet);
        if (read_ret < 0)
        {
            // 检查是否是直播流，直播流可能会间歇性没有数据
            char errbuf[256];
            av_strerror(read_ret, errbuf, sizeof(errbuf));
            qDebug() << "av_read_frame error:" << errbuf;
            
            DelayCount++;
            if( DelayCount>= maxDelayCount)
            {
                m_videoState.readFinished = true;
                DelayCount = 0 ;
            }
            if( m_videoState.quit) break; //解码线程执行完 退出
            SDL_Delay(10);
            continue;
        }
        DelayCount = 0;

        //生成图片
        if (packet->stream_index == m_videoState.videoStream)
        {
            packet_queue_put(m_videoState.videoq, packet);
        }
        else if ( packet->stream_index == m_videoState.audioStream)
        {
            packet_queue_put(m_videoState.audioq, packet);
        }
        else
        {
            av_packet_unref(packet);
        }
    }

    //9.回收数据
    while( !m_videoState.quit)
    {
        SDL_Delay(100);
    }
    //回收空间
    if( m_videoState.videoStream != -1)
        packet_queue_flush( m_videoState.videoq);//队列回收
    if( m_videoState.audioStream != -1)
        packet_queue_flush( m_videoState.audioq); //队列回收

    while( m_videoState.videoStream != -1 && !m_videoState.videoThreadFinished )
    {
        SDL_Delay(10);
    }


    //回收空间
    if( audioStream != -1)
    {
        //回收空间
        avcodec_close(pAudioCodecCtx);
    }
    //9.回收数据
    if( videoStream != -1 && pCodecCtx )
    {
        avcodec_free_context(&pCodecCtx);
    }
    if (m_videoState.hw_device_ctx)
        av_buffer_unref(&m_videoState.hw_device_ctx);
    avformat_close_input(&pFormatCtx);
    //回收资源之后,在最后添加读取文件线程退出标志.
    m_videoState.readThreadFinished = true;
    //视频自动结束 置标志位
    m_playerState = PlayerState::Stop;
    return;

error_cleanup:
    m_videoState.quit = 1;

    // Close audio device if opened
    if (m_videoState.audioID != 0) {
        SDL_LockAudio();
        SDL_PauseAudioDevice(m_videoState.audioID, 1);
        SDL_CloseAudioDevice(m_videoState.audioID);
        SDL_UnlockAudio();
        m_videoState.audioID = 0;
    }

    // Give threads time to notice quit flag
    SDL_Delay(100);

    // Close codecs if opened
    if (videoStream != -1 && pCodecCtx) {
        avcodec_free_context(&pCodecCtx);
    }
    if (m_videoState.hw_device_ctx)
        av_buffer_unref(&m_videoState.hw_device_ctx);
    if (audioStream != -1 && pAudioCodecCtx) {
        avcodec_close(pAudioCodecCtx);
    }

    // Close format context
    if (pFormatCtx) {
        avformat_close_input(&pFormatCtx);
    }

    // Free packet (malloc'd)
    if (packet) {
        av_packet_unref(packet);
        free(packet);
        packet = nullptr;
    }

    // Free audioFrame
    if (m_videoState.audioFrame) {
        av_frame_free(&m_videoState.audioFrame);
        m_videoState.audioFrame = nullptr;
    }

    m_videoState.readThreadFinished = true;
    m_playerState = PlayerState::Stop;
    Q_EMIT SIG_PlayerStateChanged(PlayerState::Stop);
}

void VideoPlayer::setFileName(const QString &newFileName)
{
    if( m_playerState != PlayerState::Stop  )   return;
    m_fileName = newFileName;
    m_playerState = PlayerState::Playing;

}

//获取当前时间
double VideoPlayer::getCurrentTime()
{
    return m_videoState.audio_clock;
}
//获取总时间
int64_t VideoPlayer::getTotalTime()
{
    if( m_videoState.pFormatCtx ) {
        // 首先尝试获取流的 duration
        int64_t duration = m_videoState.pFormatCtx->duration;
        
        // 如果格式级别的 duration 无效，尝试从视频流获取
        if (duration <= 0 || duration == AV_NOPTS_VALUE) {
            if (m_videoState.videoStream >= 0) {
                AVStream *stream = m_videoState.pFormatCtx->streams[m_videoState.videoStream];
                if (stream->duration > 0 && stream->duration != AV_NOPTS_VALUE) {
                    // 将流的时间基转换为微秒
                    duration = av_rescale_q(stream->duration, stream->time_base, AV_TIME_BASE_Q);
                    return duration;
                }
            }
            // 尝试从音频流获取
            if (m_videoState.audioStream >= 0) {
                AVStream *stream = m_videoState.pFormatCtx->streams[m_videoState.audioStream];
                if (stream->duration > 0 && stream->duration != AV_NOPTS_VALUE) {
                    duration = av_rescale_q(stream->duration, stream->time_base, AV_TIME_BASE_Q);
                    return duration;
                }
            }
            return 0; // 直播流返回 0
        }
        return duration;
    }
    return -1;
}

//设置音量
void VideoPlayer::setVolume(int volume)
{
    //音量范围 0-100 转换为 SDL 的 0-128
    if(volume < 0) volume = 0;
    if(volume > 100) volume = 100;
    m_volume = (volume * SDL_MIX_MAXVOLUME) / 100;
}

//获取音量
int VideoPlayer::getVolume() const
{
    return (m_volume * 100) / SDL_MIX_MAXVOLUME;
}

//13.回调函数中将从队列中取数据，解码后填充到播放缓冲区.
//13.回调函数中将从队列中取数据, 解码后填充到播放缓冲区.
void audio_callback(void *userdata, Uint8 *stream, int len)
{
    //
    AVCodecContext *pcodec_ctx   = (AVCodecContext *) userdata;
    VideoState * is = (VideoState *) userdata;
    int len1, audio_data_size;

    memset( stream , 0 , len);
    if(is->isPause ) return;

    //
    //
    //
    /*
static uint8_t audio_buf[(AVCODEC_MAX_AUDIO_FRAME_SIZE * 3) / 2];
static unsigned int audio_buf_size = 0;
static unsigned int audio_buf_index = 0;
len 是由SDL传入的SDL缓冲区的大小，如果这个缓冲未满，我们就一直往里填充数据 */
    /*  audio_buf_index 和 audio_buf_size 标示我们自己用来放置解码出来的数据的缓冲区，*/
    /*
这些数据待copy到SDL缓冲区， 当audio_buf_index >= audio_buf_size的时候意味着我*/
    /*
/*
们的缓冲为空，没有数据可供copy，这时候需要调用audio_decode_frame来解码出更
多的桢数据 */
    while (len > 0)
    {
        if (is->audio_buf_index >= is->audio_buf_size) {
            audio_data_size = audio_decode_frame( is ,
                                                  is->audio_buf,sizeof(is->audio_buf));
            /* audio_data_size < 0 标示没能解码出数据，我们默认播放静音 */
            if (audio_data_size < 0) {
                /* silence */
                is->audio_buf_size = 1024;
                /* 清零，静音 */
                memset(is->audio_buf, 0, is->audio_buf_size);
            } else {
                is->audio_buf_size = audio_data_size;
            }
            is->audio_buf_index = 0;
        }
        /*  查看stream可用空间，决定一次copy多少数据，剩下的下次继续copy */
        len1 = is->audio_buf_size - is->audio_buf_index;
        if (len1 > len) {
            len1 = len;
        }
        memset( stream , 0 , len1);
        //混音函数 sdl 2.0 版本使用该函数 替换 SDL_MixAudio
        //最后一个参数是音量 0-128，使用 m_volume 来控制
        SDL_MixAudioFormat(stream, (uint8_t *) is->audio_buf + is->audio_buf_index,
                           AUDIO_S16SYS, len1, is->m_player->getVolume() * SDL_MIX_MAXVOLUME / 100);
        len -= len1;
        stream += len1;
        is->audio_buf_index += len1;
    }
}

//对于音频来说，一个packet里面，可能含有多帧(frame)数据。
//解码音频帧函数
int audio_decode_frame(VideoState *is, uint8_t *audio_buf, int buf_size)
{
    AVPacket pkt;
    uint8_t *audio_pkt_data = NULL;
    int audio_pkt_size = 0;
    int len1, data_size;
    int sampleSize = 0;
    AVCodecContext *aCodecCtx = is->pAudioCodecCtx;
    AVFrame *audioFrame = is->audioFrame/*av_frame_alloc()*/;
    PacketQueue *audioq = is->audioq;
    AVFrame wanted_frame = is->out_frame;
    if( !aCodecCtx|| !audioFrame ||!audioq) return -1;
    /*static*/ struct SwrContext *swr_ctx = NULL;
    int convert_len;
    int n = 0;
    int retry_count = 0;
    const int MAX_RETRY = 100; // 最大重试次数
    for(;;)
    {
        if( is->quit ) return -1;
        if(is->isPause) return -1;
        if( !audioq ) return -1;
        if(packet_queue_get(audioq, &pkt, 0) <= 0) //一定注意
        {
            if( is->readFinished && is->audioq->nb_packets == 0 )
                is->quit = true;
            // 如果队列空但还在播放，返回静音数据而不是-1
            retry_count++;
            if (retry_count < MAX_RETRY) {
                SDL_Delay(10);
                continue;
            }
            return -1;
        }
        retry_count = 0; // 重置重试计数
        if(strcmp((char*)pkt.data,FLUSH_DATA) == 0)
        {
            avcodec_flush_buffers(is->audio_st->codec);
            av_packet_unref(&pkt);
            continue;
        }
        audio_pkt_data = pkt.data;
        audio_pkt_size = pkt.size;
        while(audio_pkt_size > 0)
        {
            if( is->quit ) return -1;
            int got_picture;
            memset(audioFrame, 0, sizeof(AVFrame));
            int ret = avcodec_send_packet(aCodecCtx, &pkt);
            if (ret < 0) {
                printf("Error sending audio packet for decoding.\n");
                av_packet_unref(&pkt);
                return -1;
            }
            ret = avcodec_receive_frame(aCodecCtx, audioFrame);
            if (ret == 0) {
                got_picture = 1;
                audio_pkt_size = 0;
            } else if (ret == AVERROR(EAGAIN)) {
                av_packet_unref(&pkt);
                continue;
            } else {
                printf("Error receiving decoded audio frame.\n");
                av_packet_unref(&pkt);
                return -1;
            }
            //一帧一个声道读取数据字节数是 nb_samples , channels 为声道数 2 表示 16 位 2 个字节
            //data_size = audioFrame->nb_samples * wanted_frame.channels * 2;
            switch( is->out_frame.format )
            {
            case AV_SAMPLE_FMT_U8:
                data_size = audioFrame->nb_samples * is->out_frame.channels * 1;
                break;
            case AV_SAMPLE_FMT_S16:
                data_size = audioFrame->nb_samples * is->out_frame.channels * 2;
                break;
            default:
                data_size = audioFrame->nb_samples * is->out_frame.channels * 2;
                break;
            }
            //计算音频时钟
            if( pkt.pts != AV_NOPTS_VALUE)
            {
                is->audio_clock = pkt.pts *av_q2d( is->audio_st->time_base )*1000000 ;
                //取音频时钟
            }else
            {
                is->audio_clock = (*(uint64_t *)
                                   audioFrame->opaque)*av_q2d( is->audio_st->time_base )*1000000 ;
            }

            //跳转到关键帧,跳过一些帧
            if( is->seek_flag_audio)
            {
                if( is ->audio_clock < is->seek_time) //没有到目的时间
                {
                    break;
                }
                else
                {
                    is->seek_flag_audio = 0 ;
                }
            }

            if( got_picture )
            {

                swr_ctx = swr_alloc_set_opts(NULL, wanted_frame.channel_layout,

                                             (AVSampleFormat)wanted_frame.format,wanted_frame.sample_rate,

                                             audioFrame->channel_layout,(AVSampleFormat)audioFrame->format,
                                             audioFrame->sample_rate, 0, NULL);
                //初始化
                if (swr_ctx == NULL || swr_init(swr_ctx) < 0)
                {
                    printf("swr_init error\n");
                    break;
                }
                convert_len = swr_convert(swr_ctx, &audio_buf,
                                          AVCODEC_MAX_AUDIO_FRAME_SIZE,
                                          (const uint8_t **)audioFrame->data,
                                          audioFrame->nb_samples);
                swr_free( &swr_ctx );
            }
            audio_pkt_size -= ret;
            if (audioFrame->nb_samples <= 0)
            {
                continue;
            }
            av_packet_unref(&pkt); //新版考虑使用av_packet_unref() 函数来代替
            return data_size ;
        }
        av_packet_unref(&pkt); //新版考虑使用av_packet_unref() 函数来代替
    }
}
//查找数据流函数
int find_stream_index(AVFormatContext *pformat_ctx, int *video_stream, int *audio_stream)
{
    assert(video_stream != NULL || audio_stream != NULL);

    int i = 0;
    int audio_index = -1;
    int video_index = -1;

    for (i = 0; i < pformat_ctx->nb_streams; i++)
    {
        qDebug() << "Stream" << i << "codec type:" << pformat_ctx->streams[i]->codecpar->codec_type;
        if (pformat_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            video_index = i;
            qDebug() << "Found VIDEO stream at index" << i;
        }
        if (pformat_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
        {
            audio_index = i;
            qDebug() << "Found AUDIO stream at index" << i;
        }
    }

    qDebug() << "video_index:" << video_index << "audio_index:" << audio_index;

    if (video_stream != NULL)
    {
        *video_stream = video_index;
    }

    if (audio_stream != NULL)
    {
        *audio_stream = audio_index;
    }

    if (video_index == -1 && audio_index == -1)
    {
        qDebug() << "No streams found!";
        return -1;
    }

    qDebug() << "find_stream_index success";
    return 0;
}
