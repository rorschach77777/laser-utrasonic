#pragma once

// 独立T2-only ROI Hilbert，严格按照LSTAlgorithmTest的
// 3倍镜像扩展 + 2^k FFT / analytic spectrum + t50前包络约定。
// 缓存属于ThicknessProcessor实例；不使用线程共享可写缓存。
#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

class SecondEchoRoiHilbert
{
public:
    int lastStart = -1;
    int lastEnd = -1;
    int lastFftSize = 0;

    const std::vector<double> &calculate(const std::vector<double> &signal,
                                          int windowLeft, int windowRight,
                                          int margin)
    {
        const int n = static_cast<int>(signal.size());
        if (n < 3) {
            m_output.clear();
            lastStart = lastEnd = -1;
            lastFftSize = 0;
            return m_output;
        }
        // right inclusive；候选区内外的support/mirror margin须与测试程序一致。
        const int start = std::max(0, windowLeft - std::max(0, margin));
        const int end = std::min(n, windowRight + 1 + std::max(0, margin));
        lastStart = start;
        lastEnd = end;
        const int roiLength = end - start;
        if (roiLength < 3) {
            m_output.assign(static_cast<size_t>(n), 0.0);
            lastFftSize = 0;
            return m_output;
        }
        int fftSize = 1;
        while (fftSize < 3 * roiLength)
            fftSize <<= 1;
        lastFftSize = fftSize;
        prepare(fftSize);
        std::fill(m_spectrum.begin(), m_spectrum.end(), std::complex<double>(0.0, 0.0));
        // 与测试程序先复制ROI再调用hilbertEnvelopeCached结果逐点一致。
        for (int i = 0; i < roiLength; ++i) {
            m_spectrum[static_cast<size_t>(i)] =
                signal[static_cast<size_t>(start + roiLength - 1 - i)];
            m_spectrum[static_cast<size_t>(roiLength + i)] =
                signal[static_cast<size_t>(start + i)];
            m_spectrum[static_cast<size_t>(2 * roiLength + i)] =
                signal[static_cast<size_t>(start + roiLength - 1 - i)];
        }
        fft(m_spectrum);
        const int half = fftSize / 2;
        for (int k = 1; k < half; ++k)
            m_spectrum[static_cast<size_t>(k)] *= 2.0;
        for (int k = half + 1; k < fftSize; ++k)
            m_spectrum[static_cast<size_t>(k)] = std::complex<double>(0.0, 0.0);
        for (auto &v : m_spectrum)
            v = std::conj(v);
        fft(m_spectrum);
        for (auto &v : m_spectrum)
            v = std::conj(v) / static_cast<double>(fftSize);

        m_output.assign(static_cast<size_t>(n), 0.0);
        for (int i = 0; i < roiLength; ++i)
            m_output[static_cast<size_t>(start + i)] =
                std::abs(m_spectrum[static_cast<size_t>(roiLength + i)]);
        return m_output;
    }

private:
    void prepare(int n)
    {
        if (m_planSize == n)
            return;
        m_planSize = n;
        m_spectrum.resize(static_cast<size_t>(n));
        m_reverse.assign(static_cast<size_t>(n), 0);
        m_twiddles.clear();
        for (int i = 1, j = 0; i < n; ++i) {
            int bit = n >> 1;
            for (; j & bit; bit >>= 1)
                j ^= bit;
            j ^= bit;
            m_reverse[static_cast<size_t>(i)] = j;
        }
        constexpr double PI = 3.141592653589793238462643383279502884;
        for (int len = 2; len <= n; len <<= 1) {
            const double angle = -2.0 * PI / len;
            const std::complex<double> wLen(std::cos(angle), std::sin(angle));
            auto &table = m_twiddles.emplace_back();
            table.reserve(static_cast<size_t>(len / 2));
            std::complex<double> w(1.0, 0.0);
            for (int j = 0; j < len / 2; ++j) {
                table.push_back(w);
                w *= wLen;
            }
        }
    }

    void fft(std::vector<std::complex<double>> &spectrum) const
    {
        const int n = static_cast<int>(spectrum.size());
        for (int i = 1; i < n; ++i) {
            const int j = m_reverse[static_cast<size_t>(i)];
            if (i < j)
                std::swap(spectrum[static_cast<size_t>(i)], spectrum[static_cast<size_t>(j)]);
        }
        int stage = 0;
        for (int len = 2; len <= n; len <<= 1, ++stage) {
            const auto &twiddles = m_twiddles[static_cast<size_t>(stage)];
            for (int i = 0; i < n; i += len) {
                for (int j = 0; j < len / 2; ++j) {
                    const auto u = spectrum[static_cast<size_t>(i + j)];
                    const auto v = spectrum[static_cast<size_t>(i + j + len / 2)]
                        * twiddles[static_cast<size_t>(j)];
                    spectrum[static_cast<size_t>(i + j)] = u + v;
                    spectrum[static_cast<size_t>(i + j + len / 2)] = u - v;
                }
            }
        }
    }
    int m_planSize = 0;
    std::vector<std::complex<double>> m_spectrum;
    std::vector<int> m_reverse;
    std::vector<std::vector<std::complex<double>>> m_twiddles;
    std::vector<double> m_output;
};
