                    bestRho13 = rho13;
                }
                if (bestThird >= 0) {
                    const auto &echo3 = packetCandidates[static_cast<size_t>(bestThird)];
                    echo2.echo3CandidateIndex = bestThird;
                    echo2.rho23 = bestRho23;
                    echo2.rho13 = bestRho13;
                    echo2.energyRatio3 = echo3.energyRatio;
                    echo2.periodDifference = bestPeriodDifference;
                    echo2.chainCertified = echo2.rho12 >= minChainCorrelation12
                        && echo2.rho23 >= minChainCorrelation23
                        && echo2.rho13 >= minChainCorrelation13
                        && echo2.energyRatio >= minEcho2EnergyRatio
                        && echo2.energyRatio3 >= minEcho3EnergyRatio;
                }
            }

            // 候选周期族的整数倍支持。完全基于当前帧候选之间的几何关系，不使用
            // thicknessStdMm/First Echo厚度。2×/3×位置存在独立包时，优先认为较小周期
            // 是一个可重复的基本回波周期，而不是偶然相似片段。
            constexpr double harmonicMinCorrelation = 0.80;
            for (int i = 0; i < static_cast<int>(packetCandidates.size()); ++i) {
                auto &base = packetCandidates[static_cast<size_t>(i)];
                if (!(base.period > 0.0))
                    continue;
                for (int multiple = 2; multiple <= 3; ++multiple) {
                    const double target = multiple * base.period;
                    const double tolerance = std::max(6.0, 0.020 * target);
                    int bestIndex = -1;
                    double bestError = std::numeric_limits<double>::infinity();
                    for (int j = i + 1; j < static_cast<int>(packetCandidates.size()); ++j) {
                        const auto &harmonic = packetCandidates[static_cast<size_t>(j)];
                        if (harmonic.rho12 < harmonicMinCorrelation)
                            continue;
                        const double error = std::abs(harmonic.period - target);
                        if (error < bestError) {
                            bestError = error;
                            bestIndex = j;
                        }
                    }
                    if (bestIndex < 0 || bestError > tolerance)
                        continue;
                    const auto &harmonic = packetCandidates[static_cast<size_t>(bestIndex)];
                    // 后续回波允许略有增强，但若能量比前级高出一个数量级，更像另一模态/干扰族。
                    const double allowedHarmonicEnergy = std::max(
                        0.20, 3.0 * std::max(base.energyRatio, minEcho2EnergyRatio));
                    if (harmonic.energyRatio > allowedHarmonicEnergy)
                        continue;
                    ++base.harmonicSupportCount;
                    base.harmonicErrorScore += std::clamp(
                        1.0 - bestError / tolerance, 0.0, 1.0);
                }
                const double waveformScore = std::isfinite(base.waveformRho12)
                    ? std::abs(base.waveformRho12) : 0.0;
                const double harmonicScore = std::min(1.0,
                    0.5 * static_cast<double>(base.harmonicSupportCount));
                base.familyScore = 0.60 * base.rho12
                    + 0.12 * waveformScore
                    + 0.18 * harmonicScore
                    + 0.10 * std::sqrt(std::clamp(base.energyRatio, 0.0, 1.0));
                if (base.chainCertified)
                    base.familyScore += 0.12;
            }

            decision.independentEchoPairCandidateCount = static_cast<int>(packetCandidates.size());
            {
                QStringList summary;
                const int summaryCount = std::min(50, static_cast<int>(packetCandidates.size()));
                for (int i = 0; i < summaryCount; ++i) {
                    const auto &candidate = packetCandidates[static_cast<size_t>(i)];
                    summary << QStringLiteral("d%1:r%2:w%3:e%4:h%5:c%6")
                        .arg(candidate.period, 0, 'f', 2)
                        .arg(candidate.rho12, 0, 'f', 3)
                        .arg(candidate.waveformRho12, 0, 'f', 3)
                        .arg(candidate.energyRatio, 0, 'f', 3)
                        .arg(candidate.harmonicSupportCount)
                        .arg(candidate.chainCertified ? 1 : 0);
                }
                decision.independentEchoPairCandidateSummary = summary.join(QLatin1Char('|'));
            }

            auto candidateWaveformAbs = [&](const IndependentPacketCandidate &candidate) {
                return std::isfinite(candidate.waveformRho12)
                    ? std::abs(candidate.waveformRho12) : 0.0;
            };
            auto periodicFundamentalEligible = [&](const IndependentPacketCandidate &candidate) {
                const double waveformAbs = candidateWaveformAbs(candidate);
                return (candidate.chainCertified || candidate.harmonicSupportCount >= 1)
                    && candidate.rho12 >= std::max(0.78, p.echoMinCorrelation)
                    && waveformAbs >= 0.20;
            };
            auto strongSameWaveformEligible = [&](const IndependentPacketCandidate &candidate) {
                return candidate.rho12 >= 0.92
                    && candidateWaveformAbs(candidate) >= 0.72;
            };
            auto earliestFundamentalCandidate = [&]() {
                // 首次GLOBAL优先选“能被2P/3P支持的最小可信周期”，而不是后方最强回波。
                // 只有当前帧没有周期族证据时，才退化到最早的超高同波形相关候选。
                for (int i = 0; i < static_cast<int>(packetCandidates.size()); ++i) {
                    if (periodicFundamentalEligible(packetCandidates[static_cast<size_t>(i)]))
                        return i;
                }
                for (int i = 0; i < static_cast<int>(packetCandidates.size()); ++i) {
                    if (strongSameWaveformEligible(packetCandidates[static_cast<size_t>(i)]))
                        return i;
                }
                return -1;
            };
            auto isFrozenHarmonicAlias = [&](double period, double frozen) {
                if (!std::isfinite(period) || !std::isfinite(frozen) || frozen <= 0.0)
                    return false;
                for (int multiple = 2; multiple <= 3; ++multiple) {
                    const double target = multiple * frozen;
                    const double gate = std::max(12.0, 0.06 * target);
                    if (std::abs(period - target) <= gate)
                        return true;
                }
                return false;
            };

            int strongSeed = -1;
            bool rejectedFrozenHarmonicAlias = false;
            QString globalIdentityPolicy;
            EchoPairTracker::StartupConsensus startupConsensus;
            const double frozenPreviousPeriod = history
                ? history->echoPairTracker.frozenPreviousPeriod() : NaN;
            double frozenIdentityGate = NaN;
            if (std::isfinite(frozenPreviousPeriod) && frozenPreviousPeriod > 0.0) {
                // LOST重捕获：frozen period只作身份先验，不限制全局扫描范围。
                // 在±15%附近存在可信候选时优先回到原fundamental；若只看到2x/3x，
                // 明确拒绝本帧，不允许谐波族直接成为新的Echo2。
                // frozen prior只用于“同一Echo2身份”的重捕获，因此核心身份门必须明显
                // 小于完整GLOBAL允许的真实周期变化范围。此前±15%在~500 sample周期上
                // 相当于±75点，会把另一个独立回波包也当成原Echo2，150655中出现过
                // 496->537 sample的假跳变。这里收紧到约±5%（至少14点）；超出核心门
                // 的候选仍可走下方NEW_FUNDAMENTAL三帧确认，不会永久禁止真实厚度变化。
                frozenIdentityGate = std::max(14.0, 0.05 * frozenPreviousPeriod);
                const double frozenGate = frozenIdentityGate;
                double bestScore = -std::numeric_limits<double>::infinity();
                for (int i = 0; i < static_cast<int>(packetCandidates.size()); ++i) {
                    const auto &candidate = packetCandidates[static_cast<size_t>(i)];
                    const double residual = std::abs(candidate.period - frozenPreviousPeriod);
                    if (residual > frozenGate
                        || candidate.rho12 < std::max(0.80, p.echoMinCorrelation)
                        || candidateWaveformAbs(candidate) < 0.25)
                        continue;
                    const double residualScore = 1.0 - residual / frozenGate;
                    const double score = 0.65 * residualScore
                        + 0.20 * candidate.rho12
                        + 0.15 * std::clamp(candidate.familyScore, 0.0, 1.5) / 1.5;
                    if (score > bestScore) {
                        bestScore = score;
                        strongSeed = i;
                    }
                }
                if (strongSeed >= 0) {
                    globalIdentityPolicy = QStringLiteral("FROZEN_PERIOD_REACQUIRE");
                } else {
                    const int fundamental = earliestFundamentalCandidate();
                    if (fundamental >= 0
                        && isFrozenHarmonicAlias(
                            packetCandidates[static_cast<size_t>(fundamental)].period,
                            frozenPreviousPeriod)) {
                        rejectedFrozenHarmonicAlias = true;
                        globalIdentityPolicy = QStringLiteral("REJECT_FROZEN_HARMONIC_ALIAS");
                    } else if (fundamental >= 0) {
                        // 原周期附近确实没有可信候选时，允许新的非谐波fundamental进入两帧WARMUP。
                        strongSeed = fundamental;
                        globalIdentityPolicy = QStringLiteral("NEW_FUNDAMENTAL_AFTER_FROZEN_MISS");
                    } else {
                        // 即使首个合格fundamental不存在，也要检查强候选是否只是2x/3x旧周期。
                        for (const auto &candidate : packetCandidates) {
                            if ((periodicFundamentalEligible(candidate)
                                 || strongSameWaveformEligible(candidate))
                                && isFrozenHarmonicAlias(candidate.period, frozenPreviousPeriod)) {
                                rejectedFrozenHarmonicAlias = true;
                                globalIdentityPolicy = QStringLiteral("REJECT_FROZEN_HARMONIC_ALIAS");
                                break;
                            }
                        }
                    }
                }
            } else if (history) {
                std::vector<EchoPairTracker::StartupCandidate> startupCandidates;
                startupCandidates.reserve(packetCandidates.size());
                for (const auto &candidate : packetCandidates) {
                    const bool periodic = periodicFundamentalEligible(candidate);
                    const bool strongWaveform = strongSameWaveformEligible(candidate);
                    if (!periodic && !strongWaveform)
                        continue;
                    const double waveformAbs = candidateWaveformAbs(candidate);
                    const double score = std::clamp(
                        0.55 * std::clamp(candidate.familyScore, 0.0, 1.5) / 1.5
                            + 0.20 * std::clamp(candidate.rho12, 0.0, 1.0)
                            + 0.15 * waveformAbs
                            + 0.10 * std::min(1.0,
                                0.5 * static_cast<double>(candidate.harmonicSupportCount)
                                    + (candidate.chainCertified ? 0.5 : 0.0)),
                        0.0, 1.0);
                    startupCandidates.push_back({candidate.period, score, periodic});
                }
                startupConsensus = history->echoPairTracker.observeStartupCandidates(startupCandidates);
                decision.independentEchoPairStartupClusterFramesObserved = startupConsensus.framesObserved;
                decision.independentEchoPairStartupClusterVotes = startupConsensus.frameVotes;
                decision.independentEchoPairStartupClusterPeriodicVotes = startupConsensus.periodicFamilyVotes;
                decision.independentEchoPairStartupClusterCount = startupConsensus.clusterCount;
                decision.independentEchoPairStartupConsensusPeriodSamples = startupConsensus.periodSamples;
                decision.independentEchoPairStartupConsensusMeanScore = startupConsensus.meanScore;

                if (startupConsensus.available) {
                    const double clusterGate = std::max(
                        18.0, 0.06 * std::abs(startupConsensus.periodSamples));
                    double bestScore = -std::numeric_limits<double>::infinity();
                    for (int i = 0; i < static_cast<int>(packetCandidates.size()); ++i) {
                        const auto &candidate = packetCandidates[static_cast<size_t>(i)];
                        const bool periodic = periodicFundamentalEligible(candidate);
                        const bool strongWaveform = strongSameWaveformEligible(candidate);
                        if (!periodic && !strongWaveform)
                            continue;
                        const double residual = std::abs(
                            candidate.period - startupConsensus.periodSamples);
                        if (residual > clusterGate)
                            continue;
                        const double residualScore = 1.0 - residual / clusterGate;
                        const double score = 0.65 * residualScore
                            + 0.20 * candidate.rho12
                            + 0.15 * std::clamp(candidate.familyScore, 0.0, 1.5) / 1.5;
                        if (score > bestScore) {
                            bestScore = score;
                            strongSeed = i;

                        }
                    }
                    globalIdentityPolicy = strongSeed >= 0
                        ? QStringLiteral("INITIAL_CLUSTER_CONSENSUS")
                        : QStringLiteral("INITIAL_CLUSTER_WAIT_CURRENT_FRAME");
                } else {
                    globalIdentityPolicy = QStringLiteral("INITIAL_CLUSTER_COLLECT");
                }
            } else {
                strongSeed = earliestFundamentalCandidate();
                if (strongSeed >= 0) {
                    const auto &candidate = packetCandidates[static_cast<size_t>(strongSeed)];
                    globalIdentityPolicy = periodicFundamentalEligible(candidate)
                        ? QStringLiteral("INITIAL_FUNDAMENTAL_PERIOD_FAMILY")
                        : QStringLiteral("INITIAL_STRONG_WAVEFORM_FALLBACK");
                }
            }
            decision.independentEchoPairGlobalIdentityPolicy = globalIdentityPolicy;
            if (strongSeed >= 0) {
                const auto &selected = packetCandidates[static_cast<size_t>(strongSeed)];
                PairCorrelationMatch envelopeMatch;
                if (selected.alignedIndex >= 0
                    && selected.alignedIndex < static_cast<int>(allSamples.size())) {
                    const int centerLag = allSamples[static_cast<size_t>(selected.alignedIndex)].lag;
                    envelopeMatch = searchEnvelopeLag(centerLag - 2, centerLag + 2);
                }
                double globalRawPreferred = NaN;
                if (envelopeMatch.available && std::isfinite(frozenPreviousPeriod)
                    && std::abs(envelopeMatch.lag - frozenPreviousPeriod) <= 6.0)
                    globalRawPreferred = frozenPreviousPeriod;
                const PairCorrelationMatch rawMatch = envelopeMatch.available
                    ? refineRawLag(envelopeMatch.lag, globalRawPreferred) : PairCorrelationMatch{};
                bool measurementReady = envelopeMatch.available
                    && envelopeMatch.correlation >= std::max(0.80, p.echoMinCorrelation)
                    && rawMatch.available && rawMatch.absoluteCorrelation >= 0.25;
                // 候选T50通过frozen门后，Envelope/Raw精定位仍可能漂到门外。若本帧声明
                // “重捕获原Echo2”，必须对最终身份位置再复核；Raw允许额外4点载波相位余量。
                if (measurementReady
                    && globalIdentityPolicy == QStringLiteral("FROZEN_PERIOD_REACQUIRE")
                    && std::isfinite(frozenIdentityGate)) {
                    const bool envelopeStillSameIdentity = std::abs(
                        envelopeMatch.lag - frozenPreviousPeriod) <= frozenIdentityGate;
                    const bool rawStillSameIdentity = std::abs(
                        rawMatch.lag - frozenPreviousPeriod) <= frozenIdentityGate + 4.0;
                    if (!envelopeStillSameIdentity || !rawStillSameIdentity) {
                        measurementReady = false;
                        globalIdentityPolicy = QStringLiteral(
                            "FROZEN_REFINED_OUTSIDE_IDENTITY_GATE");
                        decision.independentEchoPairGlobalIdentityPolicy = globalIdentityPolicy;
                    }
                }

                bool outputAuthorized = false;
                if (!history) {
                    outputAuthorized = measurementReady;
                    decision.independentEchoPairSelectionPolicy = outputAuthorized
                        ? QStringLiteral("SINGLE_FRAME_GLOBAL_ENVELOPE_RAW")
                        : QStringLiteral("SINGLE_FRAME_GLOBAL_REJECTED");
                } else if (measurementReady) {
                    const int requiredConfirmations =
                        globalIdentityPolicy == QStringLiteral("NEW_FUNDAMENTAL_AFTER_FROZEN_MISS")
                        ? 3 : 2;
                    outputAuthorized = history->echoPairTracker.observeGlobalCandidate(
                        rawMatch.lag, requiredConfirmations);
                    decision.independentEchoPairTrackerStage = EchoPairTracker::stageName(
                        history->echoPairTracker.stage());
                    decision.independentEchoPairPeriodHistoryCount = history->echoPairTracker.historyCount();
                    decision.independentEchoPairPredictedPeriodSamples = history->echoPairTracker.predictedPeriod();
                    decision.independentEchoPairPeriodModelReady =
                        std::isfinite(decision.independentEchoPairPredictedPeriodSamples);
                    decision.independentEchoPairSelectionPolicy = outputAuthorized
                        ? QStringLiteral("GLOBAL_WARMUP_LOCK_ACQUIRED")
                        : QStringLiteral("GLOBAL_WARMUP_CONFIRM");
                } else if (history) {
                    history->echoPairTracker.observeMiss();
                    decision.independentEchoPairTrackerStage = EchoPairTracker::stageName(
                        history->echoPairTracker.stage());
                    decision.independentEchoPairSelectionPolicy = QStringLiteral("GLOBAL_NO_STRONG_RAW_MATCH");
                }

                decision.independentEchoPairHarmonicSupportCount = selected.harmonicSupportCount;
                if (outputAuthorized) {
                    publishTrackedPair(
                        envelopeMatch, rawMatch,
                        QStringLiteral("ECHO_PAIR_GLOBAL_LOCK_ACQUIRED"),
                        decision.independentEchoPairSelectionPolicy);
                } else {
                    decision.independentEchoPairStatus = QStringLiteral("ECHO_PAIR_GLOBAL_WARMUP");
                }
            } else {
                if (history)
                    history->echoPairTracker.observeMiss();
                decision.independentEchoPairTrackerStage = history
                    ? EchoPairTracker::stageName(history->echoPairTracker.stage())
                    : QStringLiteral("GLOBAL_SEARCH");
                decision.independentEchoPairSelectionPolicy = rejectedFrozenHarmonicAlias
                    ? QStringLiteral("GLOBAL_REJECT_FROZEN_HARMONIC_ALIAS")
                    : (!globalIdentityPolicy.isEmpty()
                        ? globalIdentityPolicy
                        : QStringLiteral("GLOBAL_NO_PERIOD_FAMILY"));
                decision.independentEchoPairStatus = rejectedFrozenHarmonicAlias
                    ? QStringLiteral("ECHO_PAIR_GLOBAL_HARMONIC_ALIAS_REJECTED")
                    : (globalIdentityPolicy.startsWith(QStringLiteral("INITIAL_CLUSTER"))
                        ? QStringLiteral("ECHO_PAIR_GLOBAL_STARTUP_CLUSTERING")
                        : QStringLiteral("ECHO_PAIR_GLOBAL_PENDING"));
            }
            } // runGlobalSearch
        }
        decision.independentEchoPairProcessingMs = echoPairTimer.nsecsElapsed() / 1e6;
    }
