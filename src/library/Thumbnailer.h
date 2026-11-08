// SPDX-License-Identifier: GPL-3.0-or-later
// Take thumbnails: one frame ~1 s into the clip, cached on disk
// (%LOCALAPPDATA%/OneTakeLink/cache/thumbs) keyed by path, size and
// modification time, so the library opens instantly the second time.
#pragma once

#include <QImage>
#include <QString>

namespace otl {

// Cached or freshly generated thumbnail (null image on failure).
// Blocking: call from a worker thread.
QImage takeThumbnail(const QString &videoPath, int width = 320);

// Decodes one frame without caching.
QImage renderThumbnail(const QString &videoPath, int width);

QString thumbnailCacheDir();

} // namespace otl
