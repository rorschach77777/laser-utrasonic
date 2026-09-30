#pragma once

#include <QString>

#include <cmath>
#include <limits>

enum class DualPolarityIdentity { Unknown, Positive, Negative };

struct IdentityBranchObservation
{
    DualPolarityIdentity polarity = DualPolarityIdentity::Unknown;
    bool fitSolved = false;
    double fitR2 = std::numeric_limits<double>::quiet_NaN();
    double fitCenter = std::numeric_limits<double>::quiet_NaN();
    double fitLeftFoot = std::numeric_limits<double>::quiet_NaN();
    double fitRightFoot = std::numeric_limits<double>::quiet_NaN();
    double fitFootMidpoint = std::numeric_limits<double>::quiet_NaN();
    double fitWidthSamples = std::numeric_limits<double>::quiet_NaN();
    double directCenter = std::numeric_limits<double>::quiet_NaN();
    double directAmplitude = std::numeric_limits<double>::quiet_NaN();

    bool fitAvailable() const
    {
        return fitSolved && std::isfinite(fitCenter);
    }

    bool directAvailable() const
    {
        return std::isfinite(directCenter);
    }

    bool anyAvailable() const
    {
        return fitAvailable() || directAvailable();
    }
};

struct IdentityObservation
{
    IdentityBranchObservation positive;
    IdentityBranchObservation negative;
    DualPolarityIdentity selectedIdentity = DualPolarityIdentity::Unknown;
    bool identityEstablished = false;
    QString stage;
    double rawDirectCenter = std::numeric_limits<double>::quiet_NaN();

    const IdentityBranchObservation &branch(DualPolarityIdentity polarity) const
    {
        if (polarity == DualPolarityIdentity::Negative)
            return negative;
        return positive;
    }

    const IdentityBranchObservation &selectedBranch() const
    {
        return branch(selectedIdentity);
    }

    bool selectedFitAvailable() const
    {
        return selectedIdentity != DualPolarityIdentity::Unknown
            && selectedBranch().fitAvailable();
    }

    bool selectedDirectAvailable() const
    {
        return selectedIdentity != DualPolarityIdentity::Unknown
            && selectedBranch().directAvailable();
    }

    bool selectedAnyAvailable() const
    {
        return selectedFitAvailable() || selectedDirectAvailable();
    }
};
