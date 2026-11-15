//
// powermonitor.cpp
//
// mt32-pi - A baremetal MIDI synthesizer for Raspberry Pi
// Copyright (C) 2026 pim-32 contributors
//
// This file is part of mt32-pi.
//
// mt32-pi is free software: you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version.
//
// mt32-pi is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
// FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
// details.
//
// You should have received a copy of the GNU General Public License along with
// mt32-pi. If not, see <http://www.gnu.org/licenses/>.
//

#include <circle/logger.h>

#include <cstdint>

#include "powermonitor.h"

LOGMODULE("power");

namespace
{
	// INA219 registers
	constexpr u8 RegisterConfig     = 0x00;
	constexpr u8 RegisterBus        = 0x02;
	constexpr u8 RegisterPower      = 0x03;
	constexpr u8 RegisterCurrent    = 0x04;
	constexpr u8 RegisterCalibration = 0x05;

	// 32 V range, /8 shunt gain (320 mV), 12-bit 128-sample ADC, continuous
	// shunt+bus conversion.
	constexpr u16 ConfigValue = 0x3CCF;

	constexpr unsigned DefaultCurrentLSBUa = 100;

	int16_t Signed16(u16 nValue)
	{
		return static_cast<int16_t>(nValue);
	}
}

CPowerMonitor::CPowerMonitor()
	: m_pI2CMaster(nullptr),
	  m_bPresent(false),
	  m_Address(0x40),
	  m_nShuntMilliohms(100),
	  m_nCurrentLSBUa(DefaultCurrentLSBUa),
	  m_nErrors(0)
{
}

bool CPowerMonitor::Initialize(CI2CMaster* pI2CMaster, bool bEnabled, u8 Address, unsigned nShuntMilliohms)
{
	m_pI2CMaster = pI2CMaster;
	m_Address = Address;
	m_nShuntMilliohms = nShuntMilliohms ? nShuntMilliohms : 100;
	m_nCurrentLSBUa = DefaultCurrentLSBUa;
	m_nErrors = 0;
	m_bPresent = false;

	if (!bEnabled || !m_pI2CMaster)
		return false;

	// Probe before writing: an absent device must not slow down boot or make
	// the bus log an error on every sample. A floating bus reads all ones.
	u16 nConfig;
	if (!ReadRegister(RegisterConfig, &nConfig) || nConfig == 0xFFFF)
		return false;

	// Pick a current LSB that keeps the calibration register in range. 100 uA
	// gives +/-3.2 A with the common 0.1 ohm shunt.
	while (m_nCurrentLSBUa < 3200)
	{
		const unsigned nCalibration = 40960000u / (m_nCurrentLSBUa * m_nShuntMilliohms);
		if (nCalibration > 0 && nCalibration <= 65535)
			break;
		m_nCurrentLSBUa *= 2;
	}

	const unsigned nCalibration = 40960000u / (m_nCurrentLSBUa * m_nShuntMilliohms);
	if (nCalibration == 0 || nCalibration > 65535)
	{
		LOGWARN("INA219 shunt value out of range");
		return false;
	}

	if (!WriteRegister(RegisterConfig, ConfigValue) ||
	    !WriteRegister(RegisterCalibration, static_cast<u16>(nCalibration)))
		return false;

	m_bPresent = true;
	LOGNOTE("INA219 on 0x%02x, %u mOhm shunt, %u uA/LSB", m_Address, m_nShuntMilliohms, m_nCurrentLSBUa);
	return true;
}

TPowerSample CPowerMonitor::Sample()
{
	TPowerSample Sample = {false, 0, 0, 0};

	if (!m_bPresent)
		return Sample;

	u16 nBusRaw;
	u16 nCurrentRaw;
	u16 nPowerRaw;

	if (!ReadRegister(RegisterBus, &nBusRaw) ||
	    !ReadRegister(RegisterCurrent, &nCurrentRaw) ||
	    !ReadRegister(RegisterPower, &nPowerRaw))
	{
		// A single bus glitch must not disable the monitor; three in a row do,
		// so a removed board stops costing a transaction every sample.
		if (++m_nErrors >= 3)
		{
			m_bPresent = false;
			LOGWARN("INA219 stopped responding");
		}
		return Sample;
	}

	m_nErrors = 0;
	Sample.bPresent = true;
	Sample.nBusMillivolts = (nBusRaw >> 3) * 4;
	Sample.nCurrentMilliamps = (Signed16(nCurrentRaw) * static_cast<int>(m_nCurrentLSBUa)) / 1000;

	// Power LSB is 20 * current LSB; use 64-bit maths so a large current LSB
	// cannot overflow the register product.
	const long long nPowerMilliwatts =
		(static_cast<long long>(nPowerRaw) * m_nCurrentLSBUa * 20) / 1000;
	Sample.nPowerMilliwatts = nPowerMilliwatts > 0x7FFFFFFF ? 0x7FFFFFFF : static_cast<int>(nPowerMilliwatts);

	return Sample;
}

bool CPowerMonitor::WriteRegister(u8 nRegister, u16 nValue)
{
	const u8 Buffer[3] = {nRegister, static_cast<u8>(nValue >> 8), static_cast<u8>(nValue & 0xFF)};
	return m_pI2CMaster->Write(m_Address, Buffer, sizeof(Buffer)) == static_cast<int>(sizeof(Buffer));
}

bool CPowerMonitor::ReadRegister(u8 nRegister, u16* pValue)
{
	u8 Buffer[2];
	if (m_pI2CMaster->WriteReadRepeatedStart(m_Address, &nRegister, 1, Buffer, sizeof(Buffer)) !=
	    static_cast<int>(sizeof(Buffer)))
		return false;

	*pValue = (static_cast<u16>(Buffer[0]) << 8) | Buffer[1];
	return true;
}
