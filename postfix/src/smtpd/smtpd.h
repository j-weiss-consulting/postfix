/*++
/* NAME
/*	smtpd 3h
/* SUMMARY
/*	smtp server
/* SYNOPSIS
/*	include "smtpd.h"
/* DESCRIPTION
/* .nf

 /*
  * System library.
  */
#include <unistd.h>

 /*
  * SASL library.
  */
#ifdef USE_SASL_AUTH
#include <sasl.h>
#include <saslutil.h>
#endif

 /*
  * Utility library.
  */
#include <vstream.h>
#include <vstring.h>
#include <argv.h>

 /*
  * Global library.
  */
#include <mail_stream.h>

 /*
  * Variables that keep track of conversation state. There is only one SMTP
  * conversation at a time, so the state variables can be made global. And
  * some of this has to be global anyway, so that the run-time error handler
  * can clean up in case of a fatal error deep down in some library routine.
  */
typedef struct SMTPD_DEFER {
    int     active;			/* is this active */
    VSTRING *reason;			/* reason for deferral */
    int     class;			/* error notification class */
} SMTPD_DEFER;

 /*
  * Actions that affect all recipients of a given message. That is, we don't
  * "undo" results from one recipient when evaluating the next recipient.
  */
typedef struct SMTPD_MSG_ACTION {
    int     flags;			/* see below */
    char   *filter;			/* filter destination */
    char   *redirect;			/* redirect destination */
} SMTPD_MSG_ACTION;

#define SMTPD_MSG_ACT_HOLD	(1<<0)	/* place message on hold */
#define SMTPD_MSG_ACT_DISCARD	(1<<1)	/* discard message */
#define SMTPD_MSG_ACT_FILTER	(1<<2)	/* filter message */
#define SMTPD_MSG_ACT_REDIRECT	(1<<3)	/* redirect message */

 /*
  * Short-hand for when to stop searching restriction lists.
  */
#define SMTPD_MSG_ACT_FINAL		(SMTPD_MSG_ACT_DISCARD)

#define SMTPD_MSG_ACT_ZERO(a) do { \
	(a).flags = 0; \
	(a).filter = 0; \
	(a).redirect = 0; \
    } while (0)

#define SMTPD_MSG_ACT_FREE(a) do { \
	if ((a).filter) myfree((a).filter); \
	if ((a).redirect) myfree((a).redirect); \
    } while (0)

#define SMTPD_MSG_ACT_COPY(d, s) do { \
	SMTPD_MSG_ACT_FREE(d); \
	(d).filter = ((s).filter ? mystrdup((s).filter) : 0); \
	(d).redirect = ((s).redirect ? mystrdup((s).redirect) : 0); \
    } while (0)

typedef struct SMTPD_STATE {
    int     err;
    VSTREAM *client;
    VSTRING *buffer;
    time_t  time;
    char   *name;
    char   *addr;
    char   *namaddr;
    int     peer_code;			/* 2=ok, 4=soft, 5=hard */
    int     error_count;
    int     error_mask;
    int     notify_mask;
    char   *helo_name;
    char   *queue_id;
    VSTREAM *cleanup;
    MAIL_STREAM *dest;
    int     rcpt_count;
    char   *access_denied;
    ARGV   *history;
    char   *reason;
    char   *sender;
    char   *recipient;
    char   *etrn_name;
    char   *protocol;
    char   *where;
    int     recursion;
    off_t   msg_size;
    int     junk_cmds;
#ifdef USE_SASL_AUTH
#if SASL_VERSION_MAJOR >= 2
    const char *sasl_mechanism_list;
#else
    char   *sasl_mechanism_list;
#endif
    char   *sasl_method;
    char   *sasl_username;
    char   *sasl_sender;
    sasl_conn_t *sasl_conn;
    VSTRING *sasl_encoded;
    VSTRING *sasl_decoded;
#endif
    SMTPD_MSG_ACTION *action;		/* action from access map */
    SMTPD_MSG_ACTION action_client;	/* action after connect */
    SMTPD_MSG_ACTION action_helo;	/* action after helo/ehlo */
    SMTPD_MSG_ACTION action_mailrcpt;	/* action after mail from/rcpt to */
    int     rcptmap_checked;
    int     warn_if_reject;		/* force reject into warning */
    SMTPD_DEFER defer_if_reject;	/* force reject into deferral */
    SMTPD_DEFER defer_if_permit;	/* force permit into deferral */
    int     defer_if_permit_client;	/* force permit into warning */
    int     defer_if_permit_helo;	/* force permit into warning */
    int     defer_if_permit_sender;	/* force permit into warning */
    VSTRING *expand_buf;		/* scratch space for $name expansion */
} SMTPD_STATE;

#define SMTPD_MSG_ACT_FLAGS(s) \
    ((s)->action_client.flags | (s)->action_helo.flags \
	| (s)->action_mailrcpt.flags)

#define SMTPD_MSG_ACT_VALUE(s,m) \
    ((s)->action_mailrcpt.m ? (s)->action_mailrcpt.m : \
	(s)->action_helo.m ? (s)->action_helo.m : \
	    (s)->action_client.m)

extern void smtpd_state_init(SMTPD_STATE *, VSTREAM *);
extern void smtpd_state_reset(SMTPD_STATE *);

 /*
  * Conversation stages.  This is used for "lost connection after XXX"
  * diagnostics.
  */
#define SMTPD_AFTER_CONNECT	"CONNECT"
#define SMTPD_AFTER_DOT		"END-OF-MESSAGE"

 /*
  * If running in stand-alone mode, do not try to talk to Postfix daemons but
  * write to queue file instead.
  */
#define SMTPD_STAND_ALONE(state) \
	(state->client == VSTREAM_IN && getuid() != var_owner_uid)

 /*
  * SMPTD peer information lookup.
  */
void    smtpd_peer_init(SMTPD_STATE *state);
void    smtpd_peer_reset(SMTPD_STATE *state);

/* LICENSE
/* .ad
/* .fi
/*	The Secure Mailer license must be distributed with this software.
/* AUTHOR(S)
/*	Wietse Venema
/*	IBM T.J. Watson Research
/*	P.O. Box 704
/*	Yorktown Heights, NY 10598, USA
/*--*/
