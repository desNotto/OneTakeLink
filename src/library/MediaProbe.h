// SPDX-License-Identifier: GPL-3.0-or-later
// Reads a video file's metadata (duration, size, frame rate, codec).
#pragma once

#include <QString>

struct AVCodecParameters;

namespace otl {

// Clockwise display rotation (0/90/180/270) from a stream's display
// matrix (e.g. takes recorded with the monitor's rotate button).
int displayRotationCw(const AVCodecParameters *par);

struct MediaInfo {
    bool ok = false;
    qint64 durationMs = 0;
    int width = 0;  // as displayed (rotation applied)
    int height = 0;
    int rotation = 0; // clockwise degrees
    double fps = 0;
    QString codec;
    QString error;
};

MediaInfo probeMedia(const QString &path);

} // namespace otl
