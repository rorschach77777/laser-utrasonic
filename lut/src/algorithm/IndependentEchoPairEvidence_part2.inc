                        && std::isfinite(decision.independentEchoPairCorrelation23)
                        && decision.independentEchoPairCorrelation23 >= minChainCorrelation23
                        && decision.independentEchoPairEcho3PeriodDifferenceSamples <= 3.0;
                }
                return true;
            };

            auto echo3CertifiesTrackedPair = [&](const PairCorrelationMatch &rawMatch) {
                if (!rawMatch.available || !std::isfinite(rawMatch.lag) || rawMatch.lag <= 0.0)
                    return false;
                const int thirdCenter = static_cast<int>(std::llround(2.0 * rawMatch.lag));
                const PairCorrelationMatch echo3Match = searchEnvelopeLag(
                    thirdCenter - 4, thirdCenter + 4);
                if (!echo3Match.available || echo3Match.correlation < minChainCorrelation13)
                    return false;
                const int echo2Left = firstLeft + static_cast<int>(std::llround(rawMatch.lag));
                const int echo3Left = firstLeft + static_cast<int>(std::llround(echo3Match.lag));
                if (echo2Left < 0 || echo3Left < 0
                    || echo2Left + firstCount > n || echo3Left + firstCount > n)
                    return false;
                const double rho23 = packetCorrelation(
                    corrected.data() + echo2Left,
                    corrected.data() + echo3Left,
                    static_cast<size_t>(firstCount));
                if (!std::isfinite(rho23) || rho23 < minChainCorrelation23)
                    return false;
                const double p23 = echo3Match.lag - rawMatch.lag;
                return std::abs(rawMatch.lag - p23) <= 3.0;
            };

            EchoPairTracker::SearchPlan trackingPlan;
            if (history)
                trackingPlan = history->echoPairTracker.plan();
            decision.independentEchoPairTrackerStage = EchoPairTracker::stageName(trackingPlan.stage);
            decision.independentEchoPairPredictedPeriodSamples = trackingPlan.predictedPeriodSamples;
            decision.independentEchoPairPeriodModelReady = std::isfinite(trackingPlan.predictedPeriodSamples);
            decision.independentEchoPairSearchHalfWidthSamples = trackingPlan.halfWidthSamples;
            decision.independentEchoPairFrozenPreviousPeriodSamples =
                trackingPlan.frozenPreviousPeriodSamples;
            decision.independentEchoPairReacquiringFromFrozenPeriod =
                trackingPlan.reacquiringFromFrozenPeriod;
            decision.independentEchoPairPeriodHistoryCount = history
                ? history->echoPairTracker.historyCount() : 0;

            bool runGlobalSearch = !history || trackingPlan.useGlobalSearch;
            if (history && !trackingPlan.useGlobalSearch) {
                const int center = static_cast<int>(std::llround(trackingPlan.predictedPeriodSamples));
                const PairCorrelationMatch envelopeMatch = searchEnvelopeLag(
                    center - trackingPlan.halfWidthSamples,
                    center + trackingPlan.halfWidthSamples);
                const bool envelopeEnergyAccepted = envelopeMatch.available
                    && envelopeMatch.energyRatio >= minEcho2EnergyRatio;
                const bool envelopeAccepted = envelopeEnergyAccepted
                    && envelopeMatch.correlation >= std::max(0.80, p.echoMinCorrelation);
                // 差信号软恢复只允许“略低于硬Envelope门”的候选进入Raw复核；
                // 最终还必须满足高Raw相关和紧邻历史周期，不能单独靠降低Envelope阈值输出。
                const bool softEnvelopeCandidate = envelopeEnergyAccepted
                    && !envelopeAccepted
                    && envelopeMatch.correlation >= std::max(0.72, p.echoMinCorrelation);

                DiagnosticPairMatch originalDiagnostic;
                DiagnosticPairMatch localEnergyDiagnostic;
                DiagnosticPairMatch enhancedEnergyDiagnostic;
                PairCorrelationMatch matchedRawDiagnostic;
                PairCorrelationMatch subband10To30Match;
                PairCorrelationMatch subband18To30Match;
                // 正常VALID帧仍仅在正式Envelope身份门未通过时运行旁路增强诊断，避免增加开销。
                // 但当FirstEcho主链已经INVALID时，即使EchoPair Envelope勉强通过，也强制计算
                // matched-raw / 10~30MHz / 18~30MHz只读证据，供DualEchoFusion shadow审查。
                // 后续正式恢复授权仍保留!envelopeAccepted硬条件，因此这里只增加诊断，不改输出。
                const bool forceInvalidFirstEchoDiagnostics =
                    r.measurementStatus != QStringLiteral("VALID");
                if (!envelopeAccepted || forceInvalidFirstEchoDiagnostics) {
                    originalDiagnostic = searchDiagnosticLag(
                        corrected,
                        center - trackingPlan.halfWidthSamples,
                        center + trackingPlan.halfWidthSamples);
                    decision.independentEchoPairDiagnosticOriginalLagSamples =
                        originalDiagnostic.match.lag;
                    decision.independentEchoPairDiagnosticOriginalCorrelation =
                        originalDiagnostic.match.correlation;
                    decision.independentEchoPairDiagnosticOriginalEnergyRatio =
                        originalDiagnostic.match.energyRatio;
                    decision.independentEchoPairDiagnosticOriginalMaxEnergyRatio =
                        originalDiagnostic.maxEnergyRatio;

                    auto energyEnvelopes = buildLocalEnergyEnvelope();
                    localEnergyDiagnostic = searchDiagnosticLag(
                        energyEnvelopes.first,
                        center - trackingPlan.halfWidthSamples,
                        center + trackingPlan.halfWidthSamples);
                    enhancedEnergyDiagnostic = searchDiagnosticLag(
                        energyEnvelopes.second,
                        center - trackingPlan.halfWidthSamples,
                        center + trackingPlan.halfWidthSamples);
                    decision.independentEchoPairDiagnosticLocalEnergyLagSamples =
                        localEnergyDiagnostic.match.lag;
                    decision.independentEchoPairDiagnosticLocalEnergyCorrelation =
                        localEnergyDiagnostic.match.correlation;
                    decision.independentEchoPairDiagnosticLocalEnergyRatio =
                        localEnergyDiagnostic.match.energyRatio;
                    decision.independentEchoPairDiagnosticEnhancedLagSamples =
                        enhancedEnergyDiagnostic.match.lag;
                    decision.independentEchoPairDiagnosticEnhancedCorrelation =
                        enhancedEnergyDiagnostic.match.correlation;
                    decision.independentEchoPairDiagnosticEnhancedEnergyRatio =
                        enhancedEnergyDiagnostic.match.energyRatio;

                    matchedRawDiagnostic = searchMatchedRawLag(
                        center - trackingPlan.halfWidthSamples,
                        center + trackingPlan.halfWidthSamples);
                    decision.independentEchoPairDiagnosticMatchedRawLagSamples =
                        matchedRawDiagnostic.lag;
                    decision.independentEchoPairDiagnosticMatchedRawAbsCorrelation =
                        matchedRawDiagnostic.absoluteCorrelation;
                    decision.independentEchoPairDiagnosticMatchedRawSecondLagSamples =
                        matchedRawDiagnostic.secondLag;
                    decision.independentEchoPairDiagnosticMatchedRawSecondAbsCorrelation =
                        matchedRawDiagnostic.secondAbsoluteCorrelation;
                    decision.independentEchoPairDiagnosticMatchedRawSecondEnergyRatio =
                        matchedRawDiagnostic.secondEnergyRatio;
                    decision.independentEchoPairDiagnosticMatchedRawMargin =
                        matchedRawDiagnostic.margin;
                    decision.independentEchoPairDiagnosticMatchedRawEnergyRatio =
                        matchedRawDiagnostic.energyRatio;

                    const std::vector<double> subband10To30 = buildDiagnosticSubband(10.0, 30.0);
                    subband10To30Match = searchMatchedSignalLag(
                        subband10To30,
                        center - trackingPlan.halfWidthSamples,
                        center + trackingPlan.halfWidthSamples);
                    decision.independentEchoPairDiagnosticSubband10To30LagSamples = subband10To30Match.lag;
                    decision.independentEchoPairDiagnosticSubband10To30AbsCorrelation =
                        subband10To30Match.absoluteCorrelation;
                    decision.independentEchoPairDiagnosticSubband10To30Margin = subband10To30Match.margin;

                    const std::vector<double> subband18To30 = buildDiagnosticSubband(18.0, 30.0);
                    subband18To30Match = searchMatchedSignalLag(
                        subband18To30,
                        center - trackingPlan.halfWidthSamples,
                        center + trackingPlan.halfWidthSamples);
                    decision.independentEchoPairDiagnosticSubband18To30LagSamples = subband18To30Match.lag;
                    decision.independentEchoPairDiagnosticSubband18To30AbsCorrelation =
                        subband18To30Match.absoluteCorrelation;
                    decision.independentEchoPairDiagnosticSubband18To30Margin = subband18To30Match.margin;
                }
                PairCorrelationMatch rawMatch;
                PairCorrelationMatch phaseOutputRawMatch;
                PairCorrelationMatch enhancedRawMatch;
                bool rawRecenteredToEnvelope = false;
                bool rawPhaseBranchIdentityResolved = false;
                bool rawPhaseBranchSelectionApplied = false;
                bool rawPhaseBranchMismatch = false;
                if (history) {
                    decision.independentEchoPairRawPhaseBranchModelReady =
                        history->rawPhaseBranchTracker.ready();
                    decision.independentEchoPairRawPhaseBranchHistoryCount =
                        history->rawPhaseBranchTracker.historyCount();
                    decision.independentEchoPairRawPhaseBranchIndex =
                        history->rawPhaseBranchTracker.branchIndex();
                    decision.independentEchoPairRawPhaseCarrierPeriodSamples =
                        history->rawPhaseBranchTracker.carrierPeriodSamples();
                    decision.independentEchoPairRawPhaseBranchCenterResidualSamples =
                        history->rawPhaseBranchTracker.branchCenterResidualSamples();
                }
                if (envelopeAccepted || softEnvelopeCandidate) {
                    // 正常LOCK时仍让历史Raw DeltaTOF约束载波相位分支；但在±16/±32
                    // 恢复窗中，如果可信Envelope包已经离旧预测超过Raw相位窗口，继续
                    // 强求“旧预测±3”与“Envelope±4”相交会得到空Raw窗口，使扩窗失效。
                    // 此时允许Raw围绕Envelope重定心，但若phase-branch模型已经建立，
                    // 必须在Envelope附近多个载波峰中优先保持同一raw-envelope residual支路。
                    double rawPreferred = trackingPlan.predictedPeriodSamples;
                    constexpr double rawPredictionIntersectionLimit = 4.0;
                    if (envelopeAccepted && trackingPlan.halfWidthSamples > 8
                        && std::abs(envelopeMatch.lag - trackingPlan.predictedPeriodSamples)
                            > rawPredictionIntersectionLimit) {
                        rawPreferred = envelopeMatch.lag;
                        rawRecenteredToEnvelope = true;
                    }
                    rawMatch = refineRawLag(envelopeMatch.lag, rawPreferred);
                    phaseOutputRawMatch = rawMatch;

                    if (rawRecenteredToEnvelope && history
                        && history->rawPhaseBranchTracker.ready()
                        && rawMatch.available && rawMatch.absoluteCorrelation >= 0.60) {
                        const auto phaseMatches = searchRawPhaseCandidates(envelopeMatch.lag);
                        std::vector<RawPhaseBranchTracker::Candidate> phaseCandidates;
                        phaseCandidates.reserve(phaseMatches.size());
                        for (const auto &candidate : phaseMatches)
                            phaseCandidates.push_back({candidate.lag, candidate.absoluteCorrelation});
                        const auto phaseSelection = history->rawPhaseBranchTracker.select(
                            envelopeMatch.lag, phaseCandidates);
                        decision.independentEchoPairRawPhaseBranchEvaluated = true;
                        decision.independentEchoPairRawPhaseBranchMismatch =
                            phaseSelection.branchMismatch;
                        decision.independentEchoPairRawPhaseBranchSelectedResidualSamples =
                            phaseSelection.selectedResidualSamples;
                        decision.independentEchoPairRawPhaseBranchResidualErrorSamples =
                            phaseSelection.residualErrorSamples;
                        decision.independentEchoPairRawPhaseBranchBestAnyResidualSamples =
                            phaseSelection.bestAnyResidualSamples;
                        decision.independentEchoPairRawPhaseBestAnyBranchIndex =
                            phaseSelection.bestAnyBranchIndex;

                        if (phaseSelection.selected) {
                            const auto selected = std::min_element(
                                phaseMatches.begin(), phaseMatches.end(),
                                [&](const PairCorrelationMatch &a, const PairCorrelationMatch &b) {
                                    return std::abs(a.lag - phaseSelection.selectedLagSamples)
                                        < std::abs(b.lag - phaseSelection.selectedLagSamples);
                                });
                            if (selected != phaseMatches.end()) {
                                // 当前active branch内已经找到明确Raw峰。即使它因为物理周期
                                // 残差仍>6而不能直接替换，也可作为2-of-3持续位移确认的身份前提。
                                rawPhaseBranchIdentityResolved = true;
                                // phase identity还必须服从物理周期时序：它只能把Raw峰拉回
                                // 更接近EchoPairTracker预测的同一载波支路，不能仅凭包内
                                // residual身份把输出推到离物理周期更远的相邻载波峰。
                                const double originalTemporalResidual = std::abs(
                                    rawMatch.lag - trackingPlan.predictedPeriodSamples);
                                const double phaseTemporalResidual = std::abs(
                                    selected->lag - trackingPlan.predictedPeriodSamples);
                                constexpr double maxPhaseTemporalResidual = 6.0;
                                if (phaseTemporalResidual <= maxPhaseTemporalResidual
                                    && phaseTemporalResidual + 0.25 < originalTemporalResidual) {
                                    // phase identity只决定本帧Raw输出支路，不改写rawMatch本体。
                                    // EchoPairTracker后续仍使用原Raw测量推进物理周期状态。
                                    phaseOutputRawMatch = *selected;
                                    rawPhaseBranchSelectionApplied = true;
                                    decision.independentEchoPairRawPhaseBranchSelectionApplied = true;
                                }
                            }
                        } else if (phaseSelection.branchMismatch) {
                            // 找不到同支路时不硬拒绝，保留原Raw结果；phase identity只在
                            // 有明确同支路候选时纠正，不把差信号帧变成额外miss。
                            rawPhaseBranchMismatch = true;
                        }
                    }
                }
                const bool rawAccepted = rawMatch.available
                    && rawMatch.absoluteCorrelation >= 0.25;
                const double lockResidual = rawAccepted
                    ? rawMatch.lag - trackingPlan.predictedPeriodSamples : NaN;
                const bool softEnvelopeAccepted = softEnvelopeCandidate
                    && rawMatch.available && rawMatch.absoluteCorrelation >= 0.85
                    && std::isfinite(lockResidual) && std::abs(lockResidual) <= 4.0;

                // 局部能量增强只作为差信号软恢复：增强包必须仍在原预测±8点内，
                // 增强相关很高，并且原/增强能量至少保留约1%的Echo1能量，避免对近零
                // 信号产生高相关幻觉。最终Raw仍需高相关且落在同一±8点身份窗内。
                const bool enhancedEnvelopeCandidate = !envelopeAccepted
                    && enhancedEnergyDiagnostic.match.available
                    && std::isfinite(enhancedEnergyDiagnostic.match.lag)
                    && enhancedEnergyDiagnostic.match.correlation >= 0.90
                    && std::abs(enhancedEnergyDiagnostic.match.lag
                        - trackingPlan.predictedPeriodSamples) <= 8.0
                    && std::max(originalDiagnostic.maxEnergyRatio,
                                enhancedEnergyDiagnostic.match.energyRatio) >= 0.01
                    && originalDiagnostic.match.available
                    && originalDiagnostic.match.correlation >= 0.50;
                if (enhancedEnvelopeCandidate) {
                    enhancedRawMatch = refineRawLag(
                        enhancedEnergyDiagnostic.match.lag,
                        trackingPlan.predictedPeriodSamples);
                    if (!enhancedRawMatch.available
                        && std::abs(enhancedEnergyDiagnostic.match.lag
                            - trackingPlan.predictedPeriodSamples) > 4.0) {
                        enhancedRawMatch = refineRawLag(
                            enhancedEnergyDiagnostic.match.lag,
                            enhancedEnergyDiagnostic.match.lag);
                    }
                }
                const double enhancedResidual = enhancedRawMatch.available
                    ? enhancedRawMatch.lag - trackingPlan.predictedPeriodSamples : NaN;
                decision.independentEchoPairDiagnosticEnhancedRawLagSamples = enhancedRawMatch.lag;
                decision.independentEchoPairDiagnosticEnhancedRawAbsCorrelation = enhancedRawMatch.absoluteCorrelation;
                decision.independentEchoPairDiagnosticEnhancedRawResidualSamples = enhancedResidual;
                const bool enhancedEnergyAccepted = enhancedEnvelopeCandidate
                    && enhancedRawMatch.available
                    && enhancedRawMatch.absoluteCorrelation >= 0.825
                    && std::isfinite(enhancedResidual)
                    && std::abs(enhancedResidual) <= 4.0;

                // Envelope弱/失败时，允许Raw matched作为“独立身份辅证”，但绝不单独放行。
                // 必须同时满足：高Raw相关、明显top1唯一性、紧贴历史周期、足够局部能量，
                // 并至少得到一条弱Envelope或10–30 MHz子带证据支持。
                const double matchedRawResidual = matchedRawDiagnostic.available
                    ? matchedRawDiagnostic.lag - trackingPlan.predictedPeriodSamples : NaN;
                const bool originalWeakSupport = originalDiagnostic.match.available
                    && originalDiagnostic.match.correlation >= 0.45
                    && std::isfinite(originalDiagnostic.match.lag)
                    && std::isfinite(matchedRawDiagnostic.lag)
                    && std::abs(originalDiagnostic.match.lag - matchedRawDiagnostic.lag) <= 10.0;
                const bool enhancedWeakSupport = enhancedEnergyDiagnostic.match.available
                    && enhancedEnergyDiagnostic.match.correlation >= 0.55
                    && std::isfinite(enhancedEnergyDiagnostic.match.lag)
                    && std::isfinite(matchedRawDiagnostic.lag)
                    && std::abs(enhancedEnergyDiagnostic.match.lag - matchedRawDiagnostic.lag) <= 10.0;
                const bool subbandWeakSupport = subband10To30Match.available
                    && subband10To30Match.absoluteCorrelation >= 0.50
                    && subband10To30Match.margin >= 0.10
                    && std::isfinite(subband10To30Match.lag)
                    && std::isfinite(matchedRawDiagnostic.lag)
                    && std::abs(subband10To30Match.lag - matchedRawDiagnostic.lag) <= 3.0;
                const bool matchedRawAuxiliaryAccepted = !envelopeAccepted
                    && !softEnvelopeAccepted
                    && !enhancedEnergyAccepted
                    && matchedRawDiagnostic.available
                    && matchedRawDiagnostic.absoluteCorrelation >= 0.82
                    && matchedRawDiagnostic.margin >= 0.15
                    && matchedRawDiagnostic.energyRatio >= 0.05
                    && std::isfinite(matchedRawResidual)
                    && std::abs(matchedRawResidual) <= 3.0
                    && (originalWeakSupport || enhancedWeakSupport || subbandWeakSupport);

                // 单帧Raw辅证仍不足时才进入弱时序积累。top1/top2并行作为身份假设：
                // 距历史<=3点要求2帧确认；3~5点要求3帧确认；每帧候选步进<=3点。
                WeakRawTemporalTracker::Result weakRawTemporalResult;
                PairCorrelationMatch weakRawTemporalMatch;
                const bool weakRawTemporalEligible = !envelopeAccepted
                    && !softEnvelopeAccepted
                    && !enhancedEnergyAccepted
                    && !matchedRawAuxiliaryAccepted;
                if (weakRawTemporalEligible) {
                    std::vector<WeakRawTemporalTracker::Candidate> weakCandidates;
                    if (matchedRawDiagnostic.available) {
                        weakCandidates.push_back({
                            matchedRawDiagnostic.lag,
                            matchedRawDiagnostic.absoluteCorrelation,
                            matchedRawDiagnostic.energyRatio,
                            1});
                    }
                    if (std::isfinite(matchedRawDiagnostic.secondLag)
                        && std::isfinite(matchedRawDiagnostic.secondAbsoluteCorrelation)
                        && std::isfinite(matchedRawDiagnostic.secondEnergyRatio)) {
                        weakCandidates.push_back({
                            matchedRawDiagnostic.secondLag,
                            matchedRawDiagnostic.secondAbsoluteCorrelation,
                            matchedRawDiagnostic.secondEnergyRatio,
                            2});
                    }
                    weakRawTemporalResult = history->weakRawTemporalTracker.observe(
                        trackingPlan.predictedPeriodSamples, weakCandidates);
                    decision.independentEchoPairWeakRawTemporalState =
                        QString::fromLatin1(WeakRawTemporalTracker::stateName(
                            weakRawTemporalResult.state));
                    decision.independentEchoPairWeakRawTemporalCount =
                        weakRawTemporalResult.consecutiveFrames;
                    decision.independentEchoPairWeakRawTemporalRequiredFrames =
                        weakRawTemporalResult.requiredFrames;
                    decision.independentEchoPairWeakRawTemporalSelectedRank =
                        weakRawTemporalResult.selectedRank;
                    decision.independentEchoPairWeakRawTemporalSelectedLagSamples =
                        weakRawTemporalResult.selectedLagSamples;
                    decision.independentEchoPairWeakRawTemporalSelectedCorrelation =
                        weakRawTemporalResult.selectedCorrelation;
                    decision.independentEchoPairWeakRawTemporalSelectedEnergyRatio =
                        weakRawTemporalResult.selectedEnergyRatio;
                    decision.independentEchoPairWeakRawTemporalPendingCenterSamples =

                        weakRawTemporalResult.pendingCenterSamples;
                    decision.independentEchoPairWeakRawTemporalConfirmed =
                        weakRawTemporalResult.confirmed;
                    if (weakRawTemporalResult.confirmed) {
                        weakRawTemporalMatch.available = true;
                        weakRawTemporalMatch.lag = weakRawTemporalResult.confirmedLagSamples;
                        weakRawTemporalMatch.correlation = weakRawTemporalResult.selectedCorrelation;
                        weakRawTemporalMatch.absoluteCorrelation = weakRawTemporalResult.selectedCorrelation;
                        weakRawTemporalMatch.energyRatio = weakRawTemporalResult.selectedEnergyRatio;
                    }
                } else {
                    history->weakRawTemporalTracker.reset();
                    decision.independentEchoPairWeakRawTemporalState = QStringLiteral("IDLE");
                }

                const double effectiveLockResidual = rawPhaseBranchSelectionApplied
                    ? phaseOutputRawMatch.lag - trackingPlan.predictedPeriodSamples
                    : lockResidual;
                // RECENTER在同一Envelope物理包内仍可能选到相邻载波峰。原Raw相对
                // 物理周期预测偏离>6 sample时，phase identity是硬前提：
                // - 同active branch已解析，但物理残差仍>6：允许2-of-3确认持续位移；
                // - phase未解析/branch mismatch：只抑制输出，不允许靠时序票绕过身份门。
                const bool recenterFarWithoutDirectPhaseCorrection = rawRecenteredToEnvelope
                    && std::isfinite(lockResidual) && std::abs(lockResidual) > 6.0
                    && !rawPhaseBranchSelectionApplied;
                const bool recenterResolvedForTemporalVote = recenterFarWithoutDirectPhaseCorrection
                    && rawPhaseBranchIdentityResolved;
                const bool recenterPhaseUnresolved = recenterFarWithoutDirectPhaseCorrection
                    && !rawPhaseBranchIdentityResolved;
                // RECENTER恰好落在6 sample保护线内时，不应只因0.x sample数值差异就绕过
                // 身份保护。只有同时出现三项“边界歧义”才升级到2-of-3确认：
                //   1) 物理周期残差已经>5.5 sample；
                //   2) Envelope相关偏弱(<0.90)，说明扩窗物理包本身不够稳；
                //   3) 虽找到active phase branch，但包内phase residual已接近2.5点支路门。
                // 这不是把全局6点门改成5.5，而是专门拦截“两个身份门同时擦边”的帧。
                const bool recenterBoundaryAmbiguous = rawRecenteredToEnvelope
                    && std::isfinite(lockResidual)
