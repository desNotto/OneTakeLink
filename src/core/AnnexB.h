// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "VideoTypes.h"

#include <QByteArray>

namespace otl {

// Collects the parameter-set NAL units (H.264: SPS/PPS; HEVC: VPS/SPS/PPS)
// of an Annex B access unit, each re-emitted with a 4-byte start code.
// Empty when the access unit carries none.
QByteArray extractParameterSets(VideoCodec codec, const uint8_t *data, size_t size);

} // namespace otl
