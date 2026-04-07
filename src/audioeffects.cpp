//
// audioeffects.cpp
//
// mt32-pi - A baremetal MIDI synthesizer for Raspberry Pi
// Copyright (C) 2020-2023 Dale Whinham <daleyo@gmail.com>
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

#include "audioeffects.h"

#include <cassert>
#include <cmath>
#include <cstring>

// Freeverb original tunings (for 44.1 kHz); delayed lines are scaled at runtime.
static constexpr int CombTuningL[CAudioEffects::NumCombs] =
	{1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};

static constexpr int AllpassTuningL[CAudioEffects::NumAllpass] =
	{556, 441, 341, 225};

static constexpr float Pi = 3.14159265358979323846f;

// ---------------------------------------------------------------------------
// TComb
// ---------------------------------------------------------------------------

void CAudioEffects::TComb::Init(int size)
{
	Free();
	pBuffer  = new float[size]();   // zero-initialised
	nSize    = size;
	nPos     = 0;
	fFilterStore = 0.0f;
}

void CAudioEffects::TComb::Free()
{
	delete[] pBuffer;
	pBuffer = nullptr;
	nSize   = 0;
}

float CAudioEffects::TComb::Process(float x)
{
	assert(pBuffer);
	float output = pBuffer[nPos];

	// Lowpass filter inside the feedback loop (Schroeder-Moorer damping)
	fFilterStore = output * fDamp2 + fFilterStore * fDamp1;

	pBuffer[nPos] = x + fFilterStore * fFeedback;

	if (++nPos >= nSize)
		nPos = 0;

	return output;
}

// ---------------------------------------------------------------------------
// TAllpass
// ---------------------------------------------------------------------------

void CAudioEffects::TAllpass::Init(int size)
{
	Free();
	pBuffer = new float[size]();
	nSize   = size;
	nPos    = 0;
}

void CAudioEffects::TAllpass::Free()
{
	delete[] pBuffer;
	pBuffer = nullptr;
	nSize   = 0;
}

float CAudioEffects::TAllpass::Process(float x)
{
	assert(pBuffer);
	float buf = pBuffer[nPos];
	pBuffer[nPos] = x + buf * 0.5f;

	if (++nPos >= nSize)
		nPos = 0;

	return buf - x;
}

// ---------------------------------------------------------------------------
// Biquad coefficient builders — Audio EQ Cookbook (peaking EQ shelves, S=1)
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Unified biquad shelf coefficient builder
// Audio EQ Cookbook (R. Bristow-Johnson) — S = 1 shelf slope
// s = +1 → low shelf;  s = −1 → high shelf
// ---------------------------------------------------------------------------

void CAudioEffects::ComputeShelfCoeff(EShelfType type, TBiquad& f,
                                       float fFreqHz, float fGainDb, float fSr)
{
	const float s    = (type == EShelfType::Low) ? 1.0f : -1.0f;
	const float A    = powf(10.0f, fGainDb / 40.0f);   // sqrt(10^(dB/20))
	const float w0   = 2.0f * Pi * fFreqHz / fSr;
	const float cosw = cosf(w0);
	const float sinw = sinf(w0);
	const float alpha = sinw / 2.0f * sqrtf(2.0f);      // S = 1
	const float sqA  = sqrtf(A);

	const float a0inv = 1.0f / ((A + 1.0f) + s * (A - 1.0f) * cosw + 2.0f * sqA * alpha);

	f.b0 = A * ((A + 1.0f) - s * (A - 1.0f) * cosw + 2.0f * sqA * alpha) * a0inv;
	f.b1 = 2.0f * A * (s * (A - 1.0f) - (A + 1.0f) * cosw) * a0inv;
	f.b2 = A * ((A + 1.0f) - s * (A - 1.0f) * cosw - 2.0f * sqA * alpha) * a0inv;
	f.a1 = -2.0f * (s * (A - 1.0f) + (A + 1.0f) * cosw) * a0inv;
	f.a2 = ((A + 1.0f) + s * (A - 1.0f) * cosw - 2.0f * sqA * alpha) * a0inv;
	f.z1 = f.z2 = 0.0f;
}

// ---------------------------------------------------------------------------
// CAudioEffects
// ---------------------------------------------------------------------------

CAudioEffects::CAudioEffects()
{
	// Delay lines allocated in Configure()
}

CAudioEffects::~CAudioEffects()
{
	for (int i = 0; i < NumCombs; ++i)
	{
		m_CombL[i].Free();
		m_CombR[i].Free();
	}
	for (int i = 0; i < NumAllpass; ++i)
	{
		m_AllpassL[i].Free();
		m_AllpassR[i].Free();
	}
}

void CAudioEffects::Configure(const TConfig& Config, float fSampleRate)
{
	m_Config      = Config;
	m_fSampleRate = fSampleRate;

	// --- EQ biquads ---
	const float bassGain   = static_cast<float>(Config.nBassGain);
	const float trebleGain = static_cast<float>(Config.nTrebleGain);
	ComputeShelfCoeff(EShelfType::Low,  m_BassL,   BassFrequency,   bassGain,   fSampleRate);
	ComputeShelfCoeff(EShelfType::Low,  m_BassR,   BassFrequency,   bassGain,   fSampleRate);
	ComputeShelfCoeff(EShelfType::High, m_TrebleL, TrebleFrequency, trebleGain, fSampleRate);
	ComputeShelfCoeff(EShelfType::High, m_TrebleR, TrebleFrequency, trebleGain, fSampleRate);

	// --- Reverb delay lines ---
	// Original Freeverb tunings are for 44.1 kHz; scale to current sample rate.
	const float scale = fSampleRate / 44100.0f;

	for (int i = 0; i < NumCombs; ++i)
	{
		const int sizeL = static_cast<int>(CombTuningL[i] * scale + 0.5f);
		const int sizeR = sizeL + StereoSpread;

		m_CombL[i].Init(sizeL);
		m_CombR[i].Init(sizeR);

		const float feedback = Config.fReverbRoomSize * ScaleRoom + OffsetRoom;
		const float damp     = Config.fReverbDamping * ScaleDamp;
		m_CombL[i].SetFeedback(feedback);  m_CombL[i].SetDamp(damp);
		m_CombR[i].SetFeedback(feedback);  m_CombR[i].SetDamp(damp);
	}

	for (int i = 0; i < NumAllpass; ++i)
	{
		const int sizeL = static_cast<int>(AllpassTuningL[i] * scale + 0.5f);
		const int sizeR = sizeL + StereoSpread;

		m_AllpassL[i].Init(sizeL);
		m_AllpassR[i].Init(sizeR);
	}
}

void CAudioEffects::ProcessEQ(float& xL, float& xR)
{
	xL = m_TrebleL.Process(m_BassL.Process(xL));
	xR = m_TrebleR.Process(m_BassR.Process(xR));
}

void CAudioEffects::ProcessReverb(float& xL, float& xR, float fWet)
{
	const float fDry = 1.0f - fWet;
	float inL = xL * FixedGain;
	float inR = xR * FixedGain;
	float outL = 0.0f, outR = 0.0f;

	for (int i = 0; i < NumCombs; ++i)
	{
		outL += m_CombL[i].Process(inL);
		outR += m_CombR[i].Process(inR);
	}
	for (int i = 0; i < NumAllpass; ++i)
	{
		outL = m_AllpassL[i].Process(outL);
		outR = m_AllpassR[i].Process(outR);
	}

	xL = xL * fDry + outL * fWet;
	xR = xR * fDry + outR * fWet;
}

void CAudioEffects::ProcessLimiter(float& xL, float& xR)
{
	// Soft-knee saturation (optional)
	if (m_Config.bLimiterEnabled)
	{
		auto ApplyKnee = [](float& x)
		{
			const float ax = x < 0.0f ? -x : x;
			if (ax > 0.8f)
			{
				float t = (ax - 0.8f) * 5.0f;
				if (t > 1.0f) t = 1.0f;
				// Hermite cubic: H(t) = -0.2t³ + 0.2t² + 0.2t + 0.8
				// H(0)=0.8, H(1)=1, H'(0)=1 (unity gain at knee), H'(1)=0
				const float y = ((-0.2f * t + 0.2f) * t + 0.2f) * t + 0.8f;
				x = x < 0.0f ? -y : y;
			}
		};
		ApplyKnee(xL);
		ApplyKnee(xR);
	}
	// Safety hard clamp (always on) — catches any residual out-of-range signal
	if (xL >  1.0f) xL =  1.0f;
	if (xL < -1.0f) xL = -1.0f;
	if (xR >  1.0f) xR =  1.0f;
	if (xR < -1.0f) xR = -1.0f;
}

void CAudioEffects::Process(float* pBuffer, size_t nFrames)
{
	const bool  bEQ     = m_Config.bEQEnabled;
	const bool  bReverb = m_Config.bReverbEnabled;
	const float fWet    = m_Config.fReverbWet;

	for (size_t f = 0; f < nFrames; ++f)
	{
		float xL = pBuffer[f * 2 + 0];
		float xR = pBuffer[f * 2 + 1];

		if (bEQ)
			ProcessEQ(xL, xR);

		if (bReverb)
			ProcessReverb(xL, xR, fWet);

		ProcessLimiter(xL, xR);

		pBuffer[f * 2 + 0] = xL;
		pBuffer[f * 2 + 1] = xR;
	}
}
