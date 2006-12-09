/*++
/* NAME
/*	msg_output 3
/* SUMMARY
/*	diagnostics output management
/* SYNOPSIS
/*	#include <msg_output.h>
/*
/*	typedef void (*MSG_OUTPUT_FN)(int level, char *text)
/*
/*	void	msg_output(output_fn)
/*	MSG_OUTPUT_FN output_fn;
/*
/*	void	msg_printf(level, format, ...)
/*	int	level;
/*	const char *format;
/*
/*	void	msg_vprintf(level, format, ap)
/*	int	level;
/*	const char *format;
/*	va_list ap;
/*
/*	void	msg_text(level, text)
/*	int	level;
/*	const char *text;
/* DESCRIPTION
/*	This module implements low-level output management for the
/*	msg(3) diagnostics interface. The output routines are
/*	protected against ordinary recursive calls and against
/*	re-entry by a signal handler.
/*
/*	Protection against re-entry by a signal handler requires
/*	that:
/* .IP \(bu
/*	The signal handler never returns.
/* .IP \(bu
/*	The signal handler does not execute before msg_output()
/*	completes initialization.
/* .IP \(bu
/*	Each msg_output() call-back function, and each Postfix or
/*	system function called by that call-back function, either
/*	protects itself against recursive calls and re-entry by a
/*	terminating signal handler, or is called exclusively by
/*	functions in the msg_output(3) module.
/* .PP
/*	When re-entrancy is detected, the requested output operation
/*	is skipped. This prevents deadlock on Linux releases that
/*	use mutexes within system library routines such as syslog().
/*
/*	msg_output() registers an output handler for the diagnostics
/*	interface. An application can register multiple output handlers.
/*	Output handlers are called in the specified order.
/*	An output handler takes as arguments a severity level (MSG_INFO,
/*	MSG_WARN, MSG_ERROR, MSG_FATAL, MSG_PANIC, monotonically increasing
/*	integer values ranging from 0 to MSG_LAST) and pre-formatted,
/*	sanitized, text in the form of a null-terminated string.
/*
/*	msg_printf() and msg_vprintf() format their arguments, sanitize the
/*	result, and call the output handlers registered with msg_output().
/*
/*	msg_text() copies a pre-formatted text, sanitizes the result, and
/*	calls the output handlers registered with msg_output().
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

/* System library. */

#include <sys_defs.h>
#include <stdarg.h>
#include <errno.h>

/* Utility library. */

#include <mymalloc.h>
#include <vstring.h>
#include <vstream.h>
#include <msg_vstream.h>
#include <stringops.h>
#include <percentm.h>
#include <msg_output.h>

 /*
  * Global scope, to discourage the compiler from doing smart things.
  */
volatile int msg_vp_lock;
volatile int msg_txt_lock;

 /*
  * Private state.
  */
static MSG_OUTPUT_FN *msg_output_fn = 0;
static int msg_output_fn_count = 0;
static VSTRING *msg_buffer = 0;

/* msg_output - specify output handler */

void    msg_output(MSG_OUTPUT_FN output_fn)
{

    /*
     * Allocate all resources during initialization.
     */
    if (msg_buffer == 0)
	msg_buffer = vstring_alloc(100);

    /*
     * We're not doing this often, so avoid complexity and allocate memory
     * for an exact fit.
     */
    if (msg_output_fn_count == 0)
	msg_output_fn = (MSG_OUTPUT_FN *) mymalloc(sizeof(*msg_output_fn));
    else
	msg_output_fn = (MSG_OUTPUT_FN *) myrealloc((char *) msg_output_fn,
			(msg_output_fn_count + 1) * sizeof(*msg_output_fn));
    msg_output_fn[msg_output_fn_count++] = output_fn;
}

/* msg_printf - format text and log it */

void    msg_printf(int level, const char *format,...)
{
    va_list ap;

    va_start(ap, format);
    msg_vprintf(level, format, ap);
    va_end(ap);
}

/* msg_vprintf - format text and log it */

void    msg_vprintf(int level, const char *format, va_list ap)
{
    BEGIN_PROTECT_AGAINST_RECURSION_OR_TERMINATING_SIGNAL_HANDLER(msg_vp_lock);
    vstring_vsprintf(msg_buffer, percentm(format, errno), ap);
    msg_text(level, vstring_str(msg_buffer));
    END_PROTECT_AGAINST_RECURSION_OR_TERMINATING_SIGNAL_HANDLER(msg_vp_lock);
}

/* msg_text - sanitize and log pre-formatted text */

void    msg_text(int level, const char *text)
{
    int     i;

    /*
     * Sanitize the text. Use a private copy if necessary.
     */
    BEGIN_PROTECT_AGAINST_RECURSION_OR_TERMINATING_SIGNAL_HANDLER(msg_txt_lock);
    if (text != vstring_str(msg_buffer))
	vstring_strcpy(msg_buffer, text);
    printable(vstring_str(msg_buffer), '?');
    /* On-the-fly initialization for debugging test programs only. */
    if (msg_output_fn_count == 0)
	msg_vstream_init("unknown", VSTREAM_ERR);
    for (i = 0; i < msg_output_fn_count; i++)
	msg_output_fn[i] (level, vstring_str(msg_buffer));
    END_PROTECT_AGAINST_RECURSION_OR_TERMINATING_SIGNAL_HANDLER(msg_txt_lock);
}
