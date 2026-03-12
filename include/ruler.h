#ifndef _RULER_H_
#define _RULER_H_ 1
#if defined(__cplusplus)
extern "C" {
#endif

/*
 * Ruler protocol (see ruler_spec.md).
 *
 * Clients write a request to the 9P file "query" (one attribute per line,
 * newline-separated), then read the response (newline-separated key=value
 * lines). All four request attributes are required.
 *
 * Wire format:
 *   Request:  one attribute per line, "name=value\n"; order does not matter.
 *   Response: zero or more lines, "key=value\n"; first '=' on a line
 *             separates key from value (value may contain additional '=').
 *   One request/response pair per fid: open, write request, read response, close.
 */

/* Canonical request attribute names (all four required) */
#define RulerQuery	"query"		/* subject to match against rules (e.g. absolute file path) */
#define RulerClient	"client"	/* client id, e.g. "acme", "sam" */
#define RulerEvent	"event"		/* event name, e.g. "open", "save" */
#define RulerId		"id"		/* identity of calling component (e.g. window id); not used for matching */

/* 9P file name for request/response */
#define RulerQueryFile	"query"

/*
 * Optional: struct to hold request attributes before formatting for the wire.
 * Clients and the daemon can use this to stay in sync; ruler does not
 * require it. All fields must be set (all four attributes required).
 */
typedef struct Rulerreq Rulerreq;
struct Rulerreq {
	char	*query;
	char	*client;
	char	*event;
	char	*id;
};

#if defined(__cplusplus)
}
#endif
#endif
