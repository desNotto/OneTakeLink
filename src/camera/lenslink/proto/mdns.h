/*
 * Originally from LensLink (https://github.com/MyNamesEMurray/LensLink),
 * obs-plugin/src/mdns.h, licensed GPL-2.0-or-later.
 * Modified for OneTakeLink (2026): extern "C" guards for C++ callers.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

/*
 * Minimal one-shot mDNS-SD browser: finds LensLink phones on the LAN so
 * the user picks a name instead of typing an IP. See mdns.c for the
 * design constraints (no port-5353 bind, no multicast membership).
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct mdns_result {
	char name[64]; /* service instance label, e.g. "Emma's iPhone" */
	char host[64]; /* IPv4 of the responder (the phone), dotted quad */
};

/* Blocking browse for `service_type` (e.g. "_lenslink._tcp.local") lasting
 * about `timeout_ms`. Fills up to `max_results` unique hosts; returns the
 * number found. Safe to call from any thread. */
int mdns_browse(const char *service_type, int timeout_ms,
		struct mdns_result *results, int max_results);

#ifdef __cplusplus
}
#endif
