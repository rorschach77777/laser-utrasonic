#pragma once

#include "CandidateTofOutputChain.h"
#include "IdentityObservation.h"

namespace IdentityHypothesis {

using Polarity = CandidateTof::PolarityIdentity;

enum class State
{
    Unavailable,
    Available,
    Pending,
    Deferred,
    ConfirmedShift
};

struct Decision
{
    Polarity identity = Polarity::Unknown;
    bool identityEstablished = false;
    QString stage;
    double observedCenter = std::numeric_limits<double>::quiet_NaN();
    QString source = QStringLiteral("UNAVAILABLE");
    State state = State::Unavailable;
    bool backfillPrevious = false;
    QString backfillFile;
    double backfillObservedCenter = std::numeric_limits<double>::quiet_NaN();
    bool backfillUsedRaw = false;

    bool available() const { return std::isfinite(observedCenter); }
};

inline QString polarityName(Polarity identity)
{
    switch (identity) {
    case Polarity::Positive: return QStringLiteral("POSITIVE");
    case Polarity::Negative: return QStringLiteral("NEGATIVE");
    default: return QStringLiteral("UNKNOWN");
    }
}

inline QString stateName(State state)
{
    switch (state) {
    case State::Available: return QStringLiteral("AVAILABLE");
    case State::Pending: return QStringLiteral("PENDING");
    case State::Deferred: return QStringLiteral("DEFERRED");
    case State::ConfirmedShift: return QStringLiteral("CONFIRMED_SHIFT");
    default: return QStringLiteral("UNAVAILABLE");
    }
}

class Chain
{
public:
    Decision process(const QVariantMap &r)
    {
        const CandidateTof::PolarityDecision polarity = m_selector.observe(r);
        const CandidateTof::SingleAnchorDecision single = m_single.resolve(r, polarity);
        const CandidateTof::DualAnchorDecision dual = m_dual.resolve(r, polarity, single);
        const CandidateTof::CycleDecision cycle = m_cycle.resolve(r, polarity, dual);
        const CandidateTof::ShiftDecision shift = m_shift.resolve(r, polarity, dual, cycle);

        Decision out;
        out.identity = polarity.identity;
        out.identityEstablished = polarity.identity != Polarity::Unknown;
        out.stage = polarity.stage;
        out.observedCenter = shift.center;
        out.source = shift.source;
        out.state = classifyState(shift);
        out.backfillPrevious = shift.backfillPrevious;
        out.backfillFile = shift.backfillFile;
        out.backfillObservedCenter = shift.backfillCenter;
        out.backfillUsedRaw = shift.backfillUsedRaw;
        return out;
    }

private:
    CandidateTof::DualPolarityIdentitySelector m_selector;
    CandidateTof::SingleAnchorFallback m_single;
    CandidateTof::DualAnchorFallback m_dual;
    CandidateTof::SamePolarityCycleGate m_cycle;
    CandidateTof::PendingDeferredConfirmer m_shift;

    static State classifyState(const CandidateTof::ShiftDecision &shift)
    {
        if (shift.source.startsWith(QStringLiteral("CYCLE_SHIFT_PENDING")))
            return State::Pending;
        if (shift.source.startsWith(QStringLiteral("CYCLE_SHIFT_DEFERRED")))
            return State::Deferred;
        if (shift.source.startsWith(QStringLiteral("CYCLE_SHIFT_CONFIRMED")))
            return State::ConfirmedShift;
        return std::isfinite(shift.center) ? State::Available : State::Unavailable;
    }
};

inline DualPolarityIdentity toEvidenceIdentity(Polarity identity)
{
    switch (identity) {
    case Polarity::Positive: return DualPolarityIdentity::Positive;
    case Polarity::Negative: return DualPolarityIdentity::Negative;
    default: return DualPolarityIdentity::Unknown;
    }
}

} // namespace IdentityHypothesis
