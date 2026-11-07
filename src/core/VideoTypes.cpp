// SPDX-License-Identifier: GPL-3.0-or-later
#include "VideoTypes.h"

extern "C" {
#include <libavutil/frame.h>
}

namespace otl {

FramePtr makeFramePtr(AVFrame *frame)
{
    return FramePtr(frame, [](const AVFrame *f) {
        AVFrame *m = const_cast<AVFrame *>(f);
        av_frame_free(&m);
    });
}

} // namespace otl
