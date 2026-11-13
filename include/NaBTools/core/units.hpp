// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleg Kozhevnikov
#pragma once
#include <cstdint>
namespace NaBTools::runtime
{
// Canonical SI values; conversions are computed on demand, never stored copies.
	struct Pressure {
		double pascals{};
		constexpr double kilopascals()const
		{
			return pascals / 1000;
		}

		constexpr double millibars()const
		{
			return pascals / 100;
		}

		constexpr double bars()const
		{
			return pascals / 100000;
		}

		constexpr double psi()const
		{
			return pascals / 6894.757293168;
		}

		constexpr double atmospheres()const
		{
			return pascals / 101325;
		}

		constexpr double kilogram_force_per_square_cm()const
		{
			return pascals / 98066.5;
		}

	};
	struct Temperature {
		double celsius{};
		static constexpr Temperature from_centi_c(int32_t value)
		{
			return {value / 100.0};
		}

		constexpr double fahrenheit()const
		{
			return celsius * 1.8 + 32;
		}

	};
}
