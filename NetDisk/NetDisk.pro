TEMPLATE = app
CONFIG += console
CONFIG -= app_bundle
CONFIG -= qt

INCLUDEPATH+=./include
LIBS+=-lpthread -lmysqlclient

SOURCES += \
    src/main.cpp \
    src/err_str.cpp \
    src/clogic.cpp \
    src/block_epoll_net.cpp \
    src/Thread_pool.cpp \
    src/TCPKernel.cpp \
    src/Mysql.cpp

HEADERS += \
    include/Thread_pool.h \
    include/TCPKernel.h \
    include/packdef.h \
    include/Mysql.h \
    include/err_str.h \
    include/clogic.h \
    include/block_epoll_net.h

