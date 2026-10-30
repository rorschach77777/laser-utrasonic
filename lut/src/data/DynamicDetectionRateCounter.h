#pragma once

#include "../communication/PipeEndCriteria.h"

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <cstdint>
#include <deque>

// The stable triple opens the measured pipe. A 10-consecutive-frame sliding
// vote closes it on >=5 NaNs including >=3 consecutive NaNs. On confirmation,
// the FIRST NaN in that vote is the exclusive cutoff: even intervening good
// thicknesses are excluded from the denominator and numerator.
// Before confirmation, buffered frames become countable once they can no
// longer belong to any future 10-frame end window.
class DynamicDetectionRateCounter
{
public:
    enum class Phase { WaitingForThickness, Counting, Frozen };
    static constexpr std::size_t WindowFrames = PipeEndCriteria::WindowFrames;
    static constexpr std::size_t RequiredMissingFrames =
        PipeEndCriteria::RequiredInvalidFrames;
    static constexpr std::size_t RequiredConsecutiveMissingFrames =
        PipeEndCriteria::RequiredConsecutiveInvalidFrames;

    void reset()
    {
        m_phase = Phase::WaitingForThickness;
        m_attempts = m_valid = 0;
        m_pending.clear();
        m_countedMissing.clear();
        m_endVote.reset();
        m_lastFrameId = 0;
    }

    Phase phase() const { return m_phase; }
    bool hasResult() const { return m_phase != Phase::WaitingForThickness; }
    std::uint64_t attempts() const { return m_attempts; }
    std::uint64_t valid() const { return m_valid; }
    double percent() const
    {
        return m_attempts ? 100.0 * double(m_valid) / double(m_attempts) : 0.0;
    }

    // stableTriple means three consecutive Frame IDs with sufficiently
    // consistent positive, finite thicknesses (checked by DataController).
    void observe(std::uint64_t frameId, double thicknessMm, bool stableTriple)
    {
        if (!frameId || frameId <= m_lastFrameId)
            return;
        const bool consecutive = !m_lastFrameId || frameId == m_lastFrameId + 1;
        m_lastFrameId = frameId;
        if (m_phase != Phase::Counting) {
            if (stableTriple) {
                m_phase = Phase::Counting;
                m_attempts = m_valid = 3; // include the confirming three frames
                m_pending.clear();
                m_countedMissing.clear();
                m_endVote.reset();
                m_endVote.observe(frameId, false);
            }
            return;
        }

        // A Frame ID gap is not a NaN. Flush the previous undecided interval
        // and start a new window without guessing missing acquisitions.
        if (!consecutive) {
            commitPending();
            m_endVote.reset();
        }
        const bool validThickness = std::isfinite(thicknessMm);
        const auto decision = m_endVote.observe(frameId, !validThickness);
        if (m_pending.empty() && validThickness) {
            ++m_attempts;
            ++m_valid;
            return;
        }

        m_pending.push_back({frameId, validThickness});
        if (decision.confirmed) {
            // Correct a delayed decision retroactively. Everything from the
            // first NaN in the confirming window is outside the steel pipe.
            commitPendingBefore(decision.firstInvalidFrameId);
            m_phase = Phase::Frozen;
            return;
        }

        // Delay at most ten consecutive frames; once the oldest ages out of
        // the sliding window, it can no longer be excluded by a later vote.
        while (m_pending.size() > WindowFrames)
            commitFront();
        if (!m_endVote.hasInvalid())
            commitPending();
    }

    // A T2 backfill changes an existing NaN to an actual thickness result.
    // Recalculate the same vote; if the correction invalidates a premature
    // end, continue the original segment with its pending observations.
    void backfill(std::uint64_t frameId)
    {
        if (m_phase == Phase::WaitingForThickness || !frameId)
            return;
        const auto pending = std::find_if(m_pending.begin(), m_pending.end(),
            [frameId](const auto &p) { return p.frameId == frameId; });
        if (pending != m_pending.end()) {
            if (pending->valid)
                return;
            pending->valid = true;
            m_endVote.backfill(frameId);
            if (m_phase == Phase::Frozen) {
                const auto decision = m_endVote.evaluate();
                if (decision.confirmed) {
                    commitPendingBefore(decision.firstInvalidFrameId);
                    return;
                }
                m_phase = Phase::Counting;
            }
            if (!m_endVote.hasInvalid())
                commitPending();
            return;
        }
        const auto old = std::find(m_countedMissing.begin(),
                                   m_countedMissing.end(), frameId);
        if (old != m_countedMissing.end()) {
            ++m_valid;
            m_countedMissing.erase(old);
        }
    }

private:
    struct PendingFrame {
        std::uint64_t frameId = 0;
        bool valid = false;
    };

    void rememberCountedMissing(std::uint64_t id)
    {
        m_countedMissing.push_back(id);
        if (m_countedMissing.size() > 256)
            m_countedMissing.pop_front(); // same bounded T2 backfill horizon
    }
    void commitFront()
    {
        const auto frame = m_pending.front();
        m_pending.pop_front();
        ++m_attempts;
        if (frame.valid)
            ++m_valid;
        else
            rememberCountedMissing(frame.frameId);
    }
    void commitPendingBefore(std::uint64_t exclusiveFrameId)
    {
        while (!m_pending.empty() && m_pending.front().frameId < exclusiveFrameId)
            commitFront();
    }
    void commitPending()
    {
        while (!m_pending.empty())
            commitFront();
    }

    Phase m_phase = Phase::WaitingForThickness;
    std::uint64_t m_attempts = 0;
    std::uint64_t m_valid = 0;
    std::uint64_t m_lastFrameId = 0;
    std::deque<PendingFrame> m_pending;
    std::deque<std::uint64_t> m_countedMissing;
    PipeEndCriteria::VoteWindow m_endVote;
};
