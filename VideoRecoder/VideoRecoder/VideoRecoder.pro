QT       += core gui multimedia network

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++11

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    main.cpp \
    audio_mixer.cpp \
    audio_read.cpp \
    cuda_helper.cpp \
    picinpic_read.cpp \
    picturewidget.cpp \
    recorderdialog.cpp \
    savevideofilethread.cpp

HEADERS += \
    audio_mixer.h \
    audio_read.h \
    common.h \
    cuda_helper.h \
    picinpic_read.h \
    picturewidget.h \
    recorderdialog.h \
    savevideofilethread.h

FORMS += \
    picturewidget.ui \
    recorderdialog.ui

# FFmpeg 配置
INCLUDEPATH += $$PWD/ffmpeg-4.2.2/ffmpeg-4.2.2/include

LIBS += $$PWD/ffmpeg-4.2.2/ffmpeg-4.2.2/lib/avcodec.lib \
    $$PWD/ffmpeg-4.2.2/ffmpeg-4.2.2/lib/avdevice.lib \
    $$PWD/ffmpeg-4.2.2/ffmpeg-4.2.2/lib/avfilter.lib \
    $$PWD/ffmpeg-4.2.2/ffmpeg-4.2.2/lib/avformat.lib \
    $$PWD/ffmpeg-4.2.2/ffmpeg-4.2.2/lib/avutil.lib \
    $$PWD/ffmpeg-4.2.2/ffmpeg-4.2.2/lib/postproc.lib \
    $$PWD/ffmpeg-4.2.2/ffmpeg-4.2.2/lib/swresample.lib \
    $$PWD/ffmpeg-4.2.2/ffmpeg-4.2.2/lib/swscale.lib

# OpenCV 配置
INCLUDEPATH += $$PWD/opencv/opencv/opencv-release/opencv-release/include

LIBS += $$PWD/opencv/opencv/opencv-release/opencv-release/lib/libopencv_core420.dll.a \
    $$PWD/opencv/opencv/opencv-release/opencv-release/lib/libopencv_imgproc420.dll.a \
    $$PWD/opencv/opencv/opencv-release/opencv-release/lib/libopencv_highgui420.dll.a \
    $$PWD/opencv/opencv/opencv-release/opencv-release/lib/libopencv_imgcodecs420.dll.a \
    $$PWD/opencv/opencv/opencv-release/opencv-release/lib/libopencv_videoio420.dll.a

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

# ========== 直播平台网络库 ==========
# netapi（TCP 客户端库）— 从 VideoRecoder/VideoRecoder/ → Launcher/netapi/
include($$PWD/../../Launcher/netapi/netapi.pri)
# 注意：common 必须在 netapi 之前，确保共享 packdef.h 优先
INCLUDEPATH += $$PWD/../../common $$PWD/../../Launcher/netapi/net $$PWD/../../Launcher/netapi/mediator

#优化 解决链接器错误：添加 C++ 运行时库（必须在 win32 块外面）
LIBS += -lstdc++ -lgcc_eh -lole32

