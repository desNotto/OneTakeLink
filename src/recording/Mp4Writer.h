// SPDX-License-Identifier: GPL-3.0-or-later
// Thin libavformat wrapper: writes an encoded H.264/HEVC elementary stream
// (Annex B access units) into an MP4 without re-encoding. No audio track.
#pragma once

#include "core/VideoTypes.h"

#include <QString>

struct AVFormatContext;
struct AVStream;
struct AVPacket;

namespace otl {

class Mp4Writer {
public:
    ~Mp4Writer();

    // fragmented = crash-safe fragmented MP4 (fragment every ~0.5 s).
    // rotationCw (0/90/180/270): stored as the track's display matrix, so
    // players show the video rotated without re-encoding it.
    bool open(const QString &path, const StreamFormat &format, bool fragmented, QString *error,
              int rotationCw = 0);
    // pts relative to the first frame, in nanoseconds; must increase.
    bool write(const EncodedPacket &packet, int64_t relPtsNs, int64_t durationNs, QString *error);
    bool close(QString *error); // writes the trailer
    qint64 bytesWritten() const;
    bool isOpen() const { return m_ctx != nullptr; }

private:
    AVFormatContext *m_ctx = nullptr;
    AVStream *m_stream = nullptr;
    AVPacket *m_pkt = nullptr;
    int64_t m_lastDts = INT64_MIN;
    bool m_headerWritten = false;
};

QString avErrorString(int err);

} // namespace otl
