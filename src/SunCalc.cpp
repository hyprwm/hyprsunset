#include "SunCalc.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <limits>
#include <chrono>

namespace NSunCalc {

    // ------------------ Constructor ------------------

    CSunCalculator::CSunCalculator(const SLocation& l) : m_location(l) {}

    // ------------------ Public API ------------------

    SSunTimes CSunCalculator::compute(int year, int month, int day) {
        // UTC sunrise/sunset (minutes from midnight)
        double    sunriseUTC = calcSunriseUTC(day, month, year, m_location.latitude, m_location.longitude);

        double    sunsetUTC = calcSunsetUTC(day, month, year, m_location.latitude, m_location.longitude);

        SSunTimes times{};
        if (sunriseUTC == NO_EVENT_SENTINEL) {
            times.sunrise        = NO_EVENT_SENTINEL;
            times.sunriseMissing = true;
        } else {
            double localMinutes = sunriseUTC + m_location.timezone * MINUTES_PER_HOUR;
            localMinutes        = std::fmod(localMinutes, MINUTES_PER_DAY);
            if (localMinutes < 0)
                localMinutes += MINUTES_PER_DAY;
            times.sunrise = localMinutes / MINUTES_PER_HOUR;
        }

        if (sunsetUTC == NO_EVENT_SENTINEL) {
            times.sunset        = NO_EVENT_SENTINEL;
            times.sunsetMissing = true;
        } else {
            double localMinutes = sunsetUTC + m_location.timezone * MINUTES_PER_HOUR;
            localMinutes        = std::fmod(localMinutes, MINUTES_PER_DAY);
            if (localMinutes < 0)
                localMinutes += MINUTES_PER_DAY;
            times.sunset = localMinutes / MINUTES_PER_HOUR;
        }

        return times;
    }

    SSunTimes CSunCalculator::compute() {
        using namespace std::chrono;
        const auto  now           = system_clock::now();
        const auto  offsetSeconds = seconds(static_cast<long long>(std::llround(m_location.timezone * SECONDS_PER_HOUR)));
        const auto  shifted       = now + offsetSeconds;
        std::time_t tt            = system_clock::to_time_t(shifted);

        std::tm     utc{};
        gmtime_r(&tt, &utc);
        return compute(utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday);
    }

    SSunTimes CSunCalculator::computeWithFallback(int year, int month, int day) {
        SSunTimes times = compute(year, month, day);
        applyFallback(times);
        return times;
    }

    SSunTimes CSunCalculator::computeWithFallback() {
        SSunTimes times = compute();
        applyFallback(times);
        return times;
    }

    double CSunCalculator::currentLocalHours() const {
        using namespace std::chrono;
        const auto  now           = system_clock::now();
        const auto  offsetSeconds = seconds(static_cast<long long>(std::llround(m_location.timezone * SECONDS_PER_HOUR)));
        const auto  shifted       = now + offsetSeconds;
        std::time_t tt            = system_clock::to_time_t(shifted);

        std::tm     utc{};
        gmtime_r(&tt, &utc);

        const double hours   = static_cast<double>(utc.tm_hour);
        const double minutes = static_cast<double>(utc.tm_min) / MINUTES_PER_HOUR;
        const double seconds = static_cast<double>(utc.tm_sec) / SECONDS_PER_HOUR;
        double       local   = std::fmod(hours + minutes + seconds, MINUTES_PER_DAY / MINUTES_PER_HOUR);
        if (local < 0)
            local += MINUTES_PER_DAY / MINUTES_PER_HOUR;
        return local;
    }

    void CSunCalculator::applyFallback(SSunTimes& times) const {
        if (times.sunriseMissing)
            times.sunrise = NO_EVENT_SENTINEL;
        if (times.sunsetMissing)
            times.sunset = NO_EVENT_SENTINEL;
    }

    std::string CSunCalculator::formatTime(double decimalHours) {
        if (decimalHours < 0)
            return "--:--";

        double totalMinutes = std::round(decimalHours * MINUTES_PER_HOUR);
        totalMinutes        = std::fmod(totalMinutes, MINUTES_PER_DAY);
        if (totalMinutes < 0)
            totalMinutes += MINUTES_PER_DAY;

        int  h = static_cast<int>(totalMinutes / MINUTES_PER_HOUR);
        int  m = static_cast<int>(std::fmod(totalMinutes, MINUTES_PER_HOUR));

        char buf[6];
        std::snprintf(buf, sizeof(buf), "%02d:%02d", h, m);
        return std::string(buf);
    }

    // ------------------ Math Helpers ------------------

    double CSunCalculator::deg2rad(double deg) {
        return deg * M_PI / HALF_CIRCLE_DEGREES;
    }
    double CSunCalculator::rad2deg(double rad) {
        return rad * HALF_CIRCLE_DEGREES / M_PI;
    }

    // ------------------ NOAA Core Functions ------------------
    // Based on NOAA solar position calculator reference implementation.

    double CSunCalculator::calcGeomMeanLongSun(double t) {
        double L0 = GEOM_MEAN_LONG_BASE + t * (GEOM_MEAN_LONG_COEFF_PRIMARY + t * GEOM_MEAN_LONG_COEFF_SECONDARY);
        L0        = std::fmod(L0, FULL_CIRCLE_DEGREES);
        if (L0 < 0)
            L0 += FULL_CIRCLE_DEGREES;
        return L0;
    }

    double CSunCalculator::calcGeomMeanAnomalySun(double t) {
        return GEOM_MEAN_ANOMALY_BASE + t * (GEOM_MEAN_ANOMALY_COEFF_PRIMARY - GEOM_MEAN_ANOMALY_COEFF_SECONDARY * t);
    }

    double CSunCalculator::calcEccentricityEarthOrbit(double t) {
        return ECCENTRICITY_BASE - t * (ECCENTRICITY_COEFF_PRIMARY + ECCENTRICITY_COEFF_SECONDARY * t);
    }

    double CSunCalculator::calcSunEqOfCenter(double t) {
        double m     = deg2rad(calcGeomMeanAnomalySun(t));
        double sinm  = std::sin(m);
        double sin2m = std::sin(2 * m);
        double sin3m = std::sin(3 * m);

        return sinm * (SUN_EQ_CENTER_TERM1 - t * (SUN_EQ_CENTER_TERM1_T1 + SUN_EQ_CENTER_TERM1_T2 * t)) + sin2m * (SUN_EQ_CENTER_TERM2 - SUN_EQ_CENTER_TERM2_T1 * t) +
            sin3m * SUN_EQ_CENTER_TERM3;
    }

    double CSunCalculator::calcSunTrueLong(double t) {
        return calcGeomMeanLongSun(t) + calcSunEqOfCenter(t);
    }

    double CSunCalculator::calcSunApparentLong(double t) {
        double omega = deg2rad(SUN_APP_LONG_OMEGA_BASE - SUN_APP_LONG_OMEGA_COEFF * t);
        return calcSunTrueLong(t) - SUN_APP_LONG_CORR_PRIMARY - SUN_APP_LONG_CORR_SECONDARY * std::sin(omega);
    }

    double CSunCalculator::calcMeanObliquityOfEcliptic(double t) {
        double seconds = MEAN_OBLIQUITY_SECONDS - t * (MEAN_OBLIQUITY_COEFF1 + t * (MEAN_OBLIQUITY_COEFF2 - MEAN_OBLIQUITY_COEFF3 * t));
        return OBLIQUITY_BASE_DEGREES + (OBLIQUITY_ARCMINUTES + seconds / MINUTES_PER_HOUR) / MINUTES_PER_HOUR;
    }

    double CSunCalculator::calcObliquityCorrection(double t) {
        double omega = deg2rad(SUN_APP_LONG_OMEGA_BASE - SUN_APP_LONG_OMEGA_COEFF * t);
        return calcMeanObliquityOfEcliptic(t) + OBLIQUITY_CORR_COEFF * std::cos(omega);
    }

    double CSunCalculator::calcSunDeclination(double t) {
        double eps    = deg2rad(calcObliquityCorrection(t));
        double lambda = deg2rad(calcSunApparentLong(t));
        double sint   = std::sin(eps) * std::sin(lambda);
        return rad2deg(std::asin(sint));
    }

    double CSunCalculator::calcEquationOfTime(double t) {
        double epsilon = deg2rad(calcObliquityCorrection(t));
        double L0      = deg2rad(calcGeomMeanLongSun(t));
        double e       = calcEccentricityEarthOrbit(t);
        double m       = deg2rad(calcGeomMeanAnomalySun(t));

        double y = std::tan(epsilon / 2.0);
        y *= y;

        double sin2L0 = std::sin(2.0 * L0);
        double sinm   = std::sin(m);
        double cos2L0 = std::cos(2.0 * L0);
        double sin4L0 = std::sin(4.0 * L0);
        double sin2m  = std::sin(2.0 * m);

        double Etime = y * sin2L0 - 2.0 * e * sinm + 4.0 * e * y * sinm * cos2L0 - EQUATION_OF_TIME_FACTOR1 * y * y * sin4L0 - EQUATION_OF_TIME_FACTOR2 * e * e * sin2m;

        return rad2deg(Etime) * 4.0; // minutes
    }

    double CSunCalculator::calcHourAngleSunrise(double lat, double solarDec) {
        double latRad = deg2rad(lat);
        double sdRad  = deg2rad(solarDec);
        double cosHA  = (std::cos(deg2rad(SOLAR_STANDARD_ALTITUDE)) / (std::cos(latRad) * std::cos(sdRad))) - std::tan(latRad) * std::tan(sdRad);

        if (cosHA > 1.0 + COSINE_TOLERANCE)
            return std::numeric_limits<double>::quiet_NaN(); // true polar night
        if (cosHA < -1.0 - COSINE_TOLERANCE)
            return std::numeric_limits<double>::quiet_NaN(); // true midnight sun

        cosHA = std::clamp(cosHA, -1.0, 1.0);
        return std::acos(cosHA);
    }

    double CSunCalculator::calcHourAngleSunset(double lat, double solarDec) {
        return calcHourAngleSunrise(lat, solarDec);
    }

    // ------------------ Julian Date Helpers ------------------

    double CSunCalculator::calcJD(int year, int month, int day) {
        if (month <= 2) {
            year -= 1;
            month += MONTHS_IN_YEAR;
        }
        int A = year / CENTURY_DIVISOR;
        int B = GREGORIAN_CORRECTION_NUMERATOR - A + (A / LEAP_DIVISOR);
        return std::floor(JULIAN_DAYS_PER_YEAR * (year + JULIAN_YEAR_SHIFT)) + std::floor(JULIAN_DAYS_PER_MONTH * (month + 1)) + day + B - JULIAN_DAY_CORRECTION;
    }

    double CSunCalculator::calcTimeJulianCent(double jd) {
        return (jd - JULIAN_DAY_J2000) / JULIAN_CENTURY_DAYS;
    }

    // ------------------ Sunrise / Sunset ------------------
    // Uses NOAA equations, returns minutes from midnight UTC.

    double CSunCalculator::calcSunriseUTC(int day, int month, int year, double latitude, double longitude) {
        return calcSunEventUTC(day, month, year, latitude, longitude, true);
    }

    double CSunCalculator::calcSunsetUTC(int day, int month, int year, double latitude, double longitude) {
        return calcSunEventUTC(day, month, year, latitude, longitude, false);
    }

    double CSunCalculator::calcSunEventUTC(int day, int month, int year, double latitude, double longitude, bool isSunrise) {
        const double jd       = calcJD(year, month, day);
        double       julianT  = calcTimeJulianCent(jd);
        double       eventUTC = NO_EVENT_SENTINEL;

        for (int iteration = 0; iteration < 2; ++iteration) {
            const double eqTime   = calcEquationOfTime(julianT);
            const double solarDec = calcSunDeclination(julianT);
            const double ha       = isSunrise ? calcHourAngleSunrise(latitude, solarDec) : calcHourAngleSunset(latitude, solarDec);

            if (std::isnan(ha))
                return NO_EVENT_SENTINEL;

            const double haDeg        = rad2deg(ha);
            const double solarNoonUTC = MINUTES_AT_NOON - MINUTES_PER_DEGREE * longitude - eqTime;
            const double offset       = MINUTES_PER_DEGREE * haDeg;
            eventUTC                  = isSunrise ? solarNoonUTC - offset : solarNoonUTC + offset;

            eventUTC = std::fmod(eventUTC, MINUTES_PER_DAY);
            if (eventUTC < 0)
                eventUTC += MINUTES_PER_DAY;

            if (iteration == 0) {
                const double newJD = jd + eventUTC / MINUTES_PER_DAY;
                julianT            = calcTimeJulianCent(newJD);
            }
        }

        return eventUTC;
    }

} // namespace NSunCalc
