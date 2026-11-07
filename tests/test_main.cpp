// SPDX-License-Identifier: GPL-3.0-or-later
// Minimal self-contained unit tests (no framework): protocol framing,
// handshake parsing, Annex B parameter sets, naming, crop geometry.
#include "core/AnnexB.h"
#include "preview/CropMath.h"
#include "recording/TakeNamer.h"

#include "handshake.h"
#include "protocol.h"

#include <cmath>
#include <cstdio>
#include <cstring>

static int g_failures = 0;
#define CHECK(cond)                                                                 \
    do {                                                                            \
        if (!(cond)) {                                                              \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                           \
        }                                                                           \
    } while (0)

static void testHeader()
{
    uint8_t buf[OBSC_HEADER_SIZE];
    obsc_build_header(buf, OBSC_PKT_VIDEO, OBSC_FLAG_KEYFRAME, 0x0102030405060708ULL, 1234);
    obsc_header h{};
    CHECK(obsc_parse_header(buf, &h));
    CHECK(h.type == OBSC_PKT_VIDEO);
    CHECK(h.flags == OBSC_FLAG_KEYFRAME);
    CHECK(h.pts_ns == 0x0102030405060708ULL);
    CHECK(h.payload_size == 1234);
    buf[0] = 'X';
    CHECK(!obsc_parse_header(buf, &h));
    obsc_build_header(buf, OBSC_PKT_VIDEO, 0, 0, OBSC_MAX_PAYLOAD + 1);
    CHECK(!obsc_parse_header(buf, &h));
}

static void testHello()
{
    const char *json = R"({"name":"Emma's iPhone","app":"LensLink","protocol":1,"kind":"camera","standby":true,"armed":false})";
    lenslink_hello h{};
    CHECK(lenslink_hello_parse(json, std::strlen(json), &h));
    CHECK(h.has_name && std::strcmp(h.name, "Emma's iPhone") == 0);
    CHECK(!h.is_screen && h.standby && h.unarmed);

    const char *cfg = R"({"codec":"hevc","width":1920,"height":1080,"fps":60})";
    lenslink_video_config c{};
    CHECK(lenslink_video_config_parse(cfg, std::strlen(cfg), &c));
    CHECK(c.codec == LENSLINK_CODEC_HEVC);

    const char *bad = R"({"name":"a","name":"b"})";
    CHECK(!lenslink_hello_parse(bad, std::strlen(bad), &h));
}

static void testAnnexB()
{
    // SPS(7) PPS(8) IDR(5), mixed 4- and 3-byte start codes.
    const uint8_t au[] = {0, 0, 0, 1, 0x67, 0xAA, 0xBB, 0, 0, 1, 0x68, 0xCC, 0, 0, 0, 1, 0x65, 0x11, 0x22};
    QByteArray ps = otl::extractParameterSets(otl::VideoCodec::H264, au, sizeof(au));
    const char expect[] = {0, 0, 0, 1, 0x67, char(0xAA), char(0xBB), 0, 0, 0, 1, 0x68, char(0xCC)};
    CHECK(ps == QByteArray(expect, sizeof(expect)));

    const uint8_t p[] = {0, 0, 0, 1, 0x41, 0x01, 0x02};
    CHECK(otl::extractParameterSets(otl::VideoCodec::H264, p, sizeof(p)).isEmpty());

    // HEVC VPS(32)=0x40, SPS(33)=0x42, PPS(34)=0x44, IDR(19)=0x26
    const uint8_t hevc[] = {0, 0, 0, 1, 0x40, 0x01, 0, 0, 0, 1, 0x42, 0x01, 0, 0, 0, 1, 0x44, 0x01, 0, 0, 0, 1, 0x26, 0x01};
    QByteArray hps = otl::extractParameterSets(otl::VideoCodec::HEVC, hevc, sizeof(hevc));
    CHECK(hps.size() == 18);
}

static void testNamer()
{
    CHECK(otl::TakeNamer::sanitize(QStringLiteral("My:Song?")) == QStringLiteral("My_Song_"));
    CHECK(otl::TakeNamer::sanitize(QStringLiteral("  ")) == QStringLiteral("Untitled"));
    otl::TakeNamer n;
    n.root = QStringLiteral("/nonexistent-root");
    n.project = QStringLiteral("Demo");
    const QString path = n.nextTakePath();
    CHECK(path.endsWith(QStringLiteral("_Demo_Take_001.mp4")));
    CHECK(path.contains(QStringLiteral("/Demo/Session_")));
}

static void testCrop()
{
    using namespace otl::CropMath;
    QRect r = cropPixels(QSize(1920, 1080), 9.0 / 16.0);
    CHECK(r.height() == 1080);
    CHECK(std::abs(r.width() - 606) <= 2);
    CHECK(r.x() + r.width() <= 1920);
    CHECK(std::abs(r.center().x() - 960) <= 2);
    QRect left = cropPixels(QSize(1920, 1080), 1.0, 0.0);
    CHECK(left.x() == 0 && left.width() == 1080);
    QRect right = cropPixels(QSize(1920, 1080), 1.0, 1.0);
    CHECK(right.right() == 1919);
    QRect wide = cropPixels(QSize(1080, 1920), 16.0 / 9.0);
    CHECK(wide.width() == 1080 && wide.height() == 606);
}

int main()
{
    testHeader();
    testHello();
    testAnnexB();
    testNamer();
    testCrop();
    if (g_failures)
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
    else
        std::printf("all tests passed\n");
    return g_failures ? 1 : 0;
}
