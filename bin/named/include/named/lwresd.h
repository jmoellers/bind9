/*
 * Copyright (C) 2000  Internet Software Consortium.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND INTERNET SOFTWARE CONSORTIUM
 * DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL
 * INTERNET SOFTWARE CONSORTIUM BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING
 * FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
 * NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION
 * WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/* $Id: lwresd.h,v 1.7 2000/10/04 23:19:01 bwelling Exp $ */

#ifndef NAMED_LWRESD_H
#define NAMED_LWRESD_H 1

#include <isc/types.h>
#include <isc/sockaddr.h>

#include <dns/confctx.h>
#include <dns/types.h>

struct ns_lwresd {
	isc_uint32_t magic;

	isc_mutex_t lock;
	ISC_LIST(ns_lwdclientmgr_t) cmgrs;
	isc_socket_t *sock;
	dns_view_t *view;
	isc_mem_t *mctx;
	isc_boolean_t shutting_down;
};

/*
 * Configure lwresd.
 */
isc_result_t
ns_lwresd_configure(isc_mem_t *mctx, dns_c_ctx_t *cctx);

/*
 * Create a configuration context based on resolv.conf and default parameters.
 */
isc_result_t
ns_lwresd_parseresolvconf(isc_mem_t *mctx, dns_c_ctx_t **ctxp);

/*
 * Trigger shutdown.
 */
void
ns_lwresd_shutdown(void);

/*
 * INTERNAL FUNCTIONS.
 */
void
ns__lwresd_destroy(ns_lwresd_t *lwresdp);

void *
ns__lwresd_memalloc(void *arg, size_t size);

void
ns__lwresd_memfree(void *arg, void *mem, size_t size);

#endif /* NAMED_LWRESD_H */
