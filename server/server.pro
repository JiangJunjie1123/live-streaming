# LiveStreaming Server — Linux Qt Creator 项目
# 使用 CONFIG -= qt 构建纯 C++ 无 GUI 服务器

CONFIG -= qt
CONFIG += c++11 console

TARGET = server
TEMPLATE = app

INCLUDEPATH += include

SOURCES += \
    src/main.cpp \
    src/TCPKernel.cpp \
    src/block_epoll_net.cpp \
    src/Thread_pool.cpp \
    src/Mysql.cpp \
    src/clogic.cpp \
    src/err_str.cpp

HEADERS += \
    include/packdef.h \
    include/TCPKernel.h \
    include/block_epoll_net.h \
    include/Thread_pool.h \
    include/Mysql.h \
    include/clogic.h \
    include/err_str.h

LIBS += -lpthread -lmysqlclient
