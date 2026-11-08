// SPDX-License-Identifier: GPL-3.0-or-later
#include "Thumbnailer.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}

namespace otl {

QString thumbnailCacheDir()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)).filePath(QStringLiteral("thumbs"));
}

QImage renderThumbnail(const QString &videoPath, int width)
{
    QImage result;
    AVFormatContext *fmt = nullptr;
    const QByteArray utf8 = videoPath.toUtf8();
    if (avformat_open_input(&fmt, utf8.constData(), nullptr, nullptr) < 0)
        return result;
    const AVCodec *codec = nullptr;
    int vs = -1;
    AVCodecContext *dec = nullptr;
    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    if (avformat_find_stream_info(fmt, nullptr) >= 0 &&
        (vs = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0)) >= 0 && pkt && frame) {
        AVStream *st = fmt->streams[vs];
        dec = avcodec_alloc_context3(codec);
        avcodec_parameters_to_context(dec, st->codecpar);
        if (avcodec_open2(dec, codec, nullptr) >= 0) {
            // ~1 s in (past the take's pre-roll fade-in), or a third of a short clip.
            qint64 durMs = st->duration != AV_NOPTS_VALUE
                               ? av_rescale_q(st->duration, st->time_base, AVRational{1, 1000})
                               : 0;
            const qint64 atMs = durMs > 0 ? std::min<qint64>(1000, durMs / 3) : 0;
            const int64_t start = st->start_time != AV_NOPTS_VALUE ? st->start_time : 0;
            const int64_t target = start + av_rescale_q(atMs, AVRational{1, 1000}, st->time_base);
            av_seek_frame(fmt, vs, target, AVSEEK_FLAG_BACKWARD);

            bool got = false;
            int packets = 0;
            while (!got && packets < 2000) {
                int ret = avcodec_receive_frame(dec, frame);
                if (ret == 0) {
                    const int64_t pts = frame->best_effort_timestamp;
                    if (pts == AV_NOPTS_VALUE || pts >= target || packets > 600)
                        got = true;
                    else
                        av_frame_unref(frame);
                    continue;
                }
                if (ret != AVERROR(EAGAIN))
                    break;
                if (av_read_frame(fmt, pkt) < 0) {
                    avcodec_send_packet(dec, nullptr);
                    ++packets;
                    continue;
                }
                if (pkt->stream_index == vs) {
                    avcodec_send_packet(dec, pkt);
                    ++packets;
                }
                av_packet_unref(pkt);
            }
            if (got && frame->width > 0 && frame->height > 0) {
                const int w = width;
                const int h = std::max(2, int(double(width) * frame->height / frame->width + 0.5)) & ~1;
                SwsContext *sws = sws_getContext(frame->width, frame->height, AVPixelFormat(frame->format), w, h,
                                                 AV_PIX_FMT_RGB32, SWS_BICUBIC, nullptr, nullptr, nullptr);
                if (sws) {
                    QImage img(w, h, QImage::Format_RGB32);
                    uint8_t *dst[4] = {img.bits(), nullptr, nullptr, nullptr};
                    int dstStride[4] = {int(img.bytesPerLine()), 0, 0, 0};
                    sws_scale(sws, frame->data, frame->linesize, 0, frame->height, dst, dstStride);
                    sws_freeContext(sws);
                    result = img;
                }
            }
        }
    }
    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&dec);
    avformat_close_input(&fmt);
    return result;
}

namespace {

QString pathPrefix(const QString &videoPath)
{
    const QByteArray key = QFileInfo(videoPath).absoluteFilePath().toUtf8();
    return QString::fromLatin1(QCryptographicHash::hash(key, QCryptographicHash::Sha1).toHex());
}

} // namespace

void removeThumbnails(const QString &videoPath)
{
    QDir dir(thumbnailCacheDir());
    const QStringList files = dir.entryList({pathPrefix(videoPath) + QStringLiteral("-*.jpg")}, QDir::Files);
    for (const QString &f : files)
        dir.remove(f);
}

qint64 thumbnailCacheSize()
{
    qint64 total = 0;
    const QFileInfoList files = QDir(thumbnailCacheDir()).entryInfoList(QDir::Files);
    for (const QFileInfo &fi : files)
        total += fi.size();
    return total;
}

void clearThumbnailCache()
{
    QDir(thumbnailCacheDir()).removeRecursively();
}

qint64 pruneThumbnailCache(int maxAgeDays, qint64 maxBytes)
{
    QDir dir(thumbnailCacheDir());
    if (!dir.exists())
        return 0;
    // Oldest use first.
    QFileInfoList files = dir.entryInfoList(QDir::Files, QDir::Time | QDir::Reversed);
    const QDateTime cutoff = QDateTime::currentDateTime().addDays(-maxAgeDays);
    qint64 total = 0;
    for (const QFileInfo &fi : files)
        total += fi.size();
    qint64 freed = 0;
    for (const QFileInfo &fi : files) {
        const bool stale = fi.lastModified() < cutoff;
        const bool overBudget = total > maxBytes;
        // Anything that isn't a current-format thumbnail (e.g. v0.2 names
        // without the path prefix) doesn't belong here.
        const bool foreign = fi.suffix() != QLatin1String("jpg") || fi.fileName().size() != 40 + 1 + 40 + 4 ||
                             fi.fileName().at(40) != QLatin1Char('-');
        if (!stale && !overBudget && !foreign)
            continue;
        if (QFile::remove(fi.absoluteFilePath())) {
            total -= fi.size();
            freed += fi.size();
        }
    }
    return freed;
}

QImage takeThumbnail(const QString &videoPath, int width)
{
    const QFileInfo fi(videoPath);
    const QString prefix = pathPrefix(videoPath);
    const QByteArray version = QStringLiteral("%1|%2|%3")
                                   .arg(fi.size())
                                   .arg(fi.lastModified().toMSecsSinceEpoch())
                                   .arg(width)
                                   .toUtf8();
    const QString name = prefix + QLatin1Char('-') +
                         QString::fromLatin1(QCryptographicHash::hash(version, QCryptographicHash::Sha1).toHex()) +
                         QStringLiteral(".jpg");
    QDir dir(thumbnailCacheDir());
    const QString cached = dir.filePath(name);
    QImage img;
    if (img.load(cached)) {
        // Touch: pruning keeps recently used thumbnails.
        QFile f(cached);
        if (f.open(QIODevice::ReadWrite))
            f.setFileTime(QDateTime::currentDateTime(), QFileDevice::FileModificationTime);
        return img;
    }
    img = renderThumbnail(videoPath, width);
    if (!img.isNull()) {
        dir.mkpath(QStringLiteral("."));
        // The take changed (or another size was cached): replace old versions.
        const QStringList old = dir.entryList({prefix + QStringLiteral("-*.jpg")}, QDir::Files);
        for (const QString &f : old)
            dir.remove(f);
        img.save(cached, "JPG", 85);
    }
    return img;
}

} // namespace otl
