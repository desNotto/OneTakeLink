// SPDX-License-Identifier: GPL-3.0-or-later
// Take thumbnails: one frame ~1 s into the clip, cached on disk
// (%LOCALAPPDATA%/OneTakeLink/OneTakeLink/cache/thumbs) so the library opens instantly
// the second time.
//
// Cache file name: <sha1(path)>-<sha1(size|mtime|width)>.jpg. The path
// prefix lets the app drop a take's thumbnails when the take is deleted,
// renamed or re-encoded; pruneThumbnailCache() bounds the cache by age and
// total size (entries are touched on use, so active takes stay).
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

// Deletes every cached thumbnail of `videoPath`.
void removeThumbnails(const QString &videoPath);

// Drops entries unused for `maxAgeDays`, then the least recently used
// ones until the cache is under `maxBytes`. Returns bytes freed. Blocking.
qint64 pruneThumbnailCache(int maxAgeDays = 60, qint64 maxBytes = 256LL * 1024 * 1024);
qint64 thumbnailCacheSize();
void clearThumbnailCache();

} // namespace otl
