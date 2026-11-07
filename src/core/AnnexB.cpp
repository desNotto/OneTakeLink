// SPDX-License-Identifier: GPL-3.0-or-later
#include "AnnexB.h"

namespace otl {

// Returns the offset of the next start code (00 00 01) at or after `from`,
// or `size` when there is none. `scLen` receives 3 or 4 (00 00 00 01).
static size_t findStartCode(const uint8_t *d, size_t size, size_t from, size_t *scLen)
{
    for (size_t i = from; i + 3 <= size; ++i) {
        if (d[i] == 0 && d[i + 1] == 0) {
            if (d[i + 2] == 1) {
                if (i > from && d[i - 1] == 0) {
                    *scLen = 4;
                    return i - 1;
                }
                *scLen = 3;
                return i;
            }
        }
    }
    *scLen = 0;
    return size;
}

QByteArray extractParameterSets(VideoCodec codec, const uint8_t *data, size_t size)
{
    QByteArray out;
    size_t sc = 0;
    size_t pos = findStartCode(data, size, 0, &sc);
    while (pos < size) {
        const size_t nalStart = pos + sc;
        size_t nextSc = 0;
        size_t next = findStartCode(data, size, nalStart, &nextSc);
        if (nalStart >= size)
            break;
        const uint8_t header = data[nalStart];
        bool isParamSet;
        if (codec == VideoCodec::H264) {
            const int type = header & 0x1f;
            isParamSet = type == 7 || type == 8;
        } else {
            const int type = (header >> 1) & 0x3f;
            isParamSet = type == 32 || type == 33 || type == 34;
        }
        if (isParamSet) {
            static const char kStart[4] = {0, 0, 0, 1};
            out.append(kStart, 4);
            out.append(reinterpret_cast<const char *>(data + nalStart), int(next - nalStart));
        }
        pos = next;
        sc = nextSc;
    }
    return out;
}

} // namespace otl
