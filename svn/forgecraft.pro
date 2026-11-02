QT += core gui widgets concurrent svg
CONFIG += c++17
TARGET = forgecraft
SOURCES += main.cpp audio.cpp mp3.cpp dsp.cpp processingui.cpp waveform.cpp mainwindow.cpp dsp_tests.cpp recorder.cpp recorddialog.cpp recordingui.cpp recording_tests.cpp theme.cpp aboutdialog.cpp newaudiodialog.cpp mixdialog.cpp optionsdialog.cpp
HEADERS += audio.h mp3.h dsp.h waveform.h mainwindow.h recorder.h recorddialog.h theme.h aboutdialog.h newaudiodialog.h mixdialog.h optionsdialog.h
RESOURCES += forgecraft.qrc
RC_FILE = forgecraft.rc
win32:LIBS += -lwinmm
DESTDIR = $$PWD/../out/bin
CONFIG(debug, debug|release) {
    OBJECTS_DIR = $$PWD/../out/build/debug/obj
    MOC_DIR = $$PWD/../out/build/debug/moc
} else {
    OBJECTS_DIR = $$PWD/../out/build/release/obj
    MOC_DIR = $$PWD/../out/build/release/moc
}
