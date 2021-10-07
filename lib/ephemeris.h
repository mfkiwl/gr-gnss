/* -*- c++ -*- */
/*
 * Copyright 2021 Lukasz Wiecaszek <lukasz.wiecaszek@gmail.com>.
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3, or (at your option)
 * any later version.
 *
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this software; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301, USA.
 */

#ifndef INCLUDED_GNSS_EPHEMERIS_H
#define INCLUDED_GNSS_EPHEMERIS_H

#include <cstdio>
#include <cmath>
#include <string>

#include "gnss_parameters.h"
#include "vector.hpp"

namespace gr {
  namespace gnss {

    using vector = lts::vector<double, 3>;

    struct ephemeris_correction_terms
    {
      double C_RC; // Amplitude of the Cosine Harmonic Correction Term to the Orbit Radius [m]
      double C_RS; // Amplitude of the Sine Harmonic Correction Term to the Orbit Radius [m]
      double C_UC; // Amplitude of the Cosine Harmonic Correction Term to the Argument of Latitude [rad]
      double C_US; // Amplitude of the Sine Harmonic Correction Term to the Argument of Latitude [rad]
      double C_IC; // Amplitude of the Cosine Harmonic Correction Term to the Angle of Inclination [rad]
      double C_IS; // Amplitude of the Sine Harmonic Correction Term to the Angle of Inclination [rad]

      std::string to_string() const
      {
        char strbuf[256];

        snprintf(strbuf, sizeof(strbuf),
          "correction terms:\n"
          "\torbit radius:         (%+e, %+e) [m]\n"
          "\targument of latitude: (%+e, %+e) [rad]\n"
          "\tangle of inclination: (%+e, %+e) [rad]\n",
            C_RC, C_RS,
            C_UC, C_US,
            C_IC, C_IS);

        return std::string(strbuf);
      }

      operator std::string () const
      {
        return to_string();
      }
    };

    struct ephemeris
    {
      ephemeris() :
        is_valid{false},
        IODE{-1, -1}
      {
      }

      bool is_valid;
      int IODE[2];       // Issue of Data - Ephemeris
      int t_oe;          // Ephemeris data reference time of week [s] (IS-GPS-200L 20.3.4.5 "Reference Times")
      double e;          // Eccentricity [dimensionless]
      double sqrt_a;     // Square Root of the Semi-Major Axis [m^1/2]
      double M_0;        // Mean Anomaly at Reference Time [rad]
      double delta_n;    // Mean Motion Difference From Computed Value [rad/s]
      double OMEGA_0;    // Longitude of Ascending Node of Orbit Plane at Weekly Epoch [rad]
      double i_0;        // Inclination Angle at Reference Time [rad]
      double omega;      // Argument of Perigee [rad]
      double dOMEGA_dt;  // Rate of Right Ascension [rad/s]
      double dI_dt;      // Rate of Inclination Angle [rad/s]

      ephemeris_correction_terms correction_terms;

      // t is GPS system time at time of transmission, i.e., GPS time corrected for transit time (range/speed of light).
      // Furthermore, tk shall be the actual total time difference between the time t and the epoch time t_oe, and must
      // account for beginning or end of week crossovers.
      // That is, if tk is greater than 302400 seconds, subtract 604800 seconds from tk.
      // If tk is less than -302400 seconds, add 604800 seconds to tk.
      double get_tk(double t)
      {
        double tk = t - t_oe;

        if (tk > (GPS_SECONDS_PER_WEEK / 2))
        {
          tk -= GPS_SECONDS_PER_WEEK;
        }
        else
        if (tk < -(GPS_SECONDS_PER_WEEK / 2))
        {
          tk += GPS_SECONDS_PER_WEEK;
        }

        return tk;
      }

      // We know that M = E - e * sin(E), but now having got M
      // we need to figure out (calculate) E.
      double get_E(double M)
      {
        double E[2] = {M, M}; // initial condition
#if 0
        for (double dE = INFINITY; std::fabs(dE) > 1e-12; E[0] = E[1]) {
          double delta = (M - E[0] + e * std::sin(E[0])) / (1 - e * std::cos(E[0]));
          E[1] = E[0] + delta;
          dE = fmod(delta, 2 * M_PI);
        }
#else
        for (double dE = INFINITY; std::fabs(dE) > 1e-12; E[0] = E[1]) {
          E[1] = M + e * std::sin(E[1]);
          dE = fmod(E[1] - E[0], 2 * M_PI);
        }
#endif
        return E[1];
      }

      double get_ni(double E)
      {
#if 0
        double y = std::sqrt(1.0 - e * e) * std::sin(E);
        double x = std::cos(E) - e;
        return std::atan2(y, x);
#else
        return 2 * std::atan(std::sqrt((1+e)/(1-e)) * std::tan(E/2));
#endif
      }

      void get_vectors(double t, vector* position, vector* velocity, vector* acceleration)
      {
        double a = sqrt_a * sqrt_a; // semi-major axis [m]
        double n0 = std::sqrt(GPS_MI / (a * a * a)); // computed mean motion [((m^3/s^2)/m^3)^1/2] = [rad/s]
        double n = n0 + delta_n; // corrected mean motion [rad/s]
        double tk = get_tk(t); // time difference relative to ephemeris reference epoch [s]
        double M = M_0 + n * tk; // mean anomaly [rad] (that one changes linearly with time)
        double E = get_E(M); // eccentric anomaly [rad]
        double ni = get_ni(E); // true anomaly [rad]

        double phi = ni + omega; // argument of latitude [rad]
        double cos_2phi = std::cos(2 * phi);
        double sin_2phi = std::sin(2 * phi);

        double do_dt = dOMEGA_dt - GPS_dOMEGA_dt_EARTH; // rate of right ascension in ECEF coordinate system [rad/s]
        double gamma = 1.0 - e * std::cos(E);

        double delta_r = // radius correction [m] (second harmonic)
          correction_terms.C_RC * cos_2phi + correction_terms.C_RS * sin_2phi;
        double delta_u = // latitude correction [rad] (second harmonic)
          correction_terms.C_UC * cos_2phi + correction_terms.C_US * sin_2phi;
        double delta_i = // inclination correction [rad] (second harmonic)
          correction_terms.C_IC * cos_2phi + correction_terms.C_IS * sin_2phi;

        double r = a * gamma + delta_r; // corrected radius [m]
        double u = phi + delta_u; // corrected argument of latitude [rad]
        double i = i_0 + dI_dt * tk + delta_i; // corrected inclination [rad]
        double o = OMEGA_0 + do_dt * tk - GPS_dOMEGA_dt_EARTH * t_oe; // corrected longitude of ascending node [rad]

        double x_prime = r * std::cos(u); // position in orbital plane
        double y_prime = r * std::sin(u); // position in orbital plane
        double r_prime = y_prime * std::cos(i); // position in xy plane

        if (position) {
          // satellite position in Earth Fixed Earth Centered (EFEC) coordinates
          double x = x_prime * std::cos(o) - r_prime * std::sin(o);
          double y = x_prime * std::sin(o) + r_prime * std::cos(o);
          double z = y_prime * std::sin(i);

          *position = {x, y, z};
        }

        if (velocity) {
          // satellite velocity in Earth Fixed Earth Centered (EFEC) coordinates
          double dE_dt = n / gamma; // eccentric anomaly rate [rad/s]
          double dni_dt = dE_dt * std::sqrt(1 - e * e) / gamma; // true anomaly rate [rad/s]

          double delta_dr_dt = // radius rate correction [m/s] (second harmonic)
            correction_terms.C_RS * cos_2phi - correction_terms.C_RC * sin_2phi;
          double delta_du_dt = // latitude rate correction [rad/s] (second harmonic)
            correction_terms.C_US * cos_2phi - correction_terms.C_UC * sin_2phi;
          double delta_di_dt = // inclination rate correction [rad/s] (second harmonic)
            correction_terms.C_IS * cos_2phi - correction_terms.C_IC * sin_2phi;

          double dr_dt = e * a * dE_dt * std::sin(E) + 2 * dni_dt * delta_dr_dt; // corrected radius rate [m/s]
          double du_dt = dni_dt * (1 + 2 * delta_du_dt); // corrected argument of latitude rate [rad/s]
          double di_dt = dI_dt + 2 * dni_dt * delta_di_dt; // corrected inclination angle rate [rad/s]

          double dx_dt_prime = dr_dt * std::cos(u) - r * du_dt * std::sin(u); // velocity in orbital plane
          double dy_dt_prime = dr_dt * std::sin(u) + r * du_dt * std::cos(u); // velocity in orbital plane
          double dr_dt_prime = dy_dt_prime * std::cos(i) - y_prime * di_dt * std::sin(i); // velocity in xy plane

          double dx_dt = dx_dt_prime * std::cos(o) - x_prime * do_dt * std::sin(o) - dr_dt_prime * sin(o) - r_prime * do_dt * std::cos(o);
          double dy_dt = dx_dt_prime * std::sin(o) + x_prime * do_dt * std::cos(o) + dr_dt_prime * cos(o) - r_prime * do_dt * std::sin(o);
          double dz_dt = dy_dt_prime * std::sin(i) + y_prime * di_dt * std::cos(i);

          *velocity = {dx_dt, dy_dt, dz_dt};
        }

        if (acceleration) {
          // satellite acceleration in Earth Fixed Earth Centered (EFEC) coordinates
        }
      }

      std::string to_string() const
      {
        char strbuf[1024];

        snprintf(strbuf, sizeof(strbuf),
          "IODE: %d/%d, t_oe: %d [s]\n"
          "\te:          %+e [dimensionless]\n"
          "\tsqrt_a:     %+e [m^1/2]\n"
          "\tM_0:        %+e [rad]\n"
          "\tdelta_n:    %+e [rad/s]\n"
          "\tOMEGA_0:    %+e [rad]\n"
          "\ti_0:        %+e [rad]\n"
          "\tomega:      %+e [rad]\n"
          "\tdOMEGA_dt:  %+e [rad/s]\n"
          "\tdI_dt:      %+e [rad/s]\n"
          "%s\n",
            IODE[0], IODE[1], t_oe, e, sqrt_a, M_0, delta_n, OMEGA_0, i_0, omega, dOMEGA_dt, dI_dt,
            correction_terms.to_string().c_str());

        return std::string(strbuf);
      }

      operator std::string () const
      {
        return to_string();
      }
    };

  } // namespace gnss
} // namespace gr

#endif /* INCLUDED_GNSS_EPHEMERIS_H */

