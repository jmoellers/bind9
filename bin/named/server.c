/*
 * Copyright (C) 1999  Internet Software Consortium.
 * 
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS" AND INTERNET SOFTWARE CONSORTIUM DISCLAIMS
 * ALL WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL INTERNET SOFTWARE
 * CONSORTIUM BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
 * PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS
 * ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
 * SOFTWARE.
 */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <isc/assertions.h>
#include <isc/error.h>
#include <isc/mem.h>
#include <isc/task.h>
#include <isc/thread.h>
#include <isc/result.h>
#include <isc/socket.h>
#include <isc/timer.h>
#include <isc/app.h>

#include <dns/types.h>
#include <dns/result.h>
#include <dns/name.h>
#include <dns/fixedname.h>
#include <dns/rdata.h>
#include <dns/rdatalist.h>
#include <dns/rdataset.h>
#include <dns/rdatasetiter.h>
#include <dns/compress.h>
#include <dns/db.h>
#include <dns/dbtable.h>
#include <dns/message.h>

#include <named/types.h>
#include <named/globals.h>
#include <named/server.h>

#if 0
#include "udpclient.h"
#include "tcpclient.h"
#include "interfacemgr.h"
#endif

static ns_dbinfo_t *		cache_dbi;
static isc_task_t *		server_task;

#if 0
static inline isc_boolean_t
CHECKRESULT(dns_result_t result, char *msg)
{
	if ((result) != DNS_R_SUCCESS) {
		printf("%s: %s\n", (msg), dns_result_totext(result));
		return (ISC_TRUE);
	}

	return (ISC_FALSE);
}

/*
 * This is in bin/tests/wire_test.c, but should be in a debugging library.
 */
extern dns_result_t
printmessage(dns_message_t *);

#define MAX_RDATASETS 25

static dns_result_t
resolve_packet(isc_mem_t *mctx, dns_message_t *query, isc_buffer_t *target) {
	dns_message_t *message;
	dns_result_t result, dbresult;
	dns_name_t *qname, *fname, *rqname;
	dns_fixedname_t foundname, frqname;
	dns_rdataset_t *rds, *rdataset, rqrds, rdatasets[MAX_RDATASETS];
	unsigned int nrdatasets = 0;
	dns_dbnode_t *node;
	dns_db_t *db;
	dns_rdatasetiter_t *rdsiter;
	dns_rdatatype_t type;
	isc_boolean_t possibly_auth = ISC_FALSE;

	message = NULL;
	result = dns_message_create(mctx, DNS_MESSAGE_INTENTRENDER, &message);
	CHECKRESULT(result, "dns_message_create failed");

	message->id = query->id;
	message->rcode = dns_rcode_noerror;
	message->flags = query->flags;
	message->flags |= DNS_MESSAGEFLAG_QR;

	result = dns_message_firstname(query, DNS_SECTION_QUESTION);
	if (result != DNS_R_SUCCESS)
		return (result);
	qname = NULL;
	dns_fixedname_init(&frqname);
	rqname = dns_fixedname_name(&frqname);
	dns_message_currentname(query, DNS_SECTION_QUESTION, &qname);
	result = dns_name_concatenate(qname, NULL, rqname, NULL);
	if (result != DNS_R_SUCCESS)
		return (DNS_R_UNEXPECTED);
	rds = ISC_LIST_HEAD(qname->list);
	if (rds == NULL)
		return (DNS_R_UNEXPECTED);
	type = rds->type;
	dns_rdataset_init(&rqrds);
	dns_rdataset_makequestion(&rqrds, rds->rdclass, rds->type);
	ISC_LIST_APPEND(rqname->list, &rqrds, link);

	dns_message_addname(message, rqname, DNS_SECTION_QUESTION);

	result = printmessage(message);
	INSIST(result == DNS_R_SUCCESS);  /* XXX not in a real server */

	/*
	 * Find a database to answer the query from.
	 */
	db = NULL;
	result = dns_dbtable_find(ns_g_dbtable, qname, &db);
	if (result != DNS_R_SUCCESS && result != DNS_R_PARTIALMATCH) {
		printf("could not find a dbtable: %s\n",
		       dns_result_totext(result));
		message->rcode = dns_rcode_servfail;
		goto render;
	}
	
	/*
	 * Now look for an answer in the database.
	 */
	dns_fixedname_init(&foundname);
	fname = dns_fixedname_name(&foundname);
	rdataset = &rdatasets[nrdatasets++];
	dns_rdataset_init(rdataset);
	node = NULL;
	dbresult = dns_db_find(db, qname, NULL, type, 0, 0, &node, fname,
			       rdataset);
	switch (dbresult) {
	case DNS_R_SUCCESS:
	case DNS_R_DNAME:
	case DNS_R_CNAME:
		possibly_auth = ISC_TRUE;
		break;
	case DNS_R_GLUE:
	case DNS_R_ZONECUT:
	case DNS_R_DELEGATION:
		break;
	case DNS_R_NXRDATASET:
		if (dns_db_iszone(db))
			message->flags |= DNS_MESSAGEFLAG_AA;
		dns_db_detachnode(db, &node);
		dns_db_detach(&db);
                goto render;
	case DNS_R_NXDOMAIN:
		if (dns_db_iszone(db))
			message->flags |= DNS_MESSAGEFLAG_AA;
		dns_db_detach(&db);
                message->rcode = dns_rcode_nxdomain;
                goto render;
	default:
		printf("%s\n", dns_result_totext(result));
		dns_db_detach(&db);
                message->rcode = dns_rcode_servfail;
                goto render;
	}

	if (dbresult == DNS_R_DELEGATION) {
		ISC_LIST_APPEND(fname->list, rdataset, link);
		dns_message_addname(message, fname, DNS_SECTION_AUTHORITY);
	} else if (type == dns_rdatatype_any) {
		rdsiter = NULL;
		result = dns_db_allrdatasets(db, node, NULL, 0, &rdsiter);
		if (result == DNS_R_SUCCESS)
			result = dns_rdatasetiter_first(rdsiter);
		while (result == DNS_R_SUCCESS) {
			dns_rdatasetiter_current(rdsiter, rdataset);
			ISC_LIST_APPEND(fname->list, rdataset, link);
			if (nrdatasets == MAX_RDATASETS) {
				result = DNS_R_NOSPACE;
			} else {
				rdataset = &rdatasets[nrdatasets++];
				dns_rdataset_init(rdataset);
				result = dns_rdatasetiter_next(rdsiter);
			}
		}
		if (result != DNS_R_NOMORE) {
			dns_db_detachnode(db, &node);
			dns_db_detach(&db);
			message->rcode = dns_rcode_servfail;
			goto render;
		}
		dns_message_addname(message, fname, DNS_SECTION_ANSWER);
	} else {
		ISC_LIST_APPEND(fname->list, rdataset, link);
		dns_message_addname(message, fname, DNS_SECTION_ANSWER);
	}

	if (dns_db_iszone(db) && possibly_auth)
		message->flags |= DNS_MESSAGEFLAG_AA;

	dns_db_detachnode(db, &node);
	dns_db_detach(&db);

 render:

	result = dns_message_renderbegin(message, target);
	if (result != DNS_R_SUCCESS)
		return (result);

	result = dns_message_rendersection(message, DNS_SECTION_QUESTION,
					   0, 0);
	if (result != DNS_R_SUCCESS)
		return (result);

	result = dns_message_rendersection(message, DNS_SECTION_ANSWER,
					   0, 0);
	if (result != DNS_R_SUCCESS)
		return (result);

	result = dns_message_rendersection(message, DNS_SECTION_AUTHORITY,
					   0, 0);
	if (result != DNS_R_SUCCESS)
		return (result);

	result = dns_message_rendersection(message, DNS_SECTION_ADDITIONAL,
					   0, 0);
	if (result != DNS_R_SUCCESS)
		return (result);

	result = dns_message_rendersection(message, DNS_SECTION_TSIG,
					   0, 0);
	if (result != DNS_R_SUCCESS)
		return (result);

	result = dns_message_renderend(message);

	dns_message_destroy(&message);

	return (DNS_R_SUCCESS);
}

/*
 * Process the wire format message given in r, and return a new packet to
 * transmit.
 *
 * Return of DNS_R_SUCCESS means r->base is a newly allocated region of
 * memory, and r->length is its length.  The actual for-transmit packet
 * begins at (r->length + reslen) to reserve (reslen) bytes at the front
 * of the packet for transmission specific details.
 */
static dns_result_t
dispatch(isc_mem_t *mctx, isc_region_t *rxr, unsigned int reslen)
{
	char t[512];
	isc_buffer_t source;
	isc_buffer_t target;
	dns_result_t result;
	isc_region_t txr;
	dns_message_t *message;

	/*
	 * Set up the input buffer from the contents of the region passed
	 * to us.
	 */
	isc_buffer_init(&source, rxr->base, rxr->length,
			ISC_BUFFERTYPE_BINARY);
	isc_buffer_add(&source, rxr->length);

	message = NULL;
	result = dns_message_create(mctx, DNS_MESSAGE_INTENTPARSE, &message);
	if (CHECKRESULT(result, "dns_message_create failed")) {
		return (result);
	}

	result = dns_message_parse(message, &source);
	if (CHECKRESULT(result, "dns_message_parsed failed")) {
		dns_message_destroy(&message);
		return (result);
	}
	CHECKRESULT(result, "dns_message_parse failed");

	result = printmessage(message);
	if (CHECKRESULT(result, "printmessage failed")) {
		dns_message_destroy(&message);
		return (result);
	}

	isc_buffer_init(&target, t, sizeof(t), ISC_BUFFERTYPE_BINARY);
	result = resolve_packet(mctx, message, &target);
	if (result != DNS_R_SUCCESS) {
		dns_message_destroy(&message);
		return (result);
	}

	/*
	 * Copy the reply out, adjusting for reslen
	 */
	isc_buffer_used(&target, &txr);
	txr.base = isc_mem_get(mctx, txr.length + reslen);
	if (txr.base == NULL) {
		dns_message_destroy(&message);

		return (DNS_R_NOMEMORY);
	}

	memcpy(txr.base + reslen, t, txr.length);
	rxr->base = txr.base;
	rxr->length = txr.length + reslen;

	printf("Base == %p, length == %u\n", txr.base, txr.length);
	fflush(stdout);

	if (want_stats)
		isc_mem_stats(mctx, stdout);

	dns_message_destroy(&message);

	return (DNS_R_SUCCESS);
}
#endif

static dns_result_t
load(ns_dbinfo_t *dbi) {
	dns_fixedname_t forigin;
	dns_name_t *origin;
	dns_result_t result;
	isc_buffer_t source;
	size_t len;

	len = strlen(dbi->origin);
	isc_buffer_init(&source, dbi->origin, len, ISC_BUFFERTYPE_TEXT);
	isc_buffer_add(&source, len);
	dns_fixedname_init(&forigin);
	origin = dns_fixedname_name(&forigin);
	result = dns_name_fromtext(origin, &source, dns_rootname, ISC_FALSE,
				   NULL);
	if (result != DNS_R_SUCCESS)
		return (result);

	result = dns_db_create(ns_g_mctx, "rbt", origin, dbi->iscache,
			       dns_rdataclass_in, 0, NULL, &dbi->db);
	if (result != DNS_R_SUCCESS) {
		isc_mem_put(ns_g_mctx, dbi, sizeof *dbi);
		return (result);
	}

	printf("loading %s (%s)\n", dbi->path, dbi->origin);
	result = dns_db_load(dbi->db, dbi->path);
	if (result != DNS_R_SUCCESS) {
		dns_db_detach(&dbi->db);
		isc_mem_put(ns_g_mctx, dbi, sizeof *dbi);
		return (result);
	}
	printf("loaded\n");

	if (dbi->iscache) {
		INSIST(cache_dbi == NULL);
		dns_dbtable_adddefault(ns_g_dbtable, dbi->db);
		cache_dbi = dbi;
	} else {
		if (dns_dbtable_add(ns_g_dbtable, dbi->db) != DNS_R_SUCCESS) {
			dns_db_detach(&dbi->db);
			isc_mem_put(ns_g_mctx, dbi, sizeof *dbi);
			return (result);
		}
	}

	return (DNS_R_SUCCESS);
}

static isc_result_t
load_all(void) {
	isc_result_t result = ISC_R_SUCCESS;
	ns_dbinfo_t *dbi;
	
	for (dbi = ISC_LIST_HEAD(ns_g_dbs);
	     dbi != NULL;
	     dbi = ISC_LIST_NEXT(dbi, link)) {
		result = load(dbi);
		if (result != ISC_R_SUCCESS)
			break;
	}

	return (result);
}

static void
unload_all(void) {
	ns_dbinfo_t *dbi, *dbi_next;
	
	for (dbi = ISC_LIST_HEAD(ns_g_dbs); dbi != NULL; dbi = dbi_next) {
		dbi_next = ISC_LIST_NEXT(dbi, link);
		if (dns_db_iszone(dbi->db))
			dns_dbtable_remove(ns_g_dbtable, dbi->db);
		else {
			INSIST(dbi == cache_dbi);
			dns_dbtable_removedefault(ns_g_dbtable);
			cache_dbi = NULL;
		}
		dns_db_detach(&dbi->db);
		ISC_LIST_UNLINK(ns_g_dbs, dbi, link);
		isc_mem_put(ns_g_mctx, dbi, sizeof *dbi);
	}
}

static void
load_configuration(void) {
	isc_result_t result;

	/* 
	 * XXXRTH  loading code below is temporary; it
	 * will be replaced by proper config file processing.
	 */

	result = load_all();
	if (result != ISC_R_SUCCESS) {
		/* XXXRTH */
		printf("load_all(): %s\n", isc_result_totext(result));
	}

	ns_interfacemgr_scan(ns_g_interfacemgr);
}

static void
run_server(isc_task_t *task, isc_event_t *event) {

	(void)task;
	printf("server running\n");

	load_configuration();

	isc_event_free(&event);
}

static void
shutdown_server(isc_task_t *task, isc_event_t *event) {
	(void)task;
	printf("server shutting down\n");
	unload_all();
	dns_dbtable_detach(&ns_g_dbtable);
	isc_task_detach(&server_task);
	isc_event_free(&event);
}

isc_result_t
ns_server_init(void) {
	isc_result_t result;

	result = dns_dbtable_create(ns_g_mctx, dns_rdataclass_in,
				    &ns_g_dbtable);
	if (result != ISC_R_SUCCESS)
		return (result);

	result = isc_task_create(ns_g_taskmgr, ns_g_mctx, 0, &server_task);
	if (result != ISC_R_SUCCESS)
		goto cleanup_dbtable;

	result = isc_task_onshutdown(server_task, shutdown_server, NULL);
	if (result != ISC_R_SUCCESS)
		goto cleanup_task;

	result = isc_app_onrun(ns_g_mctx, server_task, run_server, NULL);
	if (result != ISC_R_SUCCESS)
		goto cleanup_task;

	return (ISC_R_SUCCESS);

 cleanup_task:
	isc_task_detach(&server_task);

 cleanup_dbtable:
	dns_dbtable_detach(&ns_g_dbtable);

	return (result);
}
