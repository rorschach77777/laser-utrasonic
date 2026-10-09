// AUTO-MIRRORED from LSTAlgorithmTest Independent EchoPair block.
    // Echo-pair Differential TOF：Echo2身份搜索不再依赖thicknessStdMm。
    // GLOBAL_SEARCH保留独立Echo2全局候选族；LOCK后改为当前Echo1模板的小窗NCC。
    // Envelope NCC只负责Echo2身份，Raw NCC负责最终亚采样DeltaTOF；Echo3仅作验证。
    {
        QElapsedTimer echoPairTimer;
        echoPairTimer.start();
        const int firstLeft = componentLeft;
        const int firstRight = componentRight;
        const int firstCount = firstRight - firstLeft + 1;
        if (firstCount >= 8 && p.longitudinalVelocityMps > 0.0 && p.sampleRateHz > 0.0) {
            double firstEnergy = 0.0;
            for (int i = firstLeft; i <= firstRight; ++i) {
                const double v = corrected[static_cast<size_t>(i)];
                firstEnergy += v * v;
            }

            const int independentGap = std::max(
                p.candidateSeparationSamples,
                firstCount + std::max(1, p.packetMergeGap));
            const int lagLeft = std::max(1, independentGap);
            const int lagRight = n - firstRight - 1;
            const double minEcho2EnergyRatio = std::max(0.001, p.echoMinRelativeEnergy);
            constexpr double minEcho3EnergyRatio = 0.002;
            constexpr double minChainCorrelation12 = 0.88;
            constexpr double minChainCorrelation23 = 0.80;
            constexpr double minChainCorrelation13 = 0.72;

            struct PairCorrelationMatch {
                bool available = false;
                double lag = NaN;
                double correlation = NaN;
                double absoluteCorrelation = NaN;
                double margin = NaN;
                double energyRatio = NaN;
                double secondLag = NaN;
                double secondAbsoluteCorrelation = NaN;
                double secondEnergyRatio = NaN;
            };

            // 当前Echo1直接作为本帧模板。Envelope NCC只回答“哪个后续包是Echo2”。
            auto searchEnvelopeLag = [&](int searchLagLeft, int searchLagRight) {
                PairCorrelationMatch match;
                searchLagLeft = std::max(lagLeft, searchLagLeft);
                searchLagRight = std::min(lagRight, searchLagRight);
                if (searchLagLeft > searchLagRight)
                    return match;

                struct Sample { int lag = 0; double rho = -1.0; double energyRatio = 0.0; };
                std::vector<Sample> samples;
                samples.reserve(static_cast<size_t>(searchLagRight - searchLagLeft + 1));
                for (int lag = searchLagLeft; lag <= searchLagRight; ++lag) {
                    const int secondLeft = firstLeft + lag;
                    double energy2 = 0.0;
                    for (int k = 0; k < firstCount; ++k) {
                        const double v = corrected[static_cast<size_t>(secondLeft + k)];
                        energy2 += v * v;
                    }
                    const double ratio = firstEnergy > 1e-24 ? energy2 / firstEnergy : 0.0;
                    double rho = -1.0;
                    if (ratio >= minEcho2EnergyRatio) {
                        const double value = packetCorrelation(
                            corrected.data() + firstLeft,
                            corrected.data() + secondLeft,
                            static_cast<size_t>(firstCount));
                        if (std::isfinite(value))
                            rho = value;
                    }
                    samples.push_back({lag, rho, ratio});
                }
                if (samples.empty())
                    return match;

                const auto best = std::max_element(samples.begin(), samples.end(),
                    [](const Sample &a, const Sample &b) { return a.rho < b.rho; });
                if (best == samples.end() || !std::isfinite(best->rho) || best->rho <= 0.0)
                    return match;
                const int bestIndex = static_cast<int>(std::distance(samples.begin(), best));
                double delta = 0.0;
                if (bestIndex > 0 && bestIndex + 1 < static_cast<int>(samples.size())) {
                    const double ym = samples[static_cast<size_t>(bestIndex - 1)].rho;
                    const double y0 = samples[static_cast<size_t>(bestIndex)].rho;
                    const double yp = samples[static_cast<size_t>(bestIndex + 1)].rho;
                    const double curvature = ym - 2.0 * y0 + yp;
                    if (std::isfinite(curvature) && curvature < -1e-12)
                        delta = std::clamp(0.5 * (ym - yp) / curvature, -0.5, 0.5);
                }
                double competitor = -1.0;
                for (int i = 1; i + 1 < static_cast<int>(samples.size()); ++i) {
                    if (std::abs(samples[static_cast<size_t>(i)].lag - best->lag) < 4)
                        continue;
                    if (samples[static_cast<size_t>(i)].rho >= samples[static_cast<size_t>(i - 1)].rho
                        && samples[static_cast<size_t>(i)].rho >= samples[static_cast<size_t>(i + 1)].rho)
                        competitor = std::max(competitor, samples[static_cast<size_t>(i)].rho);
                }
                match.available = true;
                match.lag = best->lag + delta;
                match.correlation = best->rho;
                match.absoluteCorrelation = std::abs(best->rho);
                match.margin = competitor >= -0.5 ? best->rho - competitor : best->rho;
                match.energyRatio = best->energyRatio;
                return match;
            };

            struct DiagnosticPairMatch {
                PairCorrelationMatch match;
                double maxEnergyRatio = NaN;
            };

            // 旁路诊断搜索：不施加Echo2最低能量门，只记录预测窗内最佳形态相关与
            // 最大能量比。它绝不参与正式LOCK判定。
            auto searchDiagnosticLag = [&](const std::vector<double> &signal,
                                           int searchLagLeft,
                                           int searchLagRight) {
                DiagnosticPairMatch diagnostic;
                searchLagLeft = std::max(lagLeft, searchLagLeft);
                searchLagRight = std::min(lagRight, searchLagRight);
                if (searchLagLeft > searchLagRight || signal.size() != corrected.size())
                    return diagnostic;

                double templateEnergy = 0.0;
                for (int k = 0; k < firstCount; ++k) {
                    const double v = signal[static_cast<size_t>(firstLeft + k)];
                    templateEnergy += v * v;
                }
                if (!(templateEnergy > 1e-24))
                    return diagnostic;

                struct Sample { int lag = 0; double rho = NaN; double ratio = 0.0; };
                std::vector<Sample> samples;
                samples.reserve(static_cast<size_t>(searchLagRight - searchLagLeft + 1));
                double maxRatio = 0.0;
                for (int lag = searchLagLeft; lag <= searchLagRight; ++lag) {
                    const int secondLeft = firstLeft + lag;
                    double energy2 = 0.0;
                    for (int k = 0; k < firstCount; ++k) {
                        const double v = signal[static_cast<size_t>(secondLeft + k)];
                        energy2 += v * v;
                    }
                    const double ratio = energy2 / templateEnergy;
                    maxRatio = std::max(maxRatio, ratio);
                    const double rho = packetCorrelation(
                        signal.data() + firstLeft,
                        signal.data() + secondLeft,
                        static_cast<size_t>(firstCount));
                    samples.push_back({lag, rho, ratio});
                }
                diagnostic.maxEnergyRatio = maxRatio;
                const auto best = std::max_element(samples.begin(), samples.end(),
                    [](const Sample &a, const Sample &b) {
                        const double ar = std::isfinite(a.rho) ? a.rho : -2.0;
                        const double br = std::isfinite(b.rho) ? b.rho : -2.0;
                        return ar < br;
                    });
                if (best == samples.end() || !std::isfinite(best->rho))
                    return diagnostic;

                const int bestIndex = static_cast<int>(std::distance(samples.begin(), best));
                double delta = 0.0;
                if (bestIndex > 0 && bestIndex + 1 < static_cast<int>(samples.size())) {
                    const double ym = samples[static_cast<size_t>(bestIndex - 1)].rho;
                    const double y0 = samples[static_cast<size_t>(bestIndex)].rho;
                    const double yp = samples[static_cast<size_t>(bestIndex + 1)].rho;
                    const double curvature = ym - 2.0 * y0 + yp;
                    if (std::isfinite(curvature) && curvature < -1e-12)
                        delta = std::clamp(0.5 * (ym - yp) / curvature, -0.5, 0.5);
                }
                diagnostic.match.available = true;
                diagnostic.match.lag = best->lag + delta;
                diagnostic.match.correlation = best->rho;
                diagnostic.match.absoluteCorrelation = std::abs(best->rho);
                diagnostic.match.energyRatio = best->ratio;
                return diagnostic;
            };

            auto buildLocalEnergyEnvelope = [&]() {
                constexpr int energyHalf = 4; // 9点局部RMS，约56 ns@160 MHz。
                std::vector<double> rms(static_cast<size_t>(n), 0.0);
                for (int i = energyHalf; i < n - energyHalf; ++i) {
                    double sum = 0.0;
                    for (int k = -energyHalf; k <= energyHalf; ++k) {
                        const double v = filtered[static_cast<size_t>(i + k)];
                        sum += v * v;
                    }
                    rms[static_cast<size_t>(i)] = std::sqrt(sum / (2 * energyHalf + 1));
                }

                std::vector<double> rmsBackground;
                rmsBackground.reserve(24);
                for (int i = std::max(energyHalf, firstLeft - 12); i < firstLeft; ++i)
                    rmsBackground.push_back(rms[static_cast<size_t>(i)]);
                for (int i = firstRight + 1; i <= std::min(n - 1 - energyHalf, firstRight + 12); ++i)
                    rmsBackground.push_back(rms[static_cast<size_t>(i)]);
                const double rmsBaseline = rmsBackground.empty() ? 0.0 : medianValue(rmsBackground);
                for (double &v : rms)
                    v = std::max(0.0, v - rmsBaseline);

                std::vector<double> localEnergy;
                secondEchoOnlySmoothThreePoint(rms, localEnergy);
                std::vector<double> enhancedEnergy;
                secondEchoOnlySmoothThreePoint(localEnergy, enhancedEnergy);
                return std::pair<std::vector<double>, std::vector<double>>{
                    std::move(localEnergy), std::move(enhancedEnergy)};
            };

            // 局部匹配滤波诊断：直接在预测窗内扫描Echo1↔后续带通信号的|NCC|，
            // 不依赖Envelope候选。它不参与正式输出，只用于判断弱帧是否仍保留载波级Echo2。
            auto searchMatchedSignalLag = [&](const std::vector<double> &signal,
                                              int searchLagLeft, int searchLagRight) {
                PairCorrelationMatch match;
                searchLagLeft = std::max(lagLeft, searchLagLeft);
                searchLagRight = std::min(lagRight, searchLagRight);
                if (searchLagLeft > searchLagRight || signal.size() != filtered.size())
                    return match;
                double templateEnergy = 0.0;
                for (int k = 0; k < firstCount; ++k) {
                    const double v = signal[static_cast<size_t>(firstLeft + k)];
                    templateEnergy += v * v;
                }
                if (!(templateEnergy > 1e-24))
                    return match;
                struct Sample { int lag = 0; double rho = NaN; double score = -1.0; double ratio = 0.0; };
                std::vector<Sample> samples;
                samples.reserve(static_cast<size_t>(searchLagRight - searchLagLeft + 1));
                for (int lag = searchLagLeft; lag <= searchLagRight; ++lag) {
                    double energy2 = 0.0;
                    for (int k = 0; k < firstCount; ++k) {
                        const double v = signal[static_cast<size_t>(firstLeft + lag + k)];
                        energy2 += v * v;
                    }
                    const double rho = packetCorrelation(
                        signal.data() + firstLeft,
                        signal.data() + firstLeft + lag,
                        static_cast<size_t>(firstCount));
                    if (std::isfinite(rho))
                        samples.push_back({lag, rho, std::abs(rho), energy2 / templateEnergy});
                }
                if (samples.empty())
                    return match;
                const auto best = std::max_element(samples.begin(), samples.end(),
                    [](const Sample &a, const Sample &b) { return a.score < b.score; });
                const int bestIndex = static_cast<int>(std::distance(samples.begin(), best));
                double delta = 0.0;
                if (bestIndex > 0 && bestIndex + 1 < static_cast<int>(samples.size())) {
                    const double ym = samples[static_cast<size_t>(bestIndex - 1)].score;
                    const double y0 = samples[static_cast<size_t>(bestIndex)].score;
                    const double yp = samples[static_cast<size_t>(bestIndex + 1)].score;
                    const double curvature = ym - 2.0 * y0 + yp;
                    if (std::isfinite(curvature) && curvature < -1e-12)
                        delta = std::clamp(0.5 * (ym - yp) / curvature, -0.5, 0.5);
                }
                double competitor = -1.0;
                int competitorLag = 0;
                double competitorEnergyRatio = NaN;
                for (const Sample &sample : samples) {
                    if (std::abs(sample.lag - best->lag) < 4)
                        continue;
                    if (sample.score > competitor) {
                        competitor = sample.score;
                        competitorLag = sample.lag;
                        competitorEnergyRatio = sample.ratio;
                    }
                }
                match.available = true;
                match.lag = best->lag + delta;
                match.correlation = best->rho;
                match.absoluteCorrelation = best->score;
                match.margin = competitor >= 0.0 ? best->score - competitor : best->score;
                match.energyRatio = best->ratio;
                if (competitor >= 0.0) {
                    match.secondLag = competitorLag;
                    match.secondAbsoluteCorrelation = competitor;
                    match.secondEnergyRatio = competitorEnergyRatio;
                }
                return match;
            };
            auto searchMatchedRawLag = [&](int searchLagLeft, int searchLagRight) {
                return searchMatchedSignalLag(filtered, searchLagLeft, searchLagRight);
            };

            // 二级局部子带仅用于诊断：主链仍使用3–30 MHz。对已经带通的波形再施加
            // 一个零相位对称FIR，比较10–30与18–30 MHz是否能提高弱Echo2的匹配唯一性。
            auto buildDiagnosticSubband = [&](double lowMHz, double highMHz) {
                constexpr int subOrder = 40;
                constexpr int half = subOrder / 2;
                std::vector<double> coeffs;
                designBandpassFilter(lowMHz * 1e6, highMHz * 1e6,
                                     p.sampleRateHz, subOrder, coeffs);
                std::vector<double> signal(static_cast<size_t>(n), 0.0);
                for (int i = half; i < n - half; ++i) {
                    double sum = 0.0;
                    for (int j = 0; j <= subOrder; ++j)
                        sum += coeffs[static_cast<size_t>(j)]
                            * filtered[static_cast<size_t>(i + j - half)];
                    signal[static_cast<size_t>(i)] = sum;
                }
                return signal;
            };

            // Raw NCC只在Envelope已确认的Echo2物理包附近做小范围搜索。
            // 目标函数使用|rho|允许多次反射后的极性翻转，三点抛物线给亚采样DeltaTOF。
            auto refineRawLag = [&](double envelopeLag, double preferredLag) {
                PairCorrelationMatch match;
                if (!std::isfinite(envelopeLag))
                    return match;
                // GLOBAL/WARMUP没有Raw历史时以Envelope峰为中心；LOCK后以历史Raw DeltaTOF
                // 为相位中心，并与当前Envelope包络位置求交。这样可抑制约一个载波周期的
                // 301<->308 sample相位跳变，同时仍要求Raw解落在当前Echo2物理包内。
                const double rawCenterValue = std::isfinite(preferredLag) ? preferredLag : envelopeLag;
                const int centerLag = static_cast<int>(std::llround(rawCenterValue));
                constexpr int rawHalfWidth = 3;
                constexpr int envelopeContainmentHalfWidth = 4;
                const int rawLeft = std::max({
                    lagLeft,
                    centerLag - rawHalfWidth,
                    static_cast<int>(std::floor(envelopeLag - envelopeContainmentHalfWidth))});
                const int rawRight = std::min({
                    lagRight,
                    centerLag + rawHalfWidth,
                    static_cast<int>(std::ceil(envelopeLag + envelopeContainmentHalfWidth))});
                if (rawLeft > rawRight)
                    return match;
                struct Sample { int lag = 0; double rho = NaN; double score = -1.0; };
                std::vector<Sample> samples;
                for (int lag = rawLeft; lag <= rawRight; ++lag) {
                    const double rho = packetCorrelation(
                        filtered.data() + firstLeft,
                        filtered.data() + firstLeft + lag,
                        static_cast<size_t>(firstCount));
                    if (std::isfinite(rho))
                        samples.push_back({lag, rho, std::abs(rho)});
                }
                if (samples.empty())
                    return match;
                const auto best = std::max_element(samples.begin(), samples.end(),
                    [](const Sample &a, const Sample &b) { return a.score < b.score; });
                const int bestIndex = static_cast<int>(std::distance(samples.begin(), best));
                double delta = 0.0;
                if (bestIndex > 0 && bestIndex + 1 < static_cast<int>(samples.size())) {
                    const double ym = samples[static_cast<size_t>(bestIndex - 1)].score;
                    const double y0 = samples[static_cast<size_t>(bestIndex)].score;
                    const double yp = samples[static_cast<size_t>(bestIndex + 1)].score;
                    const double curvature = ym - 2.0 * y0 + yp;
                    if (std::isfinite(curvature) && curvature < -1e-12)
                        delta = std::clamp(0.5 * (ym - yp) / curvature, -0.5, 0.5);
                }
                match.available = true;
                match.lag = best->lag + delta;
                match.correlation = best->rho;
                match.absoluteCorrelation = best->score;
                return match;
            };

            // 显式载波phase branch候选：只在同一Envelope物理包附近枚举Raw-NCC
            // 局部峰，供RawPhaseBranchTracker按raw-envelope residual选择当前支路。
            // 该搜索不决定Echo2物理包身份，只解决包内多个载波相关峰的分支歧义。
            auto searchRawPhaseCandidates = [&](double envelopeLag) {
                std::vector<PairCorrelationMatch> matches;
                if (!std::isfinite(envelopeLag))
                    return matches;
                constexpr int phaseHalfWidth = 12;
                const int rawLeft = std::max(
                    lagLeft, static_cast<int>(std::floor(envelopeLag - phaseHalfWidth)));
                const int rawRight = std::min(
                    lagRight, static_cast<int>(std::ceil(envelopeLag + phaseHalfWidth)));
                if (rawLeft > rawRight)
                    return matches;

                struct Sample { int lag = 0; double rho = NaN; double score = -1.0; };
                std::vector<Sample> samples;
                samples.reserve(static_cast<size_t>(rawRight - rawLeft + 1));
                for (int lag = rawLeft; lag <= rawRight; ++lag) {
                    const double rho = packetCorrelation(
                        filtered.data() + firstLeft,
                        filtered.data() + firstLeft + lag,
                        static_cast<size_t>(firstCount));
                    if (std::isfinite(rho))
                        samples.push_back({lag, rho, std::abs(rho)});
                }
                for (int i = 0; i < static_cast<int>(samples.size()); ++i) {
                    const double score = samples[static_cast<size_t>(i)].score;
                    if (score < 0.55)
                        continue;
                    const bool leftOk = i == 0
                        || score >= samples[static_cast<size_t>(i - 1)].score;
                    const bool rightOk = i + 1 == static_cast<int>(samples.size())
                        || score >= samples[static_cast<size_t>(i + 1)].score;
                    if (!leftOk || !rightOk)
                        continue;
                    double delta = 0.0;
                    if (i > 0 && i + 1 < static_cast<int>(samples.size())) {
                        const double ym = samples[static_cast<size_t>(i - 1)].score;
                        const double y0 = score;
                        const double yp = samples[static_cast<size_t>(i + 1)].score;
                        const double curvature = ym - 2.0 * y0 + yp;
                        if (std::isfinite(curvature) && curvature < -1e-12)
                            delta = std::clamp(0.5 * (ym - yp) / curvature, -0.5, 0.5);
                    }
                    PairCorrelationMatch match;
                    match.available = true;
                    match.lag = samples[static_cast<size_t>(i)].lag + delta;
                    match.correlation = samples[static_cast<size_t>(i)].rho;
                    match.absoluteCorrelation = score;
                    matches.push_back(match);
                }
                std::sort(matches.begin(), matches.end(),
                    [](const PairCorrelationMatch &a, const PairCorrelationMatch &b) {
                        return a.absoluteCorrelation > b.absoluteCorrelation;
                    });
                if (matches.size() > 6)
                    matches.resize(6);
                return matches;
            };

            const double firstPairTof = std::isfinite(decision.energyMedianT50Index)
                ? decision.energyMedianT50Index : decision.hilbertEnvelopePeakTofIndex;

            auto publishTrackedPair = [&](const PairCorrelationMatch &envelopeMatch,
                                          const PairCorrelationMatch &rawMatch,
                                          const QString &status,
                                          const QString &policy) {
                if (!envelopeMatch.available || !rawMatch.available
                    || !std::isfinite(rawMatch.lag) || rawMatch.lag <= 0.0)
                    return false;
                decision.independentEchoPairFirstTofIndex = firstPairTof;
                decision.independentEchoPairSecondTofIndex = std::isfinite(firstPairTof)
                    ? firstPairTof + rawMatch.lag : NaN;
                decision.independentEchoPairDifferentialTofSamples = rawMatch.lag;
                decision.independentEchoPairCorrelation12 = envelopeMatch.correlation;
                decision.independentEchoPairEnvelopeLagSamples = envelopeMatch.lag;
                decision.independentEchoPairEnvelopeCorrelation = envelopeMatch.correlation;
                decision.independentEchoPairEnvelopeMargin = envelopeMatch.margin;
                decision.independentEchoPairRawCorrelation = rawMatch.correlation;
                decision.independentEchoPairRawAbsCorrelation = rawMatch.absoluteCorrelation;
                decision.independentEchoPairEnergyRatio2 = envelopeMatch.energyRatio;
                decision.independentEchoPairThicknessMm = p.longitudinalVelocityMps
                    * rawMatch.lag / p.sampleRateHz / 2.0 * 1000.0
                    - p.echoPairBiasMm;
                decision.independentEchoPairStatus = status;
                decision.independentEchoPairSelectionPolicy = policy;

                // Echo3只作验证，不控制Echo1-Echo2本帧输出。
                const int thirdCenter = static_cast<int>(std::llround(2.0 * rawMatch.lag));
                const PairCorrelationMatch echo3Match = searchEnvelopeLag(
                    thirdCenter - 4, thirdCenter + 4);
                if (echo3Match.available) {
                    decision.independentEchoPairEcho3ValidationAvailable = true;
                    decision.independentEchoPairThirdTofIndex = std::isfinite(firstPairTof)
                        ? firstPairTof + echo3Match.lag : NaN;
                    decision.independentEchoPairCorrelation13 = echo3Match.correlation;
                    const int echo2Left = firstLeft + static_cast<int>(std::llround(rawMatch.lag));
                    const int echo3Left = firstLeft + static_cast<int>(std::llround(echo3Match.lag));
                    if (echo2Left >= 0 && echo3Left >= 0
                        && echo2Left + firstCount <= n && echo3Left + firstCount <= n) {
                        decision.independentEchoPairCorrelation23 = packetCorrelation(
                            corrected.data() + echo2Left,
                            corrected.data() + echo3Left,
                            static_cast<size_t>(firstCount));
                    }
                    const double p23 = echo3Match.lag - rawMatch.lag;
                    decision.independentEchoPairEcho3PeriodDifferenceSamples =
                        std::abs(rawMatch.lag - p23);
                    decision.independentEchoPairChainCertified =
                        echo3Match.correlation >= minChainCorrelation13
