                    && std::abs(lockResidual) > 5.5 && std::abs(lockResidual) <= 6.0
                    && envelopeMatch.correlation < 0.90
                    && rawPhaseBranchIdentityResolved
                    && !rawPhaseBranchSelectionApplied
                    && std::isfinite(decision.independentEchoPairRawPhaseBranchResidualErrorSamples)
                    && decision.independentEchoPairRawPhaseBranchResidualErrorSamples > 1.9;
                // phase identity若已把Raw纠正回<=6 sample，就不再把原始远Raw视作大位移。
                const bool largeExpandedRecovery = rawRecenteredToEnvelope
                    && std::isfinite(effectiveLockResidual)
                    && std::abs(effectiveLockResidual) > 8.0;
                const bool largeRecoveryEcho3Certified = largeExpandedRecovery
                    && echo3CertifiesTrackedPair(rawMatch);
                const bool recoveryNeedsTemporalConfirmation =
                    recenterResolvedForTemporalVote || recenterBoundaryAmbiguous
                    || largeRecoveryEcho3Certified;
                const bool recoveryOutputSuppressed = recoveryNeedsTemporalConfirmation
                    || recenterPhaseUnresolved || (largeExpandedRecovery && !largeRecoveryEcho3Certified);
                if (softEnvelopeAccepted) {
                    decision.independentEchoPairPeriodResidualSamples = lockResidual;
                    history->echoPairTracker.observeLocked(rawMatch.lag);
                    publishTrackedPair(
                        envelopeMatch, rawMatch,
                        QStringLiteral("ECHO_PAIR_LOCKED_SOFT_ENVELOPE_RECOVERY"),
                        QStringLiteral("LOCK_SOFT_ENVELOPE_HIGH_RAW_NCC"));
                    decision.independentEchoPairTrackerStage = QStringLiteral("LOCK");
                    decision.independentEchoPairPeriodHistoryCount = history->echoPairTracker.historyCount();
                } else if (enhancedEnergyAccepted) {
                    decision.independentEchoPairPeriodResidualSamples = enhancedResidual;
                    history->echoPairTracker.observeLocked(enhancedRawMatch.lag);
                    publishTrackedPair(
                        enhancedEnergyDiagnostic.match, enhancedRawMatch,
                        QStringLiteral("ECHO_PAIR_LOCKED_ENHANCED_ENVELOPE_RECOVERY"),
                        QStringLiteral("LOCK_ENHANCED_ENVELOPE_HIGH_RAW_NCC"));
                    decision.independentEchoPairTrackerStage = QStringLiteral("LOCK");
                    decision.independentEchoPairPeriodHistoryCount = history->echoPairTracker.historyCount();
                } else if (matchedRawAuxiliaryAccepted) {
                    decision.independentEchoPairPeriodResidualSamples = matchedRawResidual;
                    history->echoPairTracker.observeLocked(matchedRawDiagnostic.lag);
                    publishTrackedPair(
                        matchedRawDiagnostic, matchedRawDiagnostic,
                        QStringLiteral("ECHO_PAIR_LOCKED_MATCHED_RAW_AUX_IDENTITY"),
                        QStringLiteral("LOCK_MATCHED_RAW_AUX_IDENTITY"));
                    // 该通道没有正式Envelope身份匹配，Envelope字段保持NaN；具体弱包络/
                    // 子带证据保存在Diagnostic字段中，避免把Raw相关误标成Envelope相关。
                    decision.independentEchoPairEnvelopeLagSamples = NaN;
                    decision.independentEchoPairEnvelopeCorrelation = NaN;
                    decision.independentEchoPairEnvelopeMargin = NaN;
                    decision.independentEchoPairTrackerStage = QStringLiteral("LOCK");
                    decision.independentEchoPairPeriodHistoryCount = history->echoPairTracker.historyCount();
                } else if (weakRawTemporalResult.confirmed && weakRawTemporalMatch.available) {
                    decision.independentEchoPairPeriodResidualSamples =
                        weakRawTemporalMatch.lag - trackingPlan.predictedPeriodSamples;
                    // 弱时序确认只授权“当前帧测量输出”，不反向更新正式EchoPairTracker。
                    // 主Tracker仍按原本的miss节奏推进8->16->32->GLOBAL，避免弱证据改变
                    // GLOBAL发生在哪个物理帧，从而间接切换到另一周期族。
                    history->echoPairTracker.observeMiss();
                    const bool threeFrame = weakRawTemporalResult.requiredFrames == 3;
                    publishTrackedPair(
                        weakRawTemporalMatch, weakRawTemporalMatch,
                        threeFrame
                            ? QStringLiteral("ECHO_PAIR_WEAK_RAW_TEMPORAL_CONFIRMED_3")
                            : QStringLiteral("ECHO_PAIR_WEAK_RAW_TEMPORAL_CONFIRMED_2"),
                        threeFrame
                            ? QStringLiteral("LOCK_WEAK_RAW_TEMPORAL_CONFIRMED_3_OUTPUT_ONLY")
                            : QStringLiteral("LOCK_WEAK_RAW_TEMPORAL_CONFIRMED_2_OUTPUT_ONLY"));
                    decision.independentEchoPairEnvelopeLagSamples = NaN;
                    decision.independentEchoPairEnvelopeCorrelation = NaN;
                    decision.independentEchoPairEnvelopeMargin = NaN;
                    decision.independentEchoPairTrackerStage = EchoPairTracker::stageName(
                        history->echoPairTracker.stage());
                    decision.independentEchoPairPeriodHistoryCount = history->echoPairTracker.historyCount();
                } else if (envelopeAccepted && rawAccepted
                           && !recoveryOutputSuppressed) {
                    const PairCorrelationMatch &outputRawMatch =
                        rawPhaseBranchSelectionApplied ? phaseOutputRawMatch : rawMatch;
                    decision.independentEchoPairPeriodResidualSamples = outputRawMatch.lag
                        - trackingPlan.predictedPeriodSamples;
                    // phase identity只改本帧输出，不改正式物理周期历史。
                    history->echoPairTracker.observeLocked(rawMatch.lag);
                    if (!rawRecenteredToEnvelope
                        && envelopeMatch.correlation >= 0.94
                        && outputRawMatch.absoluteCorrelation >= 0.85) {
                        const auto reliablePhaseMatches = searchRawPhaseCandidates(envelopeMatch.lag);
                        std::vector<RawPhaseBranchTracker::Candidate> reliableCandidates;
                        reliableCandidates.reserve(reliablePhaseMatches.size());
                        for (const auto &candidate : reliablePhaseMatches)
                            reliableCandidates.push_back({candidate.lag, candidate.absoluteCorrelation});
                        history->rawPhaseBranchTracker.observeReliable(
                            envelopeMatch.lag, outputRawMatch.lag,
                            outputRawMatch.absoluteCorrelation, reliableCandidates);
                        decision.independentEchoPairRawPhaseBranchModelReady =
                            history->rawPhaseBranchTracker.ready();
                        decision.independentEchoPairRawPhaseBranchHistoryCount =
                            history->rawPhaseBranchTracker.historyCount();
                        decision.independentEchoPairRawPhaseBranchCenterResidualSamples =
                            history->rawPhaseBranchTracker.branchCenterResidualSamples();
                    }
                    publishTrackedPair(
                        envelopeMatch, outputRawMatch,
                        largeRecoveryEcho3Certified
                            ? QStringLiteral("ECHO_PAIR_LOCKED_ECHO3_CERTIFIED_RECOVERY")
                            : QStringLiteral("ECHO_PAIR_LOCKED_TEMPLATE_TRACK"),
                        largeRecoveryEcho3Certified
                            ? QStringLiteral("LOCK_EXPANDED_RECOVERY_ECHO3_CERTIFIED")
                            : (rawPhaseBranchSelectionApplied
                                ? QStringLiteral("LOCK_ENVELOPE_PHASE_BRANCH_RAW_NCC")
                                : (rawRecenteredToEnvelope
                                    ? QStringLiteral("LOCK_ENVELOPE_RECENTER_RAW_NCC")
                                    : QStringLiteral("LOCK_ENVELOPE_NCC_RAW_NCC"))));
                    decision.independentEchoPairTrackerStage = QStringLiteral("LOCK");
                    decision.independentEchoPairPeriodHistoryCount = history->echoPairTracker.historyCount();
                } else if (envelopeAccepted && rawAccepted
                           && recoveryNeedsTemporalConfirmation) {
                    // 两帧确认只控制“是否发布”，主EchoPairTracker仍按旧逻辑接收本帧Raw，
                    // 从而保持原有预测/窗口/GLOBAL节奏。这样安全门不会反向制造大面积
                    // 检出率回归；单帧远恢复虽然参与稳健median历史，但不会直接输出。
                    int recoverySourceMask = 0;
                    if (recenterResolvedForTemporalVote || recenterBoundaryAmbiguous)
                        recoverySourceMask |= 1; // phase-resolved RECENTER / boundary RECENTER
                    if (largeRecoveryEcho3Certified)
                        recoverySourceMask |= 2; // Echo3-certified
                    const auto recoveryVote = history->recoveryTemporalTracker.observe(
                        rawMatch.lag, recoverySourceMask);
                    history->echoPairTracker.observeLocked(rawMatch.lag);
                    decision.independentEchoPairTrackerStage = QStringLiteral("LOCK");
                    decision.independentEchoPairPeriodHistoryCount = history->echoPairTracker.historyCount();
                    if (recoveryVote.confirmed) {
                        decision.independentEchoPairPeriodResidualSamples = lockResidual;
                        QString recoveryPolicy = QStringLiteral("LOCK_RECOVERY_TEMPORAL_CONFIRMED_2_OF_3");
                        if ((recoveryVote.sourceMask & 3) == 3)
                            recoveryPolicy += QStringLiteral("_PHASE_ECHO3");
                        else if ((recoveryVote.sourceMask & 2) != 0)
                            recoveryPolicy += QStringLiteral("_ECHO3");
                        else if ((recoveryVote.sourceMask & 1) != 0)
                            recoveryPolicy += QStringLiteral("_PHASE");
                        publishTrackedPair(
                            envelopeMatch, rawMatch,
                            QStringLiteral("ECHO_PAIR_LOCKED_RECOVERY_TEMPORAL_CONFIRMED_2_OF_3"),
                            recoveryPolicy);
                    } else {
                        decision.independentEchoPairStatus =
                            QStringLiteral("ECHO_PAIR_LOCK_RECOVERY_PENDING_OUTPUT_SUPPRESSED");
                        if (largeRecoveryEcho3Certified) {
                            decision.independentEchoPairSelectionPolicy =
                                QStringLiteral("LOCK_ECHO3_CERTIFIED_PENDING_2_OF_3");
                        } else if (recenterBoundaryAmbiguous) {
                            decision.independentEchoPairSelectionPolicy =
                                QStringLiteral("LOCK_RECENTER_BOUNDARY_AMBIGUOUS_PENDING_2_OF_3");
                        } else {
                            decision.independentEchoPairSelectionPolicy =
                                QStringLiteral("LOCK_RECENTER_PHASE_REQUIRED_PENDING_2_OF_3");
                        }
                    }
                } else if (envelopeAccepted && rawAccepted && recenterPhaseUnresolved) {
                    // phase身份仍未解析：主Tracker保持原节奏吸收观测，但当前测量不发布，
                    // 且不给RecoveryTemporalConfirmTracker投票，防止两个稳定错误载波峰绕过身份门。
                    history->echoPairTracker.observeLocked(rawMatch.lag);
                    decision.independentEchoPairTrackerStage = QStringLiteral("LOCK");
                    decision.independentEchoPairPeriodHistoryCount = history->echoPairTracker.historyCount();
                    decision.independentEchoPairStatus =
                        QStringLiteral("ECHO_PAIR_LOCK_RECENTER_PHASE_UNRESOLVED_OUTPUT_SUPPRESSED");
                    decision.independentEchoPairSelectionPolicy =
                        QStringLiteral("LOCK_RECENTER_PHASE_UNRESOLVED_NO_TEMPORAL_VOTE");
                } else if (weakRawTemporalResult.pending) {
                    history->echoPairTracker.observeMiss();
                    decision.independentEchoPairStatus = QStringLiteral("ECHO_PAIR_LOCK_WEAK_RAW_TEMPORAL_PENDING");
                    decision.independentEchoPairSelectionPolicy =
                        weakRawTemporalResult.requiredFrames == 3
                            ? QStringLiteral("LOCK_WEAK_RAW_TEMPORAL_PENDING_3")
                            : QStringLiteral("LOCK_WEAK_RAW_TEMPORAL_PENDING_2");
                    decision.independentEchoPairTrackerStage = EchoPairTracker::stageName(
                        history->echoPairTracker.stage());
                } else {
                    history->echoPairTracker.observeMiss();
                    decision.independentEchoPairStatus = QStringLiteral("ECHO_PAIR_LOCK_MISS");
                    if (largeExpandedRecovery) {
                        decision.independentEchoPairSelectionPolicy =
                            QStringLiteral("LOCK_MISS_EXPANDED_RECOVERY_UNCONFIRMED");
                    } else if (!envelopeMatch.available) {
                        decision.independentEchoPairSelectionPolicy =
                            QStringLiteral("LOCK_MISS_ENVELOPE_UNAVAILABLE");
                    } else if (envelopeMatch.correlation < std::max(0.80, p.echoMinCorrelation)) {
                        decision.independentEchoPairSelectionPolicy =
                            QStringLiteral("LOCK_MISS_ENVELOPE_CORRELATION");
                    } else if (envelopeMatch.energyRatio < minEcho2EnergyRatio) {
                        decision.independentEchoPairSelectionPolicy =
                            QStringLiteral("LOCK_MISS_ENVELOPE_ENERGY");
                    } else if (!rawMatch.available) {
                        decision.independentEchoPairSelectionPolicy =
                            QStringLiteral("LOCK_MISS_RAW_UNAVAILABLE");
                    } else {
                        decision.independentEchoPairSelectionPolicy =
                            QStringLiteral("LOCK_MISS_RAW_CORRELATION");
                    }
                    decision.independentEchoPairTrackerStage = EchoPairTracker::stageName(
                        history->echoPairTracker.stage());
                }
                // 本帧LOCK失败后不立刻做全局重捕获；下一帧依次8->16->32点扩窗，
                // 第3次失败后下一帧进入GLOBAL_SEARCH。
                runGlobalSearch = false;
            }

            if (runGlobalSearch) {
            if (history)
                history->weakRawTemporalTracker.reset();
            struct EchoIdentityCandidate {
                int lag = 0;
                double rho12 = -1.0;
                double energyRatio2 = 0.0;
                bool hasEcho3 = false;
                double rho23 = -1.0;
                double rho13 = -1.0;
                double energyRatio3 = 0.0;
                bool chainCertified = false;
            };

            std::vector<EchoIdentityCandidate> allSamples;
            if (lagRight >= lagLeft)
                allSamples.reserve(static_cast<size_t>(lagRight - lagLeft + 1));

            for (int lag = lagLeft; lag <= lagRight; ++lag) {
                const int secondLeft = firstLeft + lag;
                double energy2 = 0.0;
                for (int k = 0; k < firstCount; ++k) {
                    const double v = corrected[static_cast<size_t>(secondLeft + k)];
                    energy2 += v * v;
                }
                const double ratio2 = firstEnergy > 1e-24 ? energy2 / firstEnergy : 0.0;
                double rho12 = -1.0;
                if (ratio2 >= minEcho2EnergyRatio) {
                    const double rawRho = packetCorrelation(
                        corrected.data() + firstLeft,
                        corrected.data() + secondLeft,
                        static_cast<size_t>(firstCount));
                    if (std::isfinite(rawRho))
                        rho12 = rawRho;
                }

                EchoIdentityCandidate candidate;
                candidate.lag = lag;
                candidate.rho12 = rho12;
                candidate.energyRatio2 = ratio2;

                const int thirdLeft = firstLeft + 2 * lag;
                if (thirdLeft >= 0 && thirdLeft + firstCount <= n) {
                    candidate.hasEcho3 = true;
                    double energy3 = 0.0;
                    for (int k = 0; k < firstCount; ++k) {
                        const double v = corrected[static_cast<size_t>(thirdLeft + k)];
                        energy3 += v * v;
                    }
                    candidate.energyRatio3 = firstEnergy > 1e-24 ? energy3 / firstEnergy : 0.0;
                    if (candidate.energyRatio3 >= minEcho3EnergyRatio
                        && candidate.rho12 >= minChainCorrelation12) {
                        candidate.rho23 = packetCorrelation(
                            corrected.data() + secondLeft,
                            corrected.data() + thirdLeft,
                            static_cast<size_t>(firstCount));
                        candidate.rho13 = packetCorrelation(
                            corrected.data() + firstLeft,
                            corrected.data() + thirdLeft,
                            static_cast<size_t>(firstCount));
                        candidate.chainCertified = std::isfinite(candidate.rho23)
                            && std::isfinite(candidate.rho13)
                            && candidate.rho23 >= minChainCorrelation23
                            && candidate.rho13 >= minChainCorrelation13;
                    }
                }
                allSamples.push_back(candidate);
            }

            // 第二回波必须先形成“独立能量包”，再允许做形态相关。这里在滑动窗口能量曲线
            // 上做非极大值抑制，避免同一个物理回波内的多个相关局部峰被重复当作Echo2。
            std::vector<int> energyOrder;
            for (int i = 0; i < static_cast<int>(allSamples.size()); ++i) {
                if (allSamples[static_cast<size_t>(i)].energyRatio2 >= minEcho2EnergyRatio)
                    energyOrder.push_back(i);
            }
            std::sort(energyOrder.begin(), energyOrder.end(), [&](int lhs, int rhs) {
                return allSamples[static_cast<size_t>(lhs)].energyRatio2
                    > allSamples[static_cast<size_t>(rhs)].energyRatio2;
            });

            const int packetSuppressionSamples = std::max(firstCount, p.candidateSeparationSamples);
            std::vector<int> packetSeedIndices;
            for (const int index : energyOrder) {
                const int lag = allSamples[static_cast<size_t>(index)].lag;
                bool overlapsExistingPacket = false;
                for (const int existing : packetSeedIndices) {
                    if (std::abs(lag - allSamples[static_cast<size_t>(existing)].lag)
                        < packetSuppressionSamples) {
                        overlapsExistingPacket = true;
                        break;
                    }
                }
                if (!overlapsExistingPacket)
                    packetSeedIndices.push_back(index);
            }
            std::sort(packetSeedIndices.begin(), packetSeedIndices.end(), [&](int lhs, int rhs) {
                return allSamples[static_cast<size_t>(lhs)].lag
                    < allSamples[static_cast<size_t>(rhs)].lag;
            });

            auto energyMedianInWindow = [&](int windowLeft, int windowRight) {
                if (windowLeft < 0 || windowRight <= windowLeft || windowRight >= n)
                    return NaN;
                double total = 0.0;
                for (int x = windowLeft; x <= windowRight; ++x) {
                    const double v = corrected[static_cast<size_t>(x)];
                    total += v * v;
                }
                if (!(total > 1e-24))
                    return NaN;
                const double target = 0.5 * total;
                double cumulativeEnergy = 0.0;
                for (int x = windowLeft; x <= windowRight; ++x) {
                    const double v = corrected[static_cast<size_t>(x)];
                    const double e = v * v;
                    if (cumulativeEnergy + e >= target) {
                        const double fraction = e > 1e-24
                            ? std::clamp((target - cumulativeEnergy) / e, 0.0, 1.0) : 0.0;
                        return static_cast<double>(x) - 0.5 + fraction;
                    }
                    cumulativeEnergy += e;
                }
                return static_cast<double>(windowRight);
            };

            const double firstWindowT50 = energyMedianInWindow(firstLeft, firstRight);
            const double firstIndependentTof = std::isfinite(firstWindowT50)
                ? firstWindowT50 : decision.energyMedianT50Index;

            struct IndependentPacketCandidate {
                int seedIndex = -1;
                int alignedIndex = -1;
                int windowLeft = -1;
                double tof = NaN;
                double period = NaN;
                double rho12 = NaN;
                double waveformRho12 = NaN;
                double energyRatio = NaN;
                int harmonicSupportCount = 0;
                double harmonicErrorScore = 0.0;
                double familyScore = 0.0;
                bool chainCertified = false;
                int echo3CandidateIndex = -1;
                double rho23 = NaN;
                double rho13 = NaN;
                double energyRatio3 = NaN;
                double periodDifference = NaN;
            };
            std::vector<IndependentPacketCandidate> packetCandidates;

            const int alignmentRadius = std::max(2, firstCount / 2);
            for (const int seedIndex : packetSeedIndices) {
                const int seedLag = allSamples[static_cast<size_t>(seedIndex)].lag;
                int bestIndex = -1;
                double bestRho = -1.0;
                for (int lag = std::max(lagLeft, seedLag - alignmentRadius);
                     lag <= std::min(lagRight, seedLag + alignmentRadius); ++lag) {
                    const int index = lag - lagLeft;
                    if (index < 0 || index >= static_cast<int>(allSamples.size()))
                        continue;
                    const auto &sample = allSamples[static_cast<size_t>(index)];
                    if (sample.energyRatio2 < minEcho2EnergyRatio)
                        continue;
                    if (sample.rho12 > bestRho) {
                        bestRho = sample.rho12;
                        bestIndex = index;
                    }
                }
                if (bestIndex < 0 || bestRho < p.echoMinCorrelation)
                    continue;

                const int windowLeft = firstLeft + seedLag;
                const int windowRight = windowLeft + firstCount - 1;
                const double secondTof = energyMedianInWindow(windowLeft, windowRight);
                if (!std::isfinite(firstIndependentTof) || !std::isfinite(secondTof)
                    || secondTof <= firstIndependentTof)
                    continue;
                const double period = secondTof - firstIndependentTof;
                // Echo2必须是Echo1之后的独立重复回波，而不是首回波附近的内部起伏/更早模态。
                // 下限只由当前帧Echo1到达尺度给出，不读取thicknessStdMm或First Echo厚度。
                const double causalPeriodFloor = std::max(
                    static_cast<double>(independentGap), 1.15 * firstIndependentTof);
                if (period < causalPeriodFloor)
                    continue;

                IndependentPacketCandidate candidate;
                candidate.seedIndex = seedIndex;
                candidate.alignedIndex = bestIndex;
                candidate.windowLeft = windowLeft;
                candidate.tof = secondTof;
                candidate.period = period;
                candidate.rho12 = bestRho;
                candidate.waveformRho12 = packetCorrelation(
                    filtered.data() + firstLeft,
                    filtered.data() + windowLeft,
                    static_cast<size_t>(firstCount));
                candidate.energyRatio = allSamples[static_cast<size_t>(seedIndex)].energyRatio2;
                packetCandidates.push_back(candidate);
            }

            // Echo3也必须来自另一个独立能量包。对每个Echo2候选寻找满足P12≈P23的后续包，
            // 再检查1↔2、2↔3、1↔3三组包络相关；这样短周期伪相关无法靠同一局部纹理自证。
            constexpr double maxPeriodDifferenceSamples = 3.0;
            for (int a = 0; a < static_cast<int>(packetCandidates.size()); ++a) {
                auto &echo2 = packetCandidates[static_cast<size_t>(a)];
                const double period12 = echo2.tof - firstIndependentTof;
                double bestPeriodDifference = std::numeric_limits<double>::infinity();
                int bestThird = -1;
                double bestRho23 = NaN;
                double bestRho13 = NaN;
                for (int b = a + 1; b < static_cast<int>(packetCandidates.size()); ++b) {
                    const auto &echo3 = packetCandidates[static_cast<size_t>(b)];
                    const double period23 = echo3.tof - echo2.tof;
                    const double periodDifference = std::abs(period12 - period23);
                    if (periodDifference > maxPeriodDifferenceSamples
                        || periodDifference >= bestPeriodDifference)
                        continue;
                    const double rho23 = packetCorrelation(
                        corrected.data() + echo2.windowLeft,
                        corrected.data() + echo3.windowLeft,
                        static_cast<size_t>(firstCount));
                    const double rho13 = packetCorrelation(
                        corrected.data() + firstLeft,
                        corrected.data() + echo3.windowLeft,
                        static_cast<size_t>(firstCount));
                    if (!std::isfinite(rho23) || !std::isfinite(rho13))
                        continue;
                    bestPeriodDifference = periodDifference;
                    bestThird = b;
                    bestRho23 = rho23;
