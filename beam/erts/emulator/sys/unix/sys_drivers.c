/*
 * %CopyrightBegin%
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Copyright Ericsson AB 1996-2026. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * %CopyrightEnd%
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#ifdef ISC32
#define _POSIX_SOURCE
#define _XOPEN_SOURCE
#endif

#include <sys/times.h>		/* ! */
#include <time.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <termios.h>
#include <ctype.h>
#include <sys/utsname.h>
#include <sys/select.h>
#include <arpa/inet.h>

#ifdef ISC32
#include <sys/bsdtypes.h>
#endif

#include <termios.h>
#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif
#ifdef HAVE_SYS_IOCTL_H
#include <sys/ioctl.h>
#endif

#include <sys/types.h>
#include <sys/socket.h>

#define WANT_NONBLOCKING    /* must define this to pull in defs from sys.h */
#include "sys.h"
#include "erl_osenv.h"

#include "erl_threads.h"

extern erts_atomic_t sys_misc_mem_sz;


#define MAX_VSIZE 16		/* Max number of entries allowed in an I/O
				 * vector sock_sendv().
				 */
#include "global.h"
#include "erl_sys_driver.h"

#if defined IOV_MAX
#define MAXIOV IOV_MAX
#elif defined UIO_MAXIOV
#define MAXIOV UIO_MAXIOV
#else
#define MAXIOV 16
#endif

/* Used by the fd driver iff the fd could not be set to non-blocking */
typedef struct ErtsSysBlocking_ {
    ErlDrvPDL pdl;
    ErlDrvSSizeT res;
    int err;
    unsigned int pkey;
} ErtsSysBlocking;

typedef struct fd_data {
    int   fd;
    char  pbuf[4];   /* hold partial packet bytes */
    int   psz;       /* size of pbuf */
    char  *buf;
    char  *cpos;
    int   sz;
    int   remain;  /* for input on fd */
} ErtsSysFdData;

typedef struct driver_data {
    ErlDrvPort port_num;
    ErtsSysFdData *ofd;
    ErtsSysFdData *ifd;
    int packet_bytes;
    int terminating;
    ErtsSysBlocking *blocking;
    int busy;
    ErlDrvSizeT high_watermark;
    ErlDrvSizeT low_watermark;
} ErtsSysDriverData;

#ifdef DEBUG
#define close(fd) do { int res = close(fd); ASSERT(res > -1); } while(0)
#endif

// #define HARD_DEBUG
#ifdef HARD_DEBUG
#define driver_select(port_num, fd, flags, onoff)                       \
    do {                                                                \
        if (((flags) & ERL_DRV_READ) && onoff)                          \
            fprintf(stderr,"%010d %p: read select %d\r\n", __LINE__, port_num, (int)fd); \
        if (((flags) & ERL_DRV_WRITE) && onoff)                         \
            fprintf(stderr,"%010d %p: writ select %d\r\n", __LINE__, port_num, (int)fd); \
        if (((flags) & ERL_DRV_READ) && !onoff)                          \
            fprintf(stderr,"%010d %p: read unsele %d\r\n", __LINE__, port_num, (int)fd); \
        if (((flags) & ERL_DRV_WRITE) && !onoff)                         \
            fprintf(stderr,"%010d %p: writ unsele %d\r\n", __LINE__, port_num, (int)fd); \
        driver_select_nkp(port_num, fd, flags, onoff);                  \
    } while(0)
#endif

/*
 * Decreasing the size of it below 16384 is not allowed.
 */

#define ERTS_SYS_READ_BUF_SZ (64*1024)

/* II. Prototypes */

/* II.III FD prototypes */
static ErlDrvData fd_start(ErlDrvPort, char*, SysDriverOpts*);
static void fd_async(void *);
static void fd_ready_async(ErlDrvData drv_data, ErlDrvThreadData thread_data);
static ErlDrvSSizeT fd_control(ErlDrvData, unsigned int, char *, ErlDrvSizeT,
			       char **, ErlDrvSizeT);
static void fd_stop(ErlDrvData);
static void fd_flush(ErlDrvData);

/* II.IV Common prototypes */
static void ready_input(ErlDrvData, ErlDrvEvent);
static void ready_output(ErlDrvData, ErlDrvEvent);
static void output(ErlDrvData, char*, ErlDrvSizeT);
static void outputv(ErlDrvData, ErlIOVec*);
static void stop_select(ErlDrvEvent, void*);

/* III Driver entries */

/* III.II The fd driver */
struct erl_drv_entry fd_driver_entry = {
    NULL,
    fd_start,
    fd_stop,
    output,
    ready_input,
    ready_output,
    "fd",
    NULL,
    NULL,
    fd_control,
    NULL,
    outputv,
    fd_ready_async, /* ready_async */
    fd_flush, /* flush */
    NULL, /* call */
    NULL, /* event */
    ERL_DRV_EXTENDED_MARKER,
    ERL_DRV_EXTENDED_MAJOR_VERSION,
    ERL_DRV_EXTENDED_MINOR_VERSION,
    0, /* ERL_DRV_FLAGs */
    NULL, /* handle2 */
    NULL, /* process_exit */
    stop_select
};

/* Untility functions */

static int set_blocking_data(ErtsSysDriverData *dd) {

    dd->blocking = erts_alloc(ERTS_ALC_T_SYS_BLOCKING, sizeof(ErtsSysBlocking));

    erts_atomic_add_nob(&sys_misc_mem_sz, sizeof(ErtsSysBlocking));

    dd->blocking->pdl = driver_pdl_create(dd->port_num);
    dd->blocking->res = 0;
    dd->blocking->err = 0;
    dd->blocking->pkey = driver_async_port_key(dd->port_num);

    return 1;
}

static void init_fd_data(ErtsSysFdData *fd_data, int fd)
{
    fd_data->fd = fd;
    fd_data->buf = NULL;
    fd_data->cpos = NULL;
    fd_data->remain = 0;
    fd_data->sz = 0;
    fd_data->psz = 0;
}

static ErtsSysDriverData *
create_driver_data(ErlDrvPort port_num,
                   int ifd,
                   int ofd,
                   int packet_bytes,
                   int read_write,
                   int is_blocking,
                   SysDriverOpts* opts)
{
    Port *prt;
    ErtsSysDriverData *driver_data;
    char *data;
    int size = sizeof(ErtsSysDriverData);

    if (read_write & DO_READ)
        size += sizeof(ErtsSysFdData);

    if ((read_write & DO_WRITE) &&
        ((ifd != ofd || ofd == -1) || !(read_write & DO_READ)))
        size += sizeof(ErtsSysFdData);

    data = erts_alloc(ERTS_ALC_T_DRV_TAB,size);
    erts_atomic_add_nob(&sys_misc_mem_sz, size);

    driver_data = (ErtsSysDriverData*)data;
    data += sizeof(*driver_data);

    prt = erts_drvport2port(port_num);
    if (prt != ERTS_INVALID_ERL_DRV_PORT)
	prt->os_pid = -1;

    driver_data->packet_bytes = packet_bytes;
    driver_data->port_num = port_num;
    driver_data->terminating = 0;
    driver_data->blocking = NULL;

    if (read_write & DO_READ) {
        driver_data->ifd = (ErtsSysFdData*)data;
        data += sizeof(*driver_data->ifd);
        init_fd_data(driver_data->ifd, ifd);
        driver_select(port_num, ifd, (ERL_DRV_READ|ERL_DRV_USE), 1);
    } else {
        driver_data->ifd = NULL;
    }

    if (read_write & DO_WRITE) {
        if (ofd != -1 && ifd == ofd && read_write & DO_READ) {
            /* This is for when ifd and ofd are the same fd */
            driver_data->ofd = driver_data->ifd;
        } else {
            driver_data->ofd = (ErtsSysFdData*)data;
            data += sizeof(*driver_data->ofd);
            init_fd_data(driver_data->ofd, ofd);
        }
        if (is_blocking)
            if (!set_blocking_data(driver_data)) {
                erts_free(ERTS_ALC_T_DRV_TAB, driver_data);
                return NULL;
            }
    } else {
        driver_data->ofd = NULL;
    }

    driver_data->busy = 0;
    driver_data->high_watermark = opts->high_watermark;
    driver_data->low_watermark = opts->low_watermark;
    
    return driver_data;
}

#define FD_DEF_HEIGHT 24
#define FD_DEF_WIDTH 80
/* Control op */
#define FD_CTRL_OP_GET_WINSIZE 100

static int fd_get_window_size(int fd, Uint32 *width, Uint32 *height)
{
#ifdef TIOCGWINSZ
    struct winsize ws;
    if (ioctl(fd,TIOCGWINSZ,&ws) == 0) {
	*width = (Uint32) ws.ws_col;
	*height = (Uint32) ws.ws_row;
	return 1;
    }
#endif
    return 0;
}

static ErlDrvSSizeT fd_control(ErlDrvData drv_data,
			       unsigned int command,
			       char *buf, ErlDrvSizeT len,
			       char **rbuf, ErlDrvSizeT rlen)
{
    char resbuff[2*sizeof(Uint32)];
    ErtsSysDriverData* dd = (ErtsSysDriverData*)drv_data;
    command -= ERTS_TTYSL_DRV_CONTROL_MAGIC_NUMBER;
    switch (command) {
    case FD_CTRL_OP_GET_WINSIZE:
	{
	    Uint32 w,h;
            int success = 0;
            if (dd->ofd != NULL) {
                /* Try with output file descriptor */
                int out_fd = dd->ofd->fd;
                success = fd_get_window_size(out_fd,&w,&h);
            }
            if (!success && dd->ifd != NULL) {
                /* Try with input file descriptor */
                int in_fd = dd->ifd->fd;
                success = fd_get_window_size(in_fd,&w,&h);
            }
            if (!success) {
                return -1;
            }
            /* Succeeded */
	    memcpy(resbuff,&w,sizeof(Uint32));
	    memcpy(resbuff+sizeof(Uint32),&h,sizeof(Uint32));
	}
	break;
    default:
	return -1;
    }
    if (rlen < 2*sizeof(Uint32)) {
	*rbuf = driver_alloc(2*sizeof(Uint32));
    }
    memcpy(*rbuf,resbuff,2*sizeof(Uint32));
    return 2*sizeof(Uint32);
}

static ErlDrvData fd_start(ErlDrvPort port_num, char* name,
			   SysDriverOpts* opts)
{
    int non_blocking = 0;

    if (((opts->read_write & DO_READ) && opts->ifd >= sys_max_files()) ||
	((opts->read_write & DO_WRITE) && opts->ofd >= sys_max_files()))
	return ERL_DRV_ERROR_GENERAL;

    /*
     * Historical:
     *
     * "Note about nonblocking I/O.
     *
     * At least on Solaris, setting the write end of a TTY to nonblocking,
     * will set the input end to nonblocking as well (and vice-versa).
     * If erl is run in a pipeline like this:  cat | erl
     * the input end of the TTY will be the standard input of cat.
     * And cat is not prepared to handle nonblocking I/O."
     *
     * Actually, the reason for this is not that the tty itself gets set
     * in non-blocking mode, but that the "input end" (cat's stdin) and
     * the "output end" (erlang's stdout) are typically the "same" file
     * descriptor, dup()'ed from a single fd by one of this process'
     * ancestors.
     *
     * The workaround for this problem used to be a rather bad kludge,
     * interposing an extra process ("internal cat") between erlang's
     * stdout and the original stdout, allowing erlang to set its stdout
     * in non-blocking mode without affecting the stdin of the preceding
     * process in the pipeline - and being a kludge, it caused all kinds
     * of weird problems.
     *
     * So, this is the current logic:
     *
     * The only reason to set non-blocking mode on the output fd at all is
     * if it's something that can cause a write() to block, of course,
     * i.e. primarily if it points to a tty, socket, pipe, or fifo. 
     *
     * If we don't set non-blocking mode when we "should" have, and output
     * becomes blocked, the entire runtime system will be suspended - this
     * is normally bad of course, and can happen fairly "easily" - e.g. user
     * hits ^S on tty - but doesn't necessarily happen.
     * 
     * If we do set non-blocking mode when we "shouldn't" have, the runtime
     * system will end up seeing EOF on the input fd (due to the preceding
     * process dying), which typically will cause the entire runtime system
     * to terminate immediately (due to whatever erlang process is seeing
     * the EOF taking it as a signal to halt the system). This is *very* bad.
     * 
     * I.e. we should take a conservative approach, and only set non-
     * blocking mode when we a) need to, and b) are reasonably certain
     * that it won't be a problem. And as in the example above, the problem
     * occurs when input fd and output fd point to different "things".
     *
     * However, determining that they are not just the same "type" of
     * "thing", but actually the same instance of that type of thing, is
     * unreasonably complex in many/most cases.
     *
     * Also, with pipes, sockets, and fifos it's far from obvious that the
     * user *wants* non-blocking output: If you're running erlang inside
     * some complex pipeline, you're probably not running a real-time system
     * that must never stop, but rather *want* it to suspend if the output
     * channel is "full".
     *
     * So, the bottom line: We will only set the output fd non-blocking if
     * it points to a tty, and either a) the input fd also points to a tty,
     * or b) we can make sure that setting the output fd non-blocking
     * doesn't interfere with someone else's input, via a somewhat milder
     * kludge than the above.
     *
     * Also keep in mind that while this code is almost exclusively run as
     * a result of an erlang open_port({fd,0,1}, ...), that isn't the only
     * case - it can be called with any old pre-existing file descriptors,
     * the relations between which (if they're even two) we can only guess
     * at - still, we try our best...
     *
     * Added note OTP 18: Some systems seem to use stdout/stderr to log data
     * using unix pipes, so we cannot allow the system to block on a write.
     * Therefore we use an async thread to write the data to fd's that could
     * not be set to non-blocking. When no async threads are available we
     * fall back on the old behaviour.
     *
     * Also the guarantee about what is delivered to the OS has changed.
     * Pre 18 the fd driver did no flushing of data before terminating.
     * Now it does. This is because we want to be able to guarantee that things
     * such as escripts and friends really have outputted all data before
     * terminating. This could potentially block the termination of the system
     * for a very long time, but if the user wants to terminate fast she should
     * use erlang:halt with flush=false.
     */

    /* Try to figure out if we can use non-blocking writes */
    if (opts->read_write & DO_WRITE) {

	/* If we don't have a read end, all bets are off - no non-blocking. */
	if (opts->read_write & DO_READ) {

	    if (isatty(opts->ofd)) { /* output fd is a tty:-) */

		if (isatty(opts->ifd)) { /* input fd is also a tty */

		    /* To really do this "right", we should also check that
		       input and output fd point to the *same* tty - but
		       this seems like overkill; ttyname() isn't for free,
		       and this is a very common case - and it's hard to
		       imagine a scenario where setting non-blocking mode
		       here would cause problems - go ahead and do it. */

                    non_blocking = 1;
		    SET_NONBLOCKING(opts->ofd);

		} else {	/* output fd is a tty, input fd isn't */

		    /* This is a "problem case", but also common (see the
		       example above) - i.e. it makes sense to try a bit
		       harder before giving up on non-blocking mode: Try to
		       re-open the tty that the output fd points to, and if
		       successful replace the original one with the "new" fd
		       obtained this way, and set *that* one in non-blocking
		       mode. (Yes, this is a kludge.)

		       However, re-opening the tty may fail in a couple of
		       (unusual) cases:

		       1) The name of the tty (or an equivalent one, i.e.
			  same major/minor number) can't be found, because
			  it actually lives somewhere other than /dev (or
			  wherever ttyname() looks for it), and isn't
			  equivalent to any of those that do live in the
			  "standard" place - this should be *very* unusual.

		       2) Permissions on the tty don't allow us to open it -
			  it's perfectly possible to have an fd open to an
			  object whose permissions wouldn't allow us to open
			  it. This is not as unusual as it sounds, one case
			  is if the user has su'ed to someone else (not
			  root) - we have a read/write fd open to the tty
			  (because it has been inherited all the way down
			  here), but we have neither read nor write
			  permission for the tty.

		       In these cases, we finally give up, and don't set the
		       output fd in non-blocking mode. */

		    char *tty;
		    int nfd;

		    if ((tty = ttyname(opts->ofd)) != NULL &&
			(nfd = open(tty, O_WRONLY)) != -1) {
			dup2(nfd, opts->ofd);
			close(nfd);
                        non_blocking = 1;
			SET_NONBLOCKING(opts->ofd);
		    }
		}
	    }
	}
    }
    return (ErlDrvData)create_driver_data(port_num, opts->ifd, opts->ofd,
                                          opts->packet_bytes,
                                          opts->read_write,
                                          !non_blocking, opts);
}

static void clear_fd_data(ErtsSysFdData *fdd)
{
    if (fdd->sz > 0) {
	erts_free(ERTS_ALC_T_FD_ENTRY_BUF, (void *) fdd->buf);
	ASSERT(erts_atomic_read_nob(&sys_misc_mem_sz) >= fdd->sz);
	erts_atomic_add_nob(&sys_misc_mem_sz, -1*fdd->sz);
    }
    fdd->buf = NULL;
    fdd->sz = 0;
    fdd->remain = 0;
    fdd->cpos = NULL;
    fdd->psz = 0;
}

static void nbio_stop_fd(ErlDrvPort prt, ErtsSysFdData *fdd, int use)
{
    clear_fd_data(fdd);
    SET_BLOCKING(abs(fdd->fd));
}

static void fd_stop(ErlDrvData ev)  /* Does not close the fds */
{
    ErtsSysDriverData* dd = (ErtsSysDriverData*)ev;
    ErlDrvPort prt = dd->port_num;
    int sz = sizeof(ErtsSysDriverData);

    if (dd->blocking) {
        erts_free(ERTS_ALC_T_SYS_BLOCKING, dd->blocking);
        dd->blocking = NULL;
        sz += sizeof(ErtsSysBlocking);
    }

    if (dd->ifd) {
        sz += sizeof(ErtsSysFdData);
        driver_select(prt, abs(dd->ifd->fd), ERL_DRV_USE_NO_CALLBACK|DO_READ|DO_WRITE, 0);
        nbio_stop_fd(prt, dd->ifd, 1);
    }
    if (dd->ofd && dd->ofd != dd->ifd) {
        sz += sizeof(ErtsSysFdData);
        driver_select(prt, abs(dd->ofd->fd), ERL_DRV_USE_NO_CALLBACK|DO_WRITE, 0);
        nbio_stop_fd(prt, dd->ofd, 1);
    }

     erts_free(ERTS_ALC_T_DRV_TAB, dd);
     erts_atomic_add_nob(&sys_misc_mem_sz, -sz);
}

static void fd_flush(ErlDrvData ev)
{
    ErtsSysDriverData* dd = (ErtsSysDriverData*)ev;
    if (!dd->terminating)
        dd->terminating = 1;
}

/* Note that driver_data[fd].ifd == fd if the port was opened for reading, */
/* otherwise (i.e. write only) driver_data[fd].ofd = fd.  */

/* used by fd_driver */
static void outputv(ErlDrvData e, ErlIOVec* ev)
{
    ErtsSysDriverData *dd = (ErtsSysDriverData*)e;
    ErlDrvPort ix = dd->port_num;
    int pb = dd->packet_bytes;
    int ofd = dd->ofd ? dd->ofd->fd : -1;
    ssize_t n;
    char lb[4];
    char* lbp;
    ErlDrvSizeT len = ev->size;
    ErlDrvSizeT qsz;

    /* (len > ((unsigned long)-1 >> (4-pb)*8)) */
    /*    if (pb >= 0 && (len & (((ErlDrvSizeT)1 << (pb*8))) - 1) != len) {*/
    if (((pb == 2) && (len > 0xffff)) || (pb == 1 && len > 0xff)) {
	driver_failure_posix(ix, EINVAL);
	return; /* -1; */
    }
    /* Handles 0 <= pb <= 4 only */
    put_int32((Uint32) len, lb);
    lbp = lb + (4-pb);

    ev->iov[0].iov_base = lbp;
    ev->iov[0].iov_len = pb;
    ev->size += pb;

    if (dd->blocking)
        driver_pdl_lock(dd->blocking->pdl);

    qsz = driver_sizeq(ix);
    if (qsz) {
        if (qsz == (ErlDrvSizeT) -1) {
            if (dd->blocking)
                driver_pdl_unlock(dd->blocking->pdl);
            driver_failure_posix(ix, EINVAL);
            return;
        }
        driver_enqv(ix, ev, 0);
        qsz += ev->size;
        if (!dd->busy && qsz >= dd->high_watermark)
            set_busy_port(ix, (dd->busy = !0));
        if (dd->blocking)
            driver_pdl_unlock(dd->blocking->pdl);
    }
    else if (!dd->blocking) {
        /* We try to write directly if the fd in non-blocking */
	int vsize = ev->vsize > MAX_VSIZE ? MAX_VSIZE : ev->vsize;

	n = writev(ofd, (const void *) (ev->iov), vsize);
	if (n == ev->size)
	    return; /* 0;*/
	if (n < 0) {
	    if ((errno != EINTR) && (errno != ERRNO_BLOCK)) {
		driver_failure_posix(ix, errno);
		return; /* -1;*/
	    }
	    n = 0;
	}
	driver_enqv(ix, ev, n);  /* n is the skip value */
        qsz = ev->size - n;
        if (!dd->busy && qsz >= dd->high_watermark)
            set_busy_port(ix, (dd->busy = !0));
	driver_select(ix, ofd, ERL_DRV_WRITE|ERL_DRV_USE, 1);
    }
    else {
        if (ev->size != 0) {
            driver_enqv(ix, ev, 0);
            qsz = ev->size;
            if (!dd->busy && qsz >= dd->high_watermark)
                set_busy_port(ix, (dd->busy = !0));
            driver_pdl_unlock(dd->blocking->pdl);
            driver_async(ix, &dd->blocking->pkey,
                         fd_async, dd, NULL);
        } else {
            driver_pdl_unlock(dd->blocking->pdl);
        }
    }

    /* return 0;*/
}

/* Used by spawn_driver */
static void output(ErlDrvData e, char* buf, ErlDrvSizeT len)
{
    ErtsSysDriverData *dd = (ErtsSysDriverData*)e;
    ErlDrvPort ix = dd->port_num;
    int pb = dd->packet_bytes;
    int ofd = dd->ofd ? dd->ofd->fd : -1;
    ssize_t n;
    ErlDrvSizeT qsz;
    char lb[4];
    char* lbp;
    struct iovec iv[2];

    /* (len > ((unsigned long)-1 >> (4-pb)*8)) */
    if (((pb == 2) && (len > 0xffff))
        || (pb == 1 && len > 0xff)
) {
	driver_failure_posix(ix, EINVAL);
	return; /* -1; */
    }
    put_int32(len, lb);
    lbp = lb + (4-pb);

    qsz = driver_sizeq(ix);
    if (qsz) {
        if (qsz == (ErlDrvSizeT) -1) {
            driver_failure_posix(ix, EINVAL);
            return;
        }
	driver_enq(ix, lbp, pb);
	driver_enq(ix, buf, len);
        qsz += len + pb;
    }
    else {
	iv[0].iov_base = lbp;
	iv[0].iov_len = pb;  /* should work for pb=0 */
	iv[1].iov_base = buf;
	iv[1].iov_len = len;
	n = writev(ofd, iv, 2);
	if (n == pb+len)
	    return; /* 0; */
	if (n < 0) {
	    if ((errno != EINTR) && (errno != ERRNO_BLOCK)) {
		driver_failure_posix(ix, errno);
		return; /* -1; */
	    }
	    n = 0;
	}
        qsz = pb + len - n;
	if (n < pb) {
	    driver_enq(ix, lbp+n, pb-n);
	    driver_enq(ix, buf, len);
	}
	else {
	    n -= pb;
	    driver_enq(ix, buf+n, len-n);
	}
	driver_select(ix, ofd, ERL_DRV_WRITE|ERL_DRV_USE, 1);
    }

    if (!dd->busy && qsz >= dd->high_watermark)
        set_busy_port(ix, (dd->busy = !0));

    return; /* 0; */
}

static int port_inp_failure(ErtsSysDriverData *dd, int res)
				/* Result: 0 (eof) or -1 (error) */
{
    int err = errno;

    ASSERT(res <= 0);
    if (dd->ifd) {
        driver_select(dd->port_num, dd->ifd->fd, ERL_DRV_READ|ERL_DRV_WRITE, 0);
        clear_fd_data(dd->ifd);
    }

    if (dd->blocking) {
        driver_pdl_lock(dd->blocking->pdl);
        if (driver_sizeq(dd->port_num) > 0) {
            driver_pdl_unlock(dd->blocking->pdl);
            /* We have stuff in the output queue, so we just
               set the state to terminating and wait for fd_async_ready
               to terminate the port */
            if (res == 0)
                dd->terminating = 2;
            else
                dd->terminating = -err;
            return 0;
        }
        driver_pdl_unlock(dd->blocking->pdl);
    }

    if (res == 0) {
        driver_failure_eof(dd->port_num);
    } else {
	driver_failure_posix(dd->port_num, err);
    }
    return 0;
}

/* fd is the drv_data that is returned from the */
/* initial start routine                        */
/* ready_fd is the descriptor that is ready to read */

static void ready_input(ErlDrvData e, ErlDrvEvent ready_fd)
{
    ErtsSysDriverData *dd = (ErtsSysDriverData*)e;
    ErlDrvPort port_num;
    int packet_bytes;
    int res;
    Uint h;

    port_num = dd->port_num;
    packet_bytes = dd->packet_bytes;

    ASSERT(abs(dd->ifd->fd) == ready_fd);

    if (packet_bytes == 0) {
	byte *read_buf = (byte *) erts_alloc(ERTS_ALC_T_SYS_READ_BUF,
					     ERTS_SYS_READ_BUF_SZ);
	res = read(ready_fd, read_buf, ERTS_SYS_READ_BUF_SZ);
	if (res < 0) {
	    if ((errno != EINTR) && (errno != ERRNO_BLOCK))
		port_inp_failure(dd, res);
	}
	else if (res == 0)
	    port_inp_failure(dd, res);
	else
	    driver_output(port_num, (char*) read_buf, res);
	erts_free(ERTS_ALC_T_SYS_READ_BUF, (void *) read_buf);
    }
    else if (dd->ifd->remain > 0) { /* We try to read the remainder */
	/* space is allocated in buf */
	res = read(ready_fd, dd->ifd->cpos,
		   dd->ifd->remain);
	if (res < 0) {
	    if ((errno != EINTR) && (errno != ERRNO_BLOCK))
		port_inp_failure(dd, res);
	}
	else if (res == 0) {
	    port_inp_failure(dd, res);
	}
	else if (res == dd->ifd->remain) { /* we're done  */
	    driver_output(port_num, dd->ifd->buf,
			  dd->ifd->sz);
	    clear_fd_data(dd->ifd);
	}
	else { /*  if (res < dd->ifd->remain) */
	    dd->ifd->cpos += res;
	    dd->ifd->remain -= res;
	}
    }
    else if (dd->ifd->remain == 0) { /* clean fd */
	byte *read_buf = (byte *) erts_alloc(ERTS_ALC_T_SYS_READ_BUF,
					     ERTS_SYS_READ_BUF_SZ);
	/* We make one read attempt and see what happens */
	res = read(ready_fd, read_buf, ERTS_SYS_READ_BUF_SZ);
	if (res < 0) {
	    if ((errno != EINTR) && (errno != ERRNO_BLOCK))
		port_inp_failure(dd, res);
	}
	else if (res == 0) {     	/* eof */
	    port_inp_failure(dd, res);
	}
	else if (res < packet_bytes - dd->ifd->psz) {
	    memcpy(dd->ifd->pbuf+dd->ifd->psz,
		   read_buf, res);
	    dd->ifd->psz += res;
	}
	else  { /* if (res >= packet_bytes) */
	    unsigned char* cpos = read_buf;
	    int bytes_left = res;

	    while (1) {
		int psz = dd->ifd->psz;
		char* pbp = dd->ifd->pbuf + psz;

		while(bytes_left && (psz < packet_bytes)) {
		    *pbp++ = *cpos++;
		    bytes_left--;
		    psz++;
		}

		if (psz < packet_bytes) {
		    dd->ifd->psz = psz;
		    break;
		}
		dd->ifd->psz = 0;

		switch (packet_bytes) {
		case 1: h = get_int8(dd->ifd->pbuf);  break;
		case 2: h = get_int16(dd->ifd->pbuf); break;
		case 4: h = get_uint32(dd->ifd->pbuf); break;
		default: ASSERT(0); return; /* -1; */
		}

		if (h <= (bytes_left)) {
		    driver_output(port_num, (char*) cpos, h);
		    cpos += h;
		    bytes_left -= h;
		    continue;
		}
		else {		/* The last message we got was split */
                    char *buf = erts_alloc_fnf(ERTS_ALC_T_FD_ENTRY_BUF, h);
		    if (!buf) {
			errno = ENOMEM;
			port_inp_failure(dd, -1);
		    }
		    else {
			erts_atomic_add_nob(&sys_misc_mem_sz, h);
			sys_memcpy(buf, cpos, bytes_left);
			dd->ifd->buf = buf;
			dd->ifd->sz = h;
			dd->ifd->remain = h - bytes_left;
			dd->ifd->cpos = buf + bytes_left;
		    }
		    break;
		}
	    }
	}
	erts_free(ERTS_ALC_T_SYS_READ_BUF, (void *) read_buf);
    }
}


/* fd is the drv_data that is returned from the */
/* initial start routine                        */
/* ready_fd is the descriptor that is ready to read */

static void ready_output(ErlDrvData e, ErlDrvEvent ready_fd)
{
    ErtsSysDriverData *dd = (ErtsSysDriverData*)e;
    ErlDrvPort ix = dd->port_num;
    int n;
    struct iovec* iv;
    int vsize;

    if ((iv = (struct iovec*) driver_peekq(ix, &vsize)) == NULL) {
        if (dd->busy)
            set_busy_port(ix, (dd->busy = 0));
	driver_select(ix, ready_fd, ERL_DRV_WRITE, 0);
        if (dd->terminating)
            driver_failure_atom(dd->port_num,"normal");
	return; /* 0; */
    }
    vsize = vsize > MAX_VSIZE ? MAX_VSIZE : vsize;
    if ((n = writev(ready_fd, iv, vsize)) > 0) {
        ErlDrvSizeT qsz = driver_deq(ix, n);
        if (qsz == (ErlDrvSizeT) -1) {
            driver_failure_posix(ix, EINVAL);
            return;
        }
        if (dd->busy && qsz < dd->low_watermark)
            set_busy_port(ix, (dd->busy = 0));
    }
    else if (n < 0) {
	if (errno == ERRNO_BLOCK || errno == EINTR)
	    return; /* 0; */
	else {
	    int res = errno;
	    driver_select(ix, ready_fd, ERL_DRV_WRITE, 0);
	    driver_failure_posix(ix, res);
	    return; /* -1; */
	}
    }
    return; /* 0; */
}

static void stop_select(ErlDrvEvent fd, void* _)
{
    close((int)fd);
}


static void
fd_async(void *async_data)
{
    ErlDrvSSizeT res;
    ErtsSysDriverData *dd = (ErtsSysDriverData *)async_data;
    SysIOVec      *iov0;
    SysIOVec      *iov;
    int            iovlen;
    int            err = 0;
    /* much of this code is stolen from efile_drv:invoke_writev */
    driver_pdl_lock(dd->blocking->pdl);
    iov0 = driver_peekq(dd->port_num, &iovlen);
    iovlen = iovlen < MAXIOV ? iovlen : MAXIOV;
    iov = erts_alloc_fnf(ERTS_ALC_T_SYS_WRITE_BUF,
                         sizeof(SysIOVec)*iovlen);
    if (!iov) {
        res = -1;
        err = ENOMEM;
        driver_pdl_unlock(dd->blocking->pdl);
    } else {
        memcpy(iov,iov0,iovlen*sizeof(SysIOVec));
        driver_pdl_unlock(dd->blocking->pdl);

        do {
            res = writev(dd->ofd->fd, iov, iovlen);
        } while (res < 0 && errno == EINTR);
        if (res < 0)
            err = errno;

        erts_free(ERTS_ALC_T_SYS_WRITE_BUF, iov);
    }
    dd->blocking->res = res;
    dd->blocking->err = err;
}

void fd_ready_async(ErlDrvData drv_data,
                    ErlDrvThreadData thread_data) {
    ErtsSysDriverData *dd = (ErtsSysDriverData *)thread_data;
    ErlDrvPort port_num = dd->port_num;

    ASSERT(dd->blocking);

    if (dd->blocking->res > 0) {
        ErlDrvSizeT qsz;
        driver_pdl_lock(dd->blocking->pdl);
        qsz = driver_deq(port_num, dd->blocking->res);
        if (qsz == (ErlDrvSizeT) -1) {
            driver_pdl_unlock(dd->blocking->pdl);
            driver_failure_posix(port_num, EINVAL);
            return;
        }
        if (dd->busy && qsz < dd->low_watermark)
            set_busy_port(port_num, (dd->busy = 0));
        driver_pdl_unlock(dd->blocking->pdl);
        if (qsz == 0) {
            if (dd->terminating) {
                /* The port is has been ordered to terminate
                   from either fd_flush or port_inp_failure */
                if (dd->terminating == 1)
                    driver_failure_atom(port_num, "normal");
                else if (dd->terminating == 2)
                    driver_failure_eof(port_num);
                else if (dd->terminating < 0)
                    driver_failure_posix(port_num, -dd->terminating);
                return; /* -1; */
            }
        } else {
            /* still data left to write in queue */
            driver_async(port_num, &dd->blocking->pkey, fd_async, dd, NULL);
            return /* 0; */;
        }
    } else if (dd->blocking->res < 0) {
        if (dd->blocking->err == ERRNO_BLOCK) {
            /* still data left to write in queue */
            driver_async(port_num, &dd->blocking->pkey, fd_async, dd, NULL);
        } else
            driver_failure_posix(port_num, dd->blocking->err);
        return; /* -1; */
    }
    return; /* 0; */
}
