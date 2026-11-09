// SPDX-License-Identifier: GPL-3.0-or-later
// Maps the PC's monotonic clock to a camera stream's timestamps, so sound
// captured on the PC (audio interface) lands at the right place in a take
// whose video carries the phone's clock.
//
//   stream_pts = local_ns + offset
//
// Exact when the source syncs clocks (LensLink TIMESYNC). Otherwise it is
// estimated from packet arrivals: the minimum of (pts - arrival) over a few
// seconds, i.e. the phone clock minus the smallest transport delay — PC
// audio then sits up to that delay early, which the user's sync offset
// corrects.
#pragma once

#include <atomic>
#include <climits>
#include <cstdint>

namespace otl {

class StreamClock {
public:
    void setExact(int64_t offsetNs)
    {
        m_offset = offsetNs;
        m_exact = true;
        m_known = true;
    }

    // Producer thread only (the source's worker).
    void observe(int64_t streamPtsNs, int64_t localArrivalNs)
    {
        if (m_exact)
            return;
        const int64_t d = streamPtsNs - localArrivalNs;
        if (d < m_windowMin)
            m_windowMin = d;
        if (!m_known) {
            m_offset = d;
            m_known = true;
            m_windowStart = localArrivalNs;
        } else if (localArrivalNs - m_windowStart > 3'000'000'000LL) {
            m_offset = m_windowMin;
            m_windowMin = LLONG_MAX;
            m_windowStart = localArrivalNs;
        }
    }

    void reset()
    {
        m_known = false;
        m_exact = false;
        m_windowMin = LLONG_MAX;
    }

    bool map(int64_t localNs, int64_t *streamNs) const
    {
        if (!m_known)
            return false;
        *streamNs = localNs + m_offset.load();
        return true;
    }
    bool isExact() const { return m_exact; }

private:
    std::atomic<bool> m_known{false};
    std::atomic<bool> m_exact{false};
    std::atomic<int64_t> m_offset{0};
    int64_t m_windowMin = LLONG_MAX;
    int64_t m_windowStart = 0;
};

} // namespace otl
