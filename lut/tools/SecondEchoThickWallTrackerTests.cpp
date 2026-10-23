#include "algorithm/SecondEchoOnlyTracker.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {
using Tracker = SecondEchoOnlyTracker;

void check(bool ok, const char *message)
{
    if (!ok) {
        std::cerr << "FAILED " << message << '\n';
        std::exit(1);
    }
}

std::vector<double> envelope(int center)
{
    std::vector<double> values(41);
    for (int i = 0; i < 41; ++i) {
        const double x = (i - center) / 3.0;
        values[static_cast<size_t>(i)] = std::exp(-0.5 * x * x);
    }
    return values;
}

Tracker::Frame frame(double tof, double snr, const std::vector<double> &shape)
{
    Tracker::Frame f;
    f.thickWallRangeOnly = true;
    f.referenceTofSamples = 805.0;
    f.candidates.push_back({tof, snr, snr / 10.0, 0, shape});
    // Direct single-peak fallback rescans waveform samples, never formal peaks.
    const int center = static_cast<int>(std::llround(tof));
    f.candidates.front().envelopePeakIndex = center;
    f.directEnvelopeFirstIndex = center - 45;
    f.directSupportLeft = center - 45;
    f.directSupportRight = center + 45;
    f.directT50HalfWidth = 12;
    f.directEnvelope.assign(91, 0.0);
    f.directLocalMaxSnrDb.assign(91, std::numeric_limits<double>::quiet_NaN());
    for (int j = -45; j <= 45; ++j) {
        const double x = (center + j - tof) / 3.5;
        f.directEnvelope[static_cast<size_t>(j + 45)] = std::exp(-0.5 * x * x);
    }
    f.directLocalMaxSnrDb[45] = snr; // old global SNR is diagnostic only
    f.directEnergyHalfWidth = 3;
    const double gain = std::pow(10.0, snr / 20.0);
    for (int j=-45; j<=45; ++j) {
        const double amp = std::abs(j)<=8 ? gain : 1.0;
        f.directFilteredSignal.push_back(amp * std::cos(
            0.5*3.14159265358979323846*j+0.25*3.14159265358979323846));
    }
    return f;
}

Tracker seeded(const std::vector<double> &shape)
{
    Tracker t;
    check(!t.process(frame(895.0, 15.0, shape)).available,
          "untrusted first-ever startup cannot use fallback");
    check(t.process(frame(895.5, 15.0, shape)).available,
          "primary startup must establish LOCK");
    check(t.process(frame(896.0, 15.0, shape)).available, "stable primary LOCK");
    check(t.process(frame(896.5, 15.0, shape)).available, "stable primary history");
    return t;
}

bool isFinalFallback(const Tracker::Decision &d)
{
    return std::string(d.reason) == "T2_LOCK_FINAL_SINGLE_PEAK_SHADOW"
        || std::string(d.reason) == "T2_LOST_FINAL_SINGLE_PEAK_SHADOW";
}
}

int main()
{
    const auto normal = envelope(20);
    const auto changed = envelope(8);

    // Thin (<15mm) second-echo mode: its primary ordinal/period tracking
    // stays intact. Only the same independent packet-net SNR and adaptive
    // final fallback are additionally permitted for explicit T2-only.
    auto makeThinFrame = [&](double tof, double snr,
                         const std::vector<double> &shape) {
        auto f = frame(tof, snr, shape);
        f.thickWallRangeOnly = false;
        return f;
    };
    auto seedThin = [&](bool allowFinalFallback) {
        Tracker t;
        t.setThinFinalSinglePeakFallbackEnabled(allowFinalFallback);
        check(!t.process(makeThinFrame(895.0, 15.0, normal)).available,
              "thin T2 cold startup must not be published by final fallback");
        check(t.process(makeThinFrame(895.5, 15.0, normal)).available,
              "thin primary T2 must establish LOCK");
        check(t.process(makeThinFrame(896.0, 15.0, normal)).available,
              "thin primary LOCK must remain active");
        check(t.process(makeThinFrame(896.5, 15.0, normal)).available,
              "thin primary prediction history must remain established");
        return t;
    };
    auto directThin = makeThinFrame(898.0, 8.0, normal);
    directThin.candidates.clear();
    auto notEnabled = seedThin(false);
    check(!notEnabled.process(directThin).available,
          "thin T2 fallback is opt-in; disabled route must remain unchanged");
    auto enabledThin = seedThin(true);
    const auto recoveredThin = enabledThin.process(directThin);
    check(recoveredThin.available && !recoveredThin.certified
              && isFinalFallback(recoveredThin)
              && recoveredThin.selectedRank == -1
              && recoveredThin.selectedSnrDb > 1.3
              && std::abs(recoveredThin.tofSamples - 898.0) < 0.35,
          "thin T2 must use current-frame packet-local net SNR and no Top-8");
    const auto afterThin = enabledThin.process(makeThinFrame(898.2, 15.0, normal));
    check(afterThin.available && afterThin.templateCorrelation > 0.9,
          "thin fallback must not train or overwrite primary T2 template");

    auto thinIndependentSnr = seedThin(true);
    auto staleNoise = directThin;
    std::fill(staleNoise.directLocalMaxSnrDb.begin(),
              staleNoise.directLocalMaxSnrDb.end(), -100.0);
    check(thinIndependentSnr.process(staleNoise).available,
          "thin net SNR must be independent of historic/global noise floor");

    auto postGap = seedThin(true);
    Tracker::Frame missingThin;
    missingThin.referenceTofSamples = 805.0;
    check(!postGap.process(missingThin).available,
          "thin post-gap identity guard requires an observed miss");
    const auto shiftedLobe = envelope(6);
    std::vector<double> borderlineShape(41);
    for (size_t i = 0; i < borderlineShape.size(); ++i)
        borderlineShape[i] = normal[i] + 0.58 * shiftedLobe[i];
    const auto postGapCandidate = postGap.process(
        makeThinFrame(898.0, 5.0, borderlineShape));
    check(!postGapCandidate.available
              && std::string(postGapCandidate.reason)
                  == "T2_LOCK_POST_GAP_LOW_IDENTITY_PENDING",
          "thin post-gap weak formal T2 must be isolated without ordinal evidence");

    auto noThinEnvelope = seedThin(true);
    auto noThinWaveform = directThin;
    noThinWaveform.directEnvelope.clear();
    noThinWaveform.directFilteredSignal.clear();
    check(!noThinEnvelope.process(noThinWaveform).available,
          "missing thin-bandpass waveform must never produce invented TOF");

    // 1. Highest priority: a successful primary measurement must never be
    // replaced by a different, stronger, nearer or more ambiguous fallback.
    auto primary = seeded(normal);
    auto primaryFrame = frame(897.0, 15.0, normal);
    primaryFrame.candidates.push_back({903.0, 18.0, 1.8, 1, changed});
    const auto primaryResult = primary.process(primaryFrame);
    check(primaryResult.available && !isFinalFallback(primaryResult),
          "primary valid detection must precede all single-peak fallbacks");
    check(std::abs(primaryResult.tofSamples - 897.0) < 0.01,
          "primary selected TOF must not be overridden by a fallback");
    check(primaryResult.selectedEnvelopePeakIndex == 897,
          "matched formal t50 candidate must retain its actual envelope maximum");

    // 2. The fallback is a publication only, not a new learned identity.
    // Exercise the preexisting RECENTER_PENDING rejection with a good
    // observed peak near BOTH the prediction and the last accepted primary.
    Tracker recenter = seeded(normal);
    const auto fallback = recenter.process(frame(905.0, 5.0, normal));
    check(fallback.available && !fallback.certified && isFinalFallback(fallback)
              && fallback.stage == Tracker::Stage::Locked,
          "recenter failure may publish untrained single-peak TOF");
    check(std::abs(fallback.tofSamples - 905.0) < 0.35
              && fallback.selectedEnvelopePeakIndex == 905
              && std::string(fallback.thickFallbackAudit)
                    == "POST_PRIMARY_LOCK_PRED_UPDATED_NO_TEMPLATE",
          "fallback must use current measured peak, not predictor");
    const auto recenterNext = recenter.process(frame(905.2, 15.0, normal));
    check(recenterNext.available && recenterNext.templateCorrelation > 0.9,
          "fallback did not replace the learned normal template");
    check(std::abs(recenterNext.predictedTofSamples - fallback.tofSamples) < 0.01,
          "next LOCK predictor must be based on successful fallback TOF");

    // 3. Reject unsupported and physically distant candidates outright.
    auto noPeak = seeded(normal);
    Tracker::Frame empty;
    empty.thickWallRangeOnly = true;
    empty.referenceTofSamples = 805.0;
    check(!noPeak.process(empty).available,
          "no generated peak must never become fabricated thickness");
    // Final fallback is deliberately lower than the ordinary tracker SNR.
    // NCC is diagnostic only; a changed waveform must not veto a >1.3dB
    // measured peak that satisfies the unchanged position/history gates.
    auto threshold = seeded(normal);
    const auto atThreshold = threshold.process(frame(899.5, 1.2, changed));
    check(atThreshold.available && isFinalFallback(atThreshold),
          "coherent low-SNR waveform can pass with temporal identity");
    auto aboveThreshold = seeded(normal);
    const auto lowSnrPeak = aboveThreshold.process(frame(900.5, 2.5, changed));
    check(lowSnrPeak.available && !lowSnrPeak.certified
              && isFinalFallback(lowSnrPeak)
              && std::abs(lowSnrPeak.tofSamples - 900.5) < 0.35,
          "1.5dB changed-waveform near-history peak must publish without NCC veto");
    auto far = seeded(normal);
    check(!far.process(frame(945.0, 20.0, changed)).available,
          "candidate outside prediction +/-32 cannot use fallback");

    // The direct waveform fallback must NOT depend on the formal Top-8, or
    // even on an existing formal candidate at all.
    auto noFormalCandidates = seeded(normal);
    auto directOnly = frame(898.0, 4.1, changed);
    directOnly.candidates.clear();
    const auto directOnlyResult = noFormalCandidates.process(directOnly);
    check(directOnlyResult.available && !directOnlyResult.certified
            && isFinalFallback(directOnlyResult)
            && directOnlyResult.selectedRank == -1
            && std::abs(directOnlyResult.tofSamples - 898.0) < 0.35,
          "direct waveform single-peak scan must work with EMPTY formal candidate pool");
    auto noRawSignal = seeded(normal);
    auto noDirect = frame(898.0, 4.1, changed);
    noDirect.directEnvelope.clear();
    noDirect.directLocalMaxSnrDb.clear();
    check(!noRawSignal.process(noDirect).available,
          "weak formal candidate without direct waveform must not be used as fallback");

    // Frame_0327-type case: a genuine peak can be 11.48 samples away from
    // both prediction and the last primary TOF. Enlarging only the prediction
    // search radius is insufficient; the last-primary publication gate must
    // also accept it. No recenter/learning is authorized by this publication.
    Tracker at327;
    for (int i = 0; i < 4; ++i)
        at327.process(frame(920.18, 15.0, normal));
    const auto recovered327 = at327.process(frame(908.7022, 10.14, normal));
    check(recovered327.available && !recovered327.certified
              && isFinalFallback(recovered327)
              && std::abs(recovered327.tofSamples - 908.7022) < 0.35
              && recovered327.thickFallbackLastResidual > 10.0
              && recovered327.thickFallbackLastResidual < 16.0,
          "Frame_0327-type 11.48 sample shift must pass last-TOF gate");
    const auto after327 = at327.process(frame(908.9, 15.0, normal));
    check(after327.available && !isFinalFallback(after327)
              && after327.templateCorrelation > 0.9,
          "Frame_0327 fallback cannot overwrite the NCC template");
    check(std::abs(after327.predictedTofSamples - recovered327.tofSamples) < 0.01,
          "Frame_0327 fallback must update next-frame predicted peak to 908.7022");

    // New prediction search radius: a measured peak up to 32 samples from
    // prediction is eligible ONLY if close enough to the last primary TOF.
    Tracker overshootWindow;
    for (int i = 0; i < 7; ++i)
        overshootWindow.process(frame(850.0 + 6.0 * i, 18.0, normal));
    const auto largeResidual = overshootWindow.process(frame(876.0, 6.0, normal));
    check(!largeResidual.available || isFinalFallback(largeResidual),
          "expanded prediction must never override a formally certified peak");
    
    // Two provisional outputs can each be <16 samples from the original
    // formally accepted TOF yet disagree by >16 with each other. The
    // predictor update must not permit such a discontinuous second fallback.
    auto consecutive = seeded(normal);
    const auto firstProvisional = consecutive.process(frame(906.5, 5.5, changed));
    check(firstProvisional.available && isFinalFallback(firstProvisional),
          "first provisional peak near the accepted physical identity is allowed");
    const auto oppositeProvisional = consecutive.process(frame(885.0, 3.5, changed));
    check(!oppositeProvisional.available,
          "prediction update cannot publish unrelated opposite-side fallback");

    // 4. The LOST primary state machine still owns misses, cold restarts
    // and warmup voting. Fallback publications add no startup confirmations.
    auto lost = seeded(normal);
    for (int i = 0; i < 3; ++i)
        check(!lost.process(frame(735.0, 25.0, normal)).available,
              "distant packet cannot steal established physical identity");
    const auto lostShadow = lost.process(frame(899.0, 6.0, changed));
    check(lostShadow.available && !lostShadow.certified && isFinalFallback(lostShadow)
              && lostShadow.stage == Tracker::Stage::GlobalSearch,
          "LOST measured fallback cannot force LOCK or relearn");
    const auto firstVote = lost.process(frame(899.2, 10.0, normal));
    check(firstVote.stage == Tracker::Stage::Warmup && firstVote.startupVotes == 1,
          "LOST fallback must not grant an extra startup vote");
    check(std::abs(firstVote.predictedTofSamples - lostShadow.tofSamples) < 0.01,
          "LOST frozen predictor must move to last successful fallback peak");
    check(firstVote.available && !firstVote.certified
              && firstVote.templateCorrelation > 0.9,
          "pending WARMUP peak may be published only as uncertified shadow");
    const auto certified = lost.process(frame(899.4, 10.0, normal));
    check(certified.available && certified.stage == Tracker::Stage::Locked
              && !isFinalFallback(certified),
          "regular two-frame certification must still establish LOCK");

    // 5. Repeated final publications must not perpetuate an expired frozen
    // identity. Check normal long-gap policy still refuses a remote fake.
    auto longLost = seeded(normal);
    for (int i = 0; i < 38; ++i)
        check(!longLost.process(frame(735.0, 25.0, normal)).available,
              "long LOST cannot certify a 160-sample distant packet");

    // 6. A/B invariant: before the first fallback, primary measurements must
    // be identical. Afterwards the next-frame prediction deliberately changes,
    // so future states and TOFs are allowed to differ without relearning.
    Tracker::Config primaryCfg;
    primaryCfg.thickFinalSinglePeakFallbackEnabled = false;
    Tracker onlyPrimary(primaryCfg);
    Tracker primaryWithFallback;
    int shadowPublications = 0;
    std::vector<Tracker::Frame> sequence;
    sequence.push_back(frame(895.0, 15.0, normal));
    sequence.push_back(frame(895.5, 15.0, normal));
    sequence.push_back(frame(896.0, 15.0, normal));
    sequence.push_back(frame(896.5, 15.0, normal));
    sequence.push_back(frame(903.5, 5.8, changed));
    sequence.push_back(frame(897.0, 15.0, normal));
    sequence.push_back(frame(735.0, 25.0, normal));
    sequence.push_back(frame(897.3, 5.8, changed));
    sequence.push_back(frame(897.5, 15.0, normal));
    sequence.push_back(frame(735.0, 25.0, normal));
    sequence.push_back(frame(735.0, 25.0, normal));
    sequence.push_back(frame(899.0, 6.0, changed));
    sequence.push_back(frame(899.3, 9.0, normal));
    sequence.push_back(frame(899.5, 9.0, normal));
    bool hasUpdatedPrediction = false;
    double priorFallbackTof = std::numeric_limits<double>::quiet_NaN();
    for (const auto &f : sequence) {
        const auto expected = onlyPrimary.process(f);
        const auto actual = primaryWithFallback.process(f);
        if (std::isfinite(priorFallbackTof)) {
            check(std::isfinite(actual.predictedTofSamples)
                      && std::abs(actual.predictedTofSamples - priorFallbackTof) < 0.01,
                  "next-frame position follows the published fallback peak");
            hasUpdatedPrediction = true;
        }
        if (shadowPublications == 0 && expected.available) {
            check(actual.available
                      && expected.certified == actual.certified
                      && std::abs(expected.tofSamples - actual.tofSamples) < 1e-9
                      && std::string(expected.reason) == actual.reason,
                  "prior to first fallback primary publication must match exactly");
        }
        priorFallbackTof = std::numeric_limits<double>::quiet_NaN();
        if (isFinalFallback(actual)) {
            ++shadowPublications;
            check(actual.available && !actual.certified,
                  "final fallback can publish only uncertified output");
            priorFallbackTof = actual.tofSamples;
        }
    }
    check(hasUpdatedPrediction, "A/B sequence should demonstrate updated fallback prediction");
    check(shadowPublications > 0, "A/B sequence must exercise the last-resort fallback");

    // 7. No changes to the separate thin-wall T2 ordinal/period path.
    Tracker thin;
    Tracker::Frame thinFrame;
    thinFrame.referenceTofSamples = 875.0;
    thinFrame.formalT1TofSamples = 334.0;
    thinFrame.periodSamples = 541.0;
    thinFrame.periodEvidenceAvailable = true;
    thinFrame.candidates.push_back({875.0, 12.0, 1.2, 0, normal});
    const auto thinResult = thin.process(thinFrame);
    check(thinResult.available && thinResult.certified
              && thinResult.deltaTStrongSupport,
          "thin-wall certified ordinal track remains intact");

    std::cout << "PASS thick T2: primary-first publication-only fallback, "
                 "no-learning, LOST votes, remote packet guard and thin-wall\n";
}
