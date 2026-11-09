#pragma once
#include "audioformats.h"
class QWidget;
bool chooseRawAudio(QWidget *parent, RawAudioOptions &options);
bool chooseAudioExport(QWidget *parent, const AudioFormats::Format &format, const AudioData &audio, AudioExportOptions &options);
