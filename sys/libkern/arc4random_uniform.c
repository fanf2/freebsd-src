/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright 2022 Tony Finch <fanf@FreeBSD.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/types.h>
#include <sys/libkern.h>

/*
 * Return an unbiased bounded random number using Daniel Lemire's
 * nearly-divisionless algorithm, https://lemire.me/blog/?p=17551
 *
 * Raw `arc4random()` returns a 32-bit value that we use as the low
 * half of a 32.32 fixed-point number, forming a fraction less than
 * one. We do a 64-bit multiply `arc4random() * limit` and use the
 * product as a 32.32 fixed-point value less than the limit. (See
 * Donald Knuth's Art of Computer Programming, volume 2, 3rd edition,
 * section 3.6, point (vi) on page 185.) Our result will be the
 * integer part (upper 32 bits), but instead of simply truncating as
 * Knuth suggests, we will use the fraction part (lower 32 bits) to
 * determine whether or not we need to resample.
 *
 * In the fast path, we avoid doing a division in most cases (because
 * division can be very slow) by comparing the fraction part of `num`
 * with the limit, which is a slight over-estimate for the exact
 * resample threshold.
 *
 * In the slow path, we re-do the approximate test more accurately.
 * The resample threshold, called `residue`, is the remainder after
 * dividing the raw `arc4random()` limit `1 << 32` by the caller's
 * limit. We use a trick to calculate it within 32 bits:
 *
 *     (1 << 32) % limit
 * == ((1 << 32) - limit) % limit
 * ==  (uint32_t)(-limit) % limit
 *
 * The modulo is safe: we know that `limit` is strictly greater than
 * zero because of the slow-path `if()` guard.
 *
 * Unless we get one of `N = (1 << 32) - residue` valid values, we
 * reject the sample. This `N` is a multiple of `limit`, so our
 * results will be unbiased; and `N` is the largest multiple that fits
 * in 32 bits, so rejections are as rare as possible.
 *
 * There are `limit` possible values for the integer part of our
 * fixed-point number. Each one corresponds to `N/limit` possible
 * fraction parts, or `N/limit + 1` in `residue` out of the possible
 * integers. For our result to be unbiased, every possible integer
 * part must have the same number of possible valid fraction parts.
 * So, when we get the superfluous value in the `N/limit + 1` cases,
 * we need to reject and resample.
 *
 * Because of the multiplication, the possible values in the fraction
 * part are equally spaced by `limit`, with varying gaps at each end
 * of the fraction's 32-bit range. We will choose a range of size `N`
 * (a multiple of `limit`) into which valid fraction values must fall,
 * with the rest of the 32-bit range covered by the `residue`. Lemire
 * explains that exactly `N/limit` possible values spaced apart by
 * `limit` will fit into our size `N` valid range, regardless of the
 * size of the end gaps, the phase alignment of the values, or where
 * `N` fits within the 32-bit range.
 *
 * So, when a fraction value falls in the `residue` outside our valid
 * range, it is superfluous and we resample. We might need multiple
 * samples to get a result, but the probability of rejection is usually
 * very small, `residue` / (1 << 32) to be exact, which is at most 0.5.
 */
uint32_t
arc4random_uniform(uint32_t limit)
{
        uint64_t num = (uint64_t)arc4random() * (uint64_t)limit;
        if (__predict_false((uint32_t)(num) < limit)) {
		uint32_t residue = (uint32_t)(-limit) % limit;
		while ((uint32_t)(num) < residue)
			num = (uint64_t)arc4random() * (uint64_t)limit;
	}
        return ((uint32_t)(num >> 32));
}
