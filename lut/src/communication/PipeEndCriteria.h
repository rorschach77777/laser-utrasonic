#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>

// One sliding-window rule shared by the axial pipe-tail detector and the
// QML dynamic detection rate. An incomplete window can confirm as soon as
// both conditions have been met; it never waits for an artificial tenth frame.
namespace PipeEndCriteria {
inline constexpr std::size_t WindowFrames = 10;
inline constexpr std::size_t RequiredInvalidFrames = 5;
inline constexpr std::size_t RequiredConsecutiveInvalidFrames = 3;

struct Decision {
    bool confirmed = false;
    std::uint64_t firstInvalidFrameId = 0;
};

class VoteWindow
{
public:
    void reset() { m_frames.clear(); }

    Decision observe(std::uint64_t frameId, bool invalid)
    {
        if (!frameId)
            return {};
        // Unreceived Frame IDs are not NaNs. Do not join votes across a gap.
        if (!m_frames.empty() && frameId != m_frames.back().frameId + 1)
            reset();
        m_frames.push_back({frameId, invalid});
        if (m_frames.size() > WindowFrames)
            m_frames.pop_front();
        return evaluate();
    }

    // T2 may retrospectively recover a pending NaN without adding a frame.
    bool backfill(std::uint64_t frameId)
    {
        for (auto &frame : m_frames) {
            if (frame.frameId != frameId)
                continue;
            const bool wasInvalid = frame.invalid;
            frame.invalid = false;
            return wasInvalid;
        }
        return false;
    }

    Decision evaluate() const
    {
        std::size_t invalidCount = 0;
        std::size_t consecutive = 0;
        std::size_t longestRun = 0;
        std::uint64_t firstInvalid = 0;
        for (const auto &frame : m_frames) {
            if (frame.invalid) {
                ++invalidCount;
                ++consecutive;
                if (!firstInvalid)
                    firstInvalid = frame.frameId;
                if (consecutive > longestRun)
                    longestRun = consecutive;
            } else {
                consecutive = 0;
            }
        }
        if (invalidCount >= RequiredInvalidFrames &&
            longestRun >= RequiredConsecutiveInvalidFrames)
            return {true, firstInvalid};
        return {};
    }

    bool hasInvalid() const
    {
        for (const auto &frame : m_frames) {
            if (frame.invalid)
                return true;
        }
        return false;
    }

private:
    struct Frame {
        std::uint64_t frameId = 0;
        bool invalid = false;
    };
    std::deque<Frame> m_frames;
};
} // namespace PipeEndCriteria
