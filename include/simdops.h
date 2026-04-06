//
// simdops.h
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

#ifndef _simdops_h
#define _simdops_h

#include <cstddef>

#ifdef __aarch64__
#include <arm_neon.h>
#endif

namespace SimdOps
{

// Scale every element of buf[0..n) by gain in-place.
inline void ApplyGain(float* buf, size_t n, float gain)
{
#ifdef __aarch64__
	float32x4_t vGain = vdupq_n_f32(gain);
	size_t i = 0;
	for (; i + 4 <= n; i += 4)
	{
		float32x4_t v = vld1q_f32(buf + i);
		vst1q_f32(buf + i, vmulq_f32(v, vGain));
	}
	for (; i < n; ++i)
		buf[i] *= gain;
#else
	for (size_t i = 0; i < n; ++i)
		buf[i] *= gain;
#endif
}

// Mix src into dst using separate left/right gains.
// n is the total number of float samples (stereo-interleaved: L R L R ...).
// Caller must ensure n is a multiple of 2.
inline void MixWithPanGain(float* dst, const float* src, size_t n,
                            float gainL, float gainR)
{
#ifdef __aarch64__
	// Pack {gainL, gainR, gainL, gainR} to process 2 stereo frames per iteration.
	const float gainPairs[4] = { gainL, gainR, gainL, gainR };
	float32x4_t vGains = vld1q_f32(gainPairs);
	size_t i = 0;
	for (; i + 4 <= n; i += 4)
	{
		float32x4_t vOut  = vld1q_f32(dst + i);
		float32x4_t vSrc  = vld1q_f32(src + i);
		vst1q_f32(dst + i, vmlaq_f32(vOut, vSrc, vGains));
	}
	for (; i < n; i += 2)
	{
		dst[i]     += src[i]     * gainL;
		dst[i + 1] += src[i + 1] * gainR;
	}
#else
	for (size_t i = 0; i < n; i += 2)
	{
		dst[i]     += src[i]     * gainL;
		dst[i + 1] += src[i + 1] * gainR;
	}
#endif
}

// Apply gain then hard-clamp every element of buf[0..n) to [lo, hi] in-place.
inline void ApplyGainAndClamp(float* buf, size_t n, float gain, float lo, float hi)
{
#ifdef __aarch64__
	float32x4_t vGain = vdupq_n_f32(gain);
	float32x4_t vMin  = vdupq_n_f32(lo);
	float32x4_t vMax  = vdupq_n_f32(hi);
	size_t i = 0;
	for (; i + 4 <= n; i += 4)
	{
		float32x4_t v = vld1q_f32(buf + i);
		v = vmulq_f32(v, vGain);
		v = vmaxq_f32(v, vMin);
		v = vminq_f32(v, vMax);
		vst1q_f32(buf + i, v);
	}
	for (; i < n; ++i)
	{
		buf[i] *= gain;
		buf[i] = buf[i] < lo ? lo : (buf[i] > hi ? hi : buf[i]);
	}
#else
	for (size_t i = 0; i < n; ++i)
	{
		buf[i] *= gain;
		buf[i] = buf[i] < lo ? lo : (buf[i] > hi ? hi : buf[i]);
	}
#endif
}

} // namespace SimdOps

#endif // _simdops_h
