#ifndef RSPAMD_UTIL_H
#define RSPAMD_UTIL_H

#include "config.h"
#include "mem_pool.h"
#include "radix.h"
#include "statfile.h"

struct config_file;
struct rspamd_main;
struct workq;
struct statfile;
struct classifier_config;

/* Create socket and bind or connect it to specified address and port */
gint make_tcp_socket (struct in_addr *, u_short, gboolean is_server, gboolean async);
/* Create socket and bind or connect it to specified address and port */
gint make_udp_socket (struct in_addr *, u_short, gboolean is_server, gboolean async);
/* Accept from socket */
gint accept_from_socket (gint listen_sock, struct sockaddr *addr, socklen_t *len);
/* Create and bind or connect unix socket */
gint make_unix_socket (const gchar *, struct sockaddr_un *, gboolean is_server);
/* Write pid to file */
gint write_pid (struct rspamd_main *);
/* Make specified socket non-blocking */
gint make_socket_nonblocking (gint);
gint make_socket_blocking (gint);
/* Poll sync socket for specified events */
gint poll_sync_socket (gint fd, gint timeout, short events);
/* Init signals */
#ifdef HAVE_SA_SIGINFO
void init_signals (struct sigaction *sa, void (*sig_handler)(gint, siginfo_t *, void *));
#else
void init_signals (struct sigaction *sa, sighandler_t);
#endif
/* Send specified signal to each worker */
void pass_signal_worker (GHashTable *, gint );
/* Convert string to lowercase */
void convert_to_lowercase (gchar *str, guint size);

#ifndef HAVE_SETPROCTITLE
gint init_title(gint argc, gchar *argv[], gchar *envp[]);
gint setproctitle(const gchar *fmt, ...);
#endif

#ifndef HAVE_PIDFILE
struct pidfh {
	gint pf_fd;
#ifdef HAVE_PATH_MAX
	gchar    pf_path[PATH_MAX + 1];
#elif defined(HAVE_MAXPATHLEN)
	gchar    pf_path[MAXPATHLEN + 1];
#else
	gchar    pf_path[1024 + 1];
#endif
 	dev_t pf_dev;
 	ino_t   pf_ino;
};
struct pidfh *pidfile_open(const gchar *path, mode_t mode, pid_t *pidptr);
gint pidfile_write(struct pidfh *pfh);
gint pidfile_close(struct pidfh *pfh);
gint pidfile_remove(struct pidfh *pfh);
#endif

/* Replace %r with rcpt value and %f with from value, new string is allocated in pool */
gchar* resolve_stat_filename (memory_pool_t *pool, gchar *pattern, gchar *rcpt, gchar *from);
#ifdef HAVE_CLOCK_GETTIME
const gchar* calculate_check_time (struct timeval *tv, struct timespec *begin, gint resolution);
#else
const gchar* calculate_check_time (struct timeval *begin, gint resolution);
#endif

double set_counter (const gchar *name, guint32 value);

gboolean lock_file (gint fd, gboolean async);
gboolean unlock_file (gint fd, gboolean async);

guint rspamd_strcase_hash (gconstpointer key);
gboolean rspamd_strcase_equal (gconstpointer v, gconstpointer v2);
guint fstr_strcase_hash (gconstpointer key);
gboolean fstr_strcase_equal (gconstpointer v, gconstpointer v2);

void gperf_profiler_init (struct config_file *cfg, const gchar *descr);

#ifdef RSPAMD_MAIN
stat_file_t* get_statfile_by_symbol (statfile_pool_t *pool, struct classifier_config *ccf, 
		const gchar *symbol, struct statfile **st, gboolean try_create);
#endif

#if ((GLIB_MAJOR_VERSION == 2) && (GLIB_MINOR_VERSION < 22))
void g_ptr_array_unref (GPtrArray *array);
#endif
/*
 * supported formats:
 *	%[0][width][x][X]O		    off_t
 *	%[0][width]T			    time_t
 *	%[0][width][u][x|X]z	    ssize_t/size_t
 *	%[0][width][u][x|X]d	    gint/guint
 *	%[0][width][u][x|X]l	    long
 *	%[0][width][u][x|X]D	    gint32/guint32
 *	%[0][width][u][x|X]L	    gint64/guint64
 *	%[0][width][.width]f	    double
 *	%[0][width][.width]F	    long double
 *	%[0][width][.width]g	    double
 *	%[0][width][.width]G	    long double
 *	%P						    pid_t
 *	%r				            rlim_t
 *	%p						    void *
 *	%V						    f_str_t *
 *	%s						    null-terminated string
 *	%*s					        length and string
 *	%Z						    '\0'
 *	%N						    '\n'
 *	%c						    gchar
 *	%%						    %
 *
 */
gint rspamd_sprintf (gchar *buf, const gchar *fmt, ...);
gint rspamd_fprintf (FILE *f, const gchar *fmt, ...);
gint rspamd_snprintf (gchar *buf, size_t max, const gchar *fmt, ...);
gchar *rspamd_vsnprintf (gchar *buf, size_t max, const gchar *fmt, va_list args);

#endif
