/*
 * OneTakeLink: tiny replacements for the libobs utilities the original
 * LensLink sources used (os_gettime_ns, astrcmpi, bmalloc/bfree), so the
 * ported protocol code builds without libobs.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <stdint.h>
#include <stdlib.h>
#include <ctype.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <time.h>
#endif

/* Monotonic clock in nanoseconds. */
static inline uint64_t ll_gettime_ns(void)
{
#ifdef _WIN32
	static LARGE_INTEGER freq;
	LARGE_INTEGER now;
	if (!freq.QuadPart)
		QueryPerformanceFrequency(&freq);
	QueryPerformanceCounter(&now);
	return (uint64_t)((double)now.QuadPart * 1e9 / (double)freq.QuadPart);
#else
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
#endif
}

/* ASCII case-insensitive compare (DNS names). */
static inline int ll_strcmpi(const char *a, const char *b)
{
	for (;; a++, b++) {
		int ca = tolower((unsigned char)*a);
		int cb = tolower((unsigned char)*b);
		if (ca != cb || !ca)
			return ca - cb;
	}
}
