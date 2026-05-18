#ifndef PACKETQUEUE_H
#define PACKETQUEUE_H

#ifdef __cplusplus
extern "C"{
#endif

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
#include <SDL.h>

#ifdef __cplusplus
}
#endif

typedef struct PacketQueue
{
    AVPacketList *first_pkt;     //队头packet 注意类型不是AVPacket
    AVPacketList *last_pkt;      //队尾packet
    int nb_packets;              //paket 个数
    int size;                    //
    SDL_mutex *mutex;            //
    SDL_cond *cond;              //条件变量
}PacketQueue;

void packet_queue_init(PacketQueue *queue);
int packet_queue_put(PacketQueue *queue, AVPacket *packet);
int packet_queue_get(PacketQueue *queue, AVPacket *pkt, int block);
void packet_queue_flush(PacketQueue *q);

#endif // PACKETQUEUE_H
