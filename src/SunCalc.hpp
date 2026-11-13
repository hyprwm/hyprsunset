#ifndef SUNCALC_HPP
#define SUNCALC_HPP

#include <string>

namespace NSunCalc {

    struct SLocation {
        double latitude;  // degrees
        double longitude; // degrees
        double timezone;  // UTC offset (e.g., -5 for EST)
    };

    struct SSunTimes {
        double sunrise; // decimal hours (local time)
        double sunset;  // decimal hours (local time)
        bool   sunriseMissing{false};
        bool   sunsetMissing{false};
    };

    class CSunCalculator {
      public:
        explicit CSunCalculator(const SLocation& loc);

        SSunTimes          compute(int year, int month, int day);
        SSunTimes          compute();
        SSunTimes          computeWithFallback(int year, int month, int day);
        SSunTimes          computeWithFallback();
        static std::string formatTime(double decimalHours);

      private:
        SLocation m_location;
        double    currentLocalHours() const;
        void      applyFallback(SSunTimes& times) const;

        // NOAA algorithm internal helpers
        double calcSunriseUTC(int day, int month, int year, double latitude, double longitude);

        double calcSunsetUTC(int day, int month, int year, double latitude, double longitude);

        // Math helpers
        static double deg2rad(double deg);
        static double rad2deg(double rad);

        // NOAA internal
        static double calcGeomMeanLongSun(double t);
        static double calcGeomMeanAnomalySun(double t);
        static double calcEccentricityEarthOrbit(double t);
        static double calcSunEqOfCenter(double t);
        static double calcSunTrueLong(double t);
        static double calcSunApparentLong(double t);
        static double calcMeanObliquityOfEcliptic(double t);
        static double calcObliquityCorrection(double t);
        static double calcSunDeclination(double t);
        static double calcEquationOfTime(double t);
        static double calcHourAngleSunrise(double lat, double solarDec);
        static double calcHourAngleSunset(double lat, double solarDec);

        static double calcTimeJulianCent(double jd);
        static double calcJD(int year, int month, int day);
        static double calcSunEventUTC(int day, int month, int year, double latitude, double longitude, bool isSunrise);

        // Shared constants
        static constexpr double MINUTES_PER_HOUR                  = 60.0;
        static constexpr double MINUTES_PER_DAY                   = 1440.0;
        static constexpr double MINUTES_AT_NOON                   = 720.0;
        static constexpr double MINUTES_PER_DEGREE                = 4.0;
        static constexpr double SECONDS_PER_HOUR                  = 3600.0;
        static constexpr double NO_EVENT_SENTINEL                 = -1.0;
        static constexpr double FULL_CIRCLE_DEGREES               = 360.0;
        static constexpr double HALF_CIRCLE_DEGREES               = 180.0;
        static constexpr double SOLAR_STANDARD_ALTITUDE           = 90.833;
        static constexpr double COSINE_TOLERANCE                  = 1e-9;
        static constexpr double JULIAN_DAYS_PER_YEAR              = 365.25;
        static constexpr double JULIAN_DAYS_PER_MONTH             = 30.6001;
        static constexpr int    JULIAN_YEAR_SHIFT                 = 4716;
        static constexpr int    MONTHS_IN_YEAR                    = 12;
        static constexpr int    GREGORIAN_CORRECTION_NUMERATOR    = 2;
        static constexpr int    CENTURY_DIVISOR                   = 100;
        static constexpr int    LEAP_DIVISOR                      = 4;
        static constexpr double JULIAN_DAY_CORRECTION             = 1524.5;
        static constexpr double JULIAN_DAY_J2000                  = 2451545.0;
        static constexpr double JULIAN_CENTURY_DAYS               = 36525.0;
        static constexpr double GEOM_MEAN_LONG_BASE               = 280.46646;
        static constexpr double GEOM_MEAN_LONG_COEFF_PRIMARY      = 36000.76983;
        static constexpr double GEOM_MEAN_LONG_COEFF_SECONDARY    = 0.0003032;
        static constexpr double GEOM_MEAN_ANOMALY_BASE            = 357.52911;
        static constexpr double GEOM_MEAN_ANOMALY_COEFF_PRIMARY   = 35999.05029;
        static constexpr double GEOM_MEAN_ANOMALY_COEFF_SECONDARY = 0.0001537;
        static constexpr double ECCENTRICITY_BASE                 = 0.016708634;
        static constexpr double ECCENTRICITY_COEFF_PRIMARY        = 0.000042037;
        static constexpr double ECCENTRICITY_COEFF_SECONDARY      = 0.0000001267;
        static constexpr double SUN_EQ_CENTER_TERM1               = 1.914602;
        static constexpr double SUN_EQ_CENTER_TERM1_T1            = 0.004817;
        static constexpr double SUN_EQ_CENTER_TERM1_T2            = 0.000014;
        static constexpr double SUN_EQ_CENTER_TERM2               = 0.019993;
        static constexpr double SUN_EQ_CENTER_TERM2_T1            = 0.000101;
        static constexpr double SUN_EQ_CENTER_TERM3               = 0.000289;
        static constexpr double SUN_APP_LONG_OMEGA_BASE           = 125.04;
        static constexpr double SUN_APP_LONG_OMEGA_COEFF          = 1934.136;
        static constexpr double SUN_APP_LONG_CORR_PRIMARY         = 0.00569;
        static constexpr double SUN_APP_LONG_CORR_SECONDARY       = 0.00478;
        static constexpr double MEAN_OBLIQUITY_SECONDS            = 21.448;
        static constexpr double MEAN_OBLIQUITY_COEFF1             = 46.815;
        static constexpr double MEAN_OBLIQUITY_COEFF2             = 0.00059;
        static constexpr double MEAN_OBLIQUITY_COEFF3             = 0.001813;
        static constexpr double OBLIQUITY_BASE_DEGREES            = 23.0;
        static constexpr double OBLIQUITY_ARCMINUTES              = 26.0;
        static constexpr double OBLIQUITY_CORR_COEFF              = 0.00256;
        static constexpr double EQUATION_OF_TIME_FACTOR1          = 0.5;
        static constexpr double EQUATION_OF_TIME_FACTOR2          = 1.25;
    };

} // namespace NSunCalc

#endif // SUNCALC_HPP
