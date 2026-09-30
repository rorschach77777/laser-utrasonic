#pragma once

#include <algorithm>
#include <cmath>
#include <deque>
#include <limits>
#include <vector>

class FirstEchoStartupTracker
{
public:
    struct Candidate
    {
        double tofSamples = std::numeric_limits<double>::quiet_NaN();
        double score = 0.0;
        double supportSnrDb = std::numeric_limits<double>::quiet_NaN();
        int rank = 0;
    };

    struct Result
    {
        bool pending = false;
        bool confirmed = false;
        int framesObserved = 0;
        int votes = 0;
        int clusterCount = 0;
        int selectedRank = 0;
        int previousClock = 0;
        int currentClock = 0;
        double previousTofSamples = std::numeric_limits<double>::quiet_NaN();
        double currentTofSamples = std::numeric_limits<double>::quiet_NaN();
        double consensusTofSamples = std::numeric_limits<double>::quiet_NaN();
        double meanSupportSnrDb = std::numeric_limits<double>::quiet_NaN();
        double meanScore = std::numeric_limits<double>::quiet_NaN();
    };

    void reset()
    {
        m_frames.clear();
    }

    Result observe(int physicalClock, const std::vector<Candidate> &candidates)
    {
        prune(physicalClock);

        Frame frame;
        frame.clock = physicalClock;
        frame.candidates.reserve(std::min<size_t>(3, candidates.size()));
        for (const Candidate &candidate : candidates) {
            if (!std::isfinite(candidate.tofSamples)
                || !std::isfinite(candidate.supportSnrDb)
                || candidate.supportSnrDb < MinimumSupportSnrDb)
                continue;
            frame.candidates.push_back(candidate);
            if (frame.candidates.size() >= 3)
                break;
        }
        m_frames.push_back(std::move(frame));
        prune(physicalClock);

        Result best;
        best.currentClock = physicalClock;
        for (const Frame &stored : m_frames) {
            if (!stored.candidates.empty())
                ++best.framesObserved;
        }

        const Frame *current = nullptr;
        for (const Frame &stored : m_frames) {
            if (stored.clock == physicalClock) {
                current = &stored;
                break;
            }
        }
        if (!current || current->candidates.empty())
            return best;

        struct Cluster
        {
            int votes = 0;
            int currentRank = 0;
            int previousClock = 0;
            double previousTof = std::numeric_limits<double>::quiet_NaN();
            double currentTof = std::numeric_limits<double>::quiet_NaN();
            double consensus = std::numeric_limits<double>::quiet_NaN();
            double meanSnr = std::numeric_limits<double>::quiet_NaN();
            double meanScore = std::numeric_limits<double>::quiet_NaN();
        };

        std::vector<Cluster> clusters;
        for (const Candidate &currentCandidate : current->candidates) {
            std::vector<Candidate> members;
            std::vector<int> memberClocks;
            members.push_back(currentCandidate);
            memberClocks.push_back(physicalClock);

            for (const Frame &stored : m_frames) {
                if (stored.clock == physicalClock || stored.candidates.empty())
                    continue;
                const Candidate *nearest = nullptr;
                double nearestDistance = MatchingToleranceSamples + 1.0;
                for (const Candidate &candidate : stored.candidates) {
                    const double distance = std::abs(candidate.tofSamples - currentCandidate.tofSamples);
                    if (distance <= MatchingToleranceSamples && distance < nearestDistance) {
                        nearest = &candidate;
                        nearestDistance = distance;
                    }
                }
                if (nearest) {
                    members.push_back(*nearest);
                    memberClocks.push_back(stored.clock);
                }
            }

            Cluster cluster;
            cluster.votes = static_cast<int>(members.size());
            cluster.currentRank = currentCandidate.rank;
            cluster.currentTof = currentCandidate.tofSamples;

            std::vector<double> tofs;
            tofs.reserve(members.size());
            double scoreSum = 0.0;
            double snrSum = 0.0;
            int snrCount = 0;
            for (size_t i = 0; i < members.size(); ++i) {
                tofs.push_back(members[i].tofSamples);
                scoreSum += members[i].score;
                if (std::isfinite(members[i].supportSnrDb)) {
                    snrSum += members[i].supportSnrDb;
                    ++snrCount;
                }
                if (memberClocks[i] != physicalClock
                    && memberClocks[i] > cluster.previousClock) {
                    cluster.previousClock = memberClocks[i];
                    cluster.previousTof = members[i].tofSamples;
                }
            }
            std::sort(tofs.begin(), tofs.end());
            const size_t middle = tofs.size() / 2;
            cluster.consensus = tofs.size() % 2
                ? tofs[middle] : 0.5 * (tofs[middle - 1] + tofs[middle]);
            cluster.meanScore = scoreSum / std::max<size_t>(1, members.size());
            cluster.meanSnr = snrCount > 0 ? snrSum / snrCount
                                           : std::numeric_limits<double>::quiet_NaN();
            clusters.push_back(cluster);
        }
        best.clusterCount = static_cast<int>(clusters.size());
        if (clusters.empty())
            return best;

        const auto better = [](const Cluster &a, const Cluster &b) {
            if (a.votes != b.votes)
                return a.votes > b.votes;
            const double aSnr = std::isfinite(a.meanSnr) ? a.meanSnr : -1e9;
            const double bSnr = std::isfinite(b.meanSnr) ? b.meanSnr : -1e9;
            if (std::abs(aSnr - bSnr) > 1e-9)
                return aSnr > bSnr;
            if (std::abs(a.meanScore - b.meanScore) > 1e-12)
                return a.meanScore > b.meanScore;
            return a.currentRank < b.currentRank;
        };
        const Cluster &winner = *std::min_element(
            clusters.begin(), clusters.end(),
            [&](const Cluster &a, const Cluster &b) { return better(a, b); });

        best.votes = winner.votes;
        best.selectedRank = winner.currentRank;
        best.previousClock = winner.previousClock;
        best.previousTofSamples = winner.previousTof;
        best.currentTofSamples = winner.currentTof;
        best.consensusTofSamples = winner.consensus;
        best.meanSupportSnrDb = winner.meanSnr;
        best.meanScore = winner.meanScore;
        best.confirmed = winner.votes >= RequiredVotes && winner.previousClock > 0;
        best.pending = !best.confirmed;
        return best;
    }

private:
    struct Frame
    {
        int clock = 0;
        std::vector<Candidate> candidates;
    };

    void prune(int currentClock)
    {
        while (!m_frames.empty() && m_frames.front().clock < currentClock - (WindowFrames - 1))
            m_frames.pop_front();
    }

    static constexpr int WindowFrames = 3;
    static constexpr int RequiredVotes = 2;
    static constexpr double MatchingToleranceSamples = 3.0;
    static constexpr double MinimumSupportSnrDb = 7.0;
    std::deque<Frame> m_frames;
};
