QT += core network
QT -= gui

CONFIG += c++11

TARGET = DataProxy
CONFIG += console
CONFIG -= app_bundle

TEMPLATE = app

SOURCES += main.cpp \
    network/zp_net_threadpool.cpp \
    network/zp_netlistenthread.cpp \
    network/zp_nettransthread.cpp \
    network/zp_tcpserver.cpp \
    proxyobject.cpp \
    logger/st_logger.cpp

HEADERS += \
    network/ssl_config.h \
    network/zp_net_threadpool.h \
    network/zp_netlistenthread.h \
    network/zp_nettransthread.h \
    network/zp_tcpserver.h \
    proxyobject.h \
    logger/st_logger.h
