TEMPLATE = subdirs

SUBDIRS = \
    Launcher \
    VideoRecoder \
    MediaPlayer

Launcher.file = Launcher/Launcher.pro
VideoRecoder.file = VideoRecoder/VideoRecoder/VideoRecoder.pro
MediaPlayer.file = videoplayer/MedieaPlayer/MedieaPlayer.pro

CONFIG += ordered
