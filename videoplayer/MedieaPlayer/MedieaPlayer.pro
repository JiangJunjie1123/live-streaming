QT       += core gui network

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++11

# 引入 OpenGL 模块
include($$PWD/opengl/opengl/opengl.pri)

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    main.cpp \
    playerdialog.cpp \
    videoplayer.cpp \
    PacketQueue.cpp \
    cuda_helper.cpp \
    danmaku_overlay.cpp

HEADERS += \
    playerdialog.h \
    videoplayer.h \
    PacketQueue.h \
    cuda_helper.h \
    danmaku_overlay.h

FORMS += \
    playerdialog.ui

INCLUDEPATH += $$PWD/ffmpeg-4.2.2/ffmpeg-4.2.2/include \
                $$PWD/SDL2-2.0.10/SDL2-2.0.10/include

LIBS += $$PWD/ffmpeg-4.2.2/ffmpeg-4.2.2/lib/avcodec.lib \
        $$PWD/ffmpeg-4.2.2/ffmpeg-4.2.2/lib/avdevice.lib \
        $$PWD/ffmpeg-4.2.2/ffmpeg-4.2.2/lib/avfilter.lib \
        $$PWD/ffmpeg-4.2.2/ffmpeg-4.2.2/lib/avformat.lib \
        $$PWD/ffmpeg-4.2.2/ffmpeg-4.2.2/lib/avutil.lib \
        $$PWD/ffmpeg-4.2.2/ffmpeg-4.2.2/lib/postproc.lib \
        $$PWD/ffmpeg-4.2.2/ffmpeg-4.2.2/lib/swresample.lib \
        $$PWD/ffmpeg-4.2.2/ffmpeg-4.2.2/lib/swscale.lib \
        $$PWD/SDL2-2.0.10/SDL2-2.0.10/lib/x86/SDL2.lib


# ========== 直播平台网络库 ==========
include($$PWD/../../Launcher/netapi/netapi.pri)
# 注意：common 必须在 netapi 之前，确保共享 packdef.h 优先
INCLUDEPATH += $$PWD/../../common $$PWD/../../Launcher/netapi/net $$PWD/../../Launcher/netapi/mediator

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
