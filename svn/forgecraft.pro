QT += core gui widgets concurrent svg
SOURCES += audioformats.cpp audiofiledialogs.cpp audioformats_tests.cpp
HEADERS += audioformats.h audiofiledialogs.h
HEADERS += waveoutputmute.h
CONFIG += c++17
TARGET = forgecraft
SOURCES += main.cpp audio.cpp mp3.cpp dsp.cpp processingui.cpp waveform.cpp mainwindow.cpp dsp_tests.cpp recorder.cpp recording_tests.cpp vumeter.cpp theme.cpp aboutdialog.cpp newaudiodialog.cpp mixdialog.cpp optionsdialog.cpp
HEADERS += audio.h mp3.h dsp.h waveform.h mainwindow.h recorder.h theme.h aboutdialog.h newaudiodialog.h vumeter.h mixdialog.h optionsdialog.h
RESOURCES += forgecraft.qrc
RC_FILE = forgecraft.rc
SOURCES += loading_tests.cpp
SOURCES += audio_benchmark.cpp
SOURCES += normalization.cpp normalizedialog.cpp
SOURCES += normalizedialog_tests.cpp
SOURCES += previewplayer.cpp
SOURCES += processpreview.cpp processpreview_tests.cpp
HEADERS += processpreview.h
SOURCES += processpresets.cpp processpresets_tests.cpp
HEADERS += processpresets.h
HEADERS += previewplayer.h
HEADERS += normalization.h normalizedialog.h
win32:LIBS += -lwinmm
DESTDIR = $$PWD/../out/bin
CONFIG(debug, debug|release) {
    OBJECTS_DIR = $$PWD/../out/build/debug/obj
    MOC_DIR = $$PWD/../out/build/debug/moc
} else {
    OBJECTS_DIR = $$PWD/../out/build/release/obj
    MOC_DIR = $$PWD/../out/build/release/moc
}

SOURCES += samplebuffer.cpp largeaudio_tests.cpp
HEADERS += samplebuffer.h

SOURCES += playbackstream.cpp
HEADERS += playbackstream.h
