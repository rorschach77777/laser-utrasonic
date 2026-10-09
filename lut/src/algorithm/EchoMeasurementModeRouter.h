#pragma once

#include <cmath>

class EchoMeasurementModeRouter
{
public:
    enum class Mode { FirstEchoOnly, SecondEchoOnly };

    struct Config {
        double secondEchoUpperReferenceMm = 15.0;
    };

    EchoMeasurementModeRouter() = default;
    explicit EchoMeasurementModeRouter(const Config &cfg) : m_cfg(cfg) {}

    Mode select(double referenceThicknessMm) const
    {
        return std::isfinite(referenceThicknessMm)
            && referenceThicknessMm > 0.0
            && referenceThicknessMm < m_cfg.secondEchoUpperReferenceMm
            ? Mode::SecondEchoOnly : Mode::FirstEchoOnly;
    }

    static const char *modeName(Mode mode)
    {
        return mode == Mode::SecondEchoOnly ? "SECOND_ECHO_ONLY" : "FIRST_ECHO_ONLY";
    }

private:
    Config m_cfg;
};
