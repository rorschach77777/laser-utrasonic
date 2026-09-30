#pragma once

#include "IdentityObservation.h"

#include <QString>

#include <cmath>
#include <limits>

struct TofEvidenceFrame
{
    QString file;
    QString trackingState;
    QString measurementStatus;
    QString finalMeasurementStatus;
    bool predictionApplied = false;

    double finalTof = std::numeric_limits<double>::quiet_NaN();
    double finalThicknessMm = std::numeric_limits<double>::quiet_NaN();

    double currentOutputTof = std::numeric_limits<double>::quiet_NaN();
    double currentOutputThicknessMm = std::numeric_limits<double>::quiet_NaN();

    double envelopeNccTof = std::numeric_limits<double>::quiet_NaN();
    double envelopeNccCorrelation = std::numeric_limits<double>::quiet_NaN();
    double hilbertEnvelopePeakTof = std::numeric_limits<double>::quiet_NaN();
    double energyMedianT50Tof = std::numeric_limits<double>::quiet_NaN();

    double rawDirectTof = std::numeric_limits<double>::quiet_NaN();
    double cfd30Tof = std::numeric_limits<double>::quiet_NaN();
    double leadingEdgeFitTof = std::numeric_limits<double>::quiet_NaN();

    double snrDb = std::numeric_limits<double>::quiet_NaN();
    double maxLocalSnrDb = std::numeric_limits<double>::quiet_NaN();
    double trackingCorrelation = std::numeric_limits<double>::quiet_NaN();

    IdentityObservation identity;

    bool finalAvailable() const { return std::isfinite(finalTof); }
    bool envelopeAvailable() const
    {
        return std::isfinite(envelopeNccTof)
            || std::isfinite(hilbertEnvelopePeakTof)
            || std::isfinite(energyMedianT50Tof);
    }
    bool localWaveAvailable() const
    {
        return std::isfinite(rawDirectTof)
            || std::isfinite(cfd30Tof)
            || std::isfinite(leadingEdgeFitTof);
    }
};
