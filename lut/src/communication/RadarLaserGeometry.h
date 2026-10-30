#pragma once

// Installation geometry shared by the UI, pipe-head tracker, pipe-presence
// display, CSV and tail-position estimation. Only these are geometric
// distances; radar TotalLength scaling and ultrasonic TOF are independent.
namespace RadarLaserGeometry {
// Field-measured installation geometry (mm):
// LB1 --150--> radar --210--> LB2 --300--> laser.
inline constexpr double DefaultRadarToLaserMm = 510.0;
inline constexpr double RadarToLb2Mm = 210.0;
inline constexpr double Lb1ToRadarMm = 150.0;
inline constexpr double MaximumRadarToLaserMm = 2000.0;
inline constexpr double MinimumRadarToLaserMm = RadarToLb2Mm + 1.0; // integer UI
}
