// SPDX-License-Identifier: GPL-3.0-or-later
// Reads a video file's metadata (duration, size, frame rate, codec).
#pragma once

#include <QString>

namespace otl {

struct MediaInfo {
    bool ok = false;
    qint64 durationMs = 0;
    int width = 0;
    int height = 0;
    double fps = 0;
    QString codec;
    QString error;
};

MediaInfo probeMedia(const QString &path);

} // namespace otl
