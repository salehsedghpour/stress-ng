/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C)      2022 Colin Ian King.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */
#include "stress-ng.h"
#include "core-net.h"

#if defined(HAVE_SYS_UN_H)
#include <sys/un.h>
#else
UNEXPECTED
#endif

#if defined(HAVE_NETINET_SCTP_H)
#include <netinet/sctp.h>
#else
UNEXPECTED
#endif

#if defined(HAVE_NETINET_TCP_H)
#include <netinet/tcp.h>
#else
UNEXPECTED
#endif

#define MIN_SCTP_PORT		(1024)
#define MAX_SCTP_PORT		(65535)
#define DEFAULT_SCTP_PORT	(9000)

#define SOCKET_BUF		(8192)	/* Socket I/O buffer size */

static const stress_help_t help[] = {
	{ NULL,	"sctp N",	 "start N workers performing SCTP send/receives " },
	{ NULL,	"sctp-ops N",	 "stop after N SCTP bogo operations" },
	{ NULL,	"sctp-if I",	 "use network interface I, e.g. lo, eth0, etc." },
	{ NULL,	"sctp-domain D", "specify sctp domain, default is ipv4" },
	{ NULL,	"sctp-port P",	 "use SCTP ports P to P + number of workers - 1" },
	{ NULL, "sctp-sched S",	 "specify sctp scheduler" },
	{ NULL,	NULL, 		 NULL }
};

#if defined(HAVE_LIB_SCTP) &&	\
    defined(HAVE_NETINET_SCTP_H)

#if !defined(LOCALTIME_STREAM)
#define LOCALTIME_STREAM        0
#endif

static uint64_t	sigpipe_count;
#else
UNEXPECTED
#endif

/*
 *  stress_set_sctp_port()
 *	set port to use
 */
static int stress_set_sctp_port(const char *opt)
{
	int sctp_port;

	stress_set_net_port("sctp-port", opt,
		MIN_SCTP_PORT, MAX_SCTP_PORT - STRESS_PROCS_MAX,
		&sctp_port);
	return stress_set_setting("sctp-port", TYPE_ID_INT, &sctp_port);
}

/*
 *  stress_set_sctp_domain()
 *	set the socket domain option
 */
static int stress_set_sctp_domain(const char *name)
{
	int ret, sctp_domain;

	ret = stress_set_net_domain(DOMAIN_INET | DOMAIN_INET6, "sctp-domain",
				     name, &sctp_domain);
	stress_set_setting("sctp-domain", TYPE_ID_INT, &sctp_domain);

	return ret;
}

static int stress_set_sctp_if(const char *name)
{
        return stress_set_setting("sctp-if", TYPE_ID_STR, name);
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_sctp_domain,	stress_set_sctp_domain },
	{ OPT_sctp_if,		stress_set_sctp_if },
	{ OPT_sctp_port,	stress_set_sctp_port },
	{ 0,			NULL }
};

#if defined(HAVE_LIB_SCTP) &&	\
    defined(HAVE_NETINET_SCTP_H)

#define STRESS_SCTP_SOCKOPT(opt, type)			\
{							\
	type info;					\
	socklen_t opt_len = sizeof(info);		\
	int ret;					\
							\
	ret = getsockopt(fd, IPPROTO_SCTP, opt,		\
		 &info, &opt_len);			\
	if (ret == 0) {					\
		ret = setsockopt(fd, IPPROTO_SCTP, opt,	\
			&info, opt_len);		\
		(void)ret;				\
	}						\
}

/*
 *  stress_sctp_sockopts()
 *	exercise some SCTP specific sockopts
 */
static void stress_sctp_sockopts(const int fd)
{
#if defined(SCTP_RTOINFO)
	STRESS_SCTP_SOCKOPT(SCTP_RTOINFO, struct sctp_rtoinfo)
#else
	UNEXPECTED
#endif
#if defined(SCTP_ASSOCINFO)
	STRESS_SCTP_SOCKOPT(SCTP_ASSOCINFO, struct sctp_assocparams)
#else
	UNEXPECTED
#endif
#if defined(SCTP_INITMSG)
	STRESS_SCTP_SOCKOPT(SCTP_INITMSG, struct sctp_initmsg)
#else
	UNEXPECTED
#endif
#if defined(SCTP_NODELAY)
	STRESS_SCTP_SOCKOPT(SCTP_NODELAY, int)
#else
	UNEXPECTED
#endif
#if defined(SCTP_PRIMARY_ADDR)
	STRESS_SCTP_SOCKOPT(SCTP_PRIMARY_ADDR, struct sctp_prim)
#else
	UNEXPECTED
#endif
#if defined(SCTP_PEER_ADDR_PARAMS)
	STRESS_SCTP_SOCKOPT(SCTP_PEER_ADDR_PARAMS, struct sctp_paddrparams)
#else
	UNEXPECTED
#endif
#if defined(SCTP_EVENTS)
	STRESS_SCTP_SOCKOPT(SCTP_EVENTS, struct sctp_event_subscribe)
#else
	UNEXPECTED
#endif
#if defined(SCTP_MAXSEG)
	STRESS_SCTP_SOCKOPT(SCTP_MAXSEG, struct sctp_assoc_value)
#else
	UNEXPECTED
#endif
#if defined(SCTP_STATUS)
	STRESS_SCTP_SOCKOPT(SCTP_STATUS, struct sctp_status)
#else
	UNEXPECTED
#endif
#if defined(SCTP_GET_PEER_ADDR_INFO) && 0
	STRESS_SCTP_SOCKOPT(SCTP_GET_PEER_ADDR_INFO, struct sctp_paddrinfo)
#else
	/* UNEXPECTED */
#endif
#if defined(SCTP_GET_ASSOC_STATS)
	STRESS_SCTP_SOCKOPT(SCTP_GET_ASSOC_STATS, struct sctp_assoc_stats)
#else
	UNEXPECTED
#endif
}

/*
 *  stress_sctp_client()
 *	client reader
 */
static void stress_sctp_client(
	const stress_args_t *args,
	const pid_t ppid,
	const int sctp_port,
	const int sctp_domain,
	const char *sctp_if)
{
	struct sockaddr *addr;

	(void)setpgid(0, g_pgrp);
	stress_parent_died_alarm();
	(void)sched_settings_apply(true);

	do {
		char buf[SOCKET_BUF];
		int fd;
		int retries = 0;
		socklen_t addr_len = 0;
		struct sctp_event_subscribe events;
retry:
		if (!keep_stressing_flag()) {
			(void)kill(getppid(), SIGALRM);
			_exit(EXIT_FAILURE);
		}
		if ((fd = socket(sctp_domain, SOCK_STREAM, IPPROTO_SCTP)) < 0) {
			if (errno == EPROTONOSUPPORT) {
				if (args->instance == 0)
					pr_inf_skip("%s: SCTP protocol not supported, skipping stressor\n",
						args->name);
				(void)kill(getppid(), SIGALRM);
				_exit(EXIT_NOT_IMPLEMENTED);
			}
			pr_fail("%s: socket failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			/* failed, kick parent to finish */
			(void)kill(getppid(), SIGALRM);
			_exit(EXIT_FAILURE);
		}

		stress_set_sockaddr_if(args->name, args->instance, ppid,
			sctp_domain, sctp_port, sctp_if,
			&addr, &addr_len, NET_ADDR_LOOPBACK);
		if (connect(fd, addr, addr_len) < 0) {
			(void)close(fd);
			(void)shim_usleep(10000);
			retries++;
			if (retries > 100) {
				/* Give up.. */
				pr_fail("%s: connect failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				(void)kill(getppid(), SIGALRM);
				_exit(EXIT_FAILURE);
			}
			goto retry;
		}
		(void)memset(&events, 0, sizeof(events));
		events.sctp_data_io_event = 1;
		if (setsockopt(fd, SOL_SCTP, SCTP_EVENTS, &events,
			sizeof(events)) < 0) {
			(void)close(fd);
			pr_fail("%s: setsockopt failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			(void)kill(getppid(), SIGALRM);
			_exit(EXIT_FAILURE);
		}

		do {
			int flags;
			struct sctp_sndrcvinfo sndrcvinfo;
			ssize_t n;

			n = sctp_recvmsg(fd, buf, sizeof(buf),
				NULL, 0, &sndrcvinfo, &flags);
			if (n <= 0)
				break;
		} while (keep_stressing(args));
		(void)shutdown(fd, SHUT_RDWR);
		(void)close(fd);
	} while (keep_stressing(args));

#if defined(AF_UNIX) &&		\
    defined(HAVE_SOCKADDR_UN)
	if (sctp_domain == AF_UNIX) {
		struct sockaddr_un *addr_un = (struct sockaddr_un *)addr;

		(void)shim_unlink(addr_un->sun_path);
	}
#else
	UNEXPECTED
#endif
	/* Inform parent we're all done */
	(void)kill(getppid(), SIGALRM);
}

/*
 *  stress_sctp_server()
 *	server writer
 */
static int stress_sctp_server(
	const stress_args_t *args,
	const pid_t pid,
	const pid_t ppid,
	const int sctp_port,
	const int sctp_domain,
	const char *sctp_if)
{
	char buf[SOCKET_BUF];
	int fd, status;
	int so_reuseaddr = 1;
	socklen_t addr_len = 0;
	struct sockaddr *addr = NULL;
	uint64_t msgs = 0;
	int rc = EXIT_SUCCESS;

	(void)setpgid(pid, g_pgrp);

	if (stress_sig_stop_stressing(args->name, SIGALRM)) {
		rc = EXIT_FAILURE;
		goto die;
	}
	if ((fd = socket(sctp_domain, SOCK_STREAM, IPPROTO_SCTP)) < 0) {
		if (errno == EPROTONOSUPPORT) {
			if (args->instance == 0)
				pr_inf_skip("%s: SCTP protocol not supported, skipping stressor\n",
					args->name);
			rc = EXIT_NOT_IMPLEMENTED;
			goto die;
		}
		rc = exit_status(errno);
		pr_fail("%s: socket failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		goto die;
	}
	if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR,
		&so_reuseaddr, sizeof(so_reuseaddr)) < 0) {
		pr_fail("%s: setsockopt failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		rc = EXIT_FAILURE;
		goto die_close;
	}

	stress_set_sockaddr_if(args->name, args->instance, ppid,
		sctp_domain, sctp_port, sctp_if, &addr, &addr_len, NET_ADDR_ANY);
	if (bind(fd, addr, addr_len) < 0) {
		rc = exit_status(errno);
		pr_fail("%s: bind failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		goto die_close;
	}
	if (listen(fd, 10) < 0) {
		pr_fail("%s: listen failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		rc = EXIT_FAILURE;
		goto die_close;
	}

	do {
		int sfd;

		if (!keep_stressing(args))
			break;

		sfd = accept(fd, (struct sockaddr *)NULL, NULL);
		if (sfd >= 0) {
			size_t i;

#if defined(TCP_NODELAY)
			int one = 1;

			if (g_opt_flags & OPT_FLAGS_SOCKET_NODELAY) {
				if (setsockopt(fd, SOL_TCP, TCP_NODELAY, &one, sizeof(one)) < 0) {
					pr_inf("%s: setsockopt TCP_NODELAY "
						"failed and disabled, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
					g_opt_flags &= ~OPT_FLAGS_SOCKET_NODELAY;
				}
			}
#else
			UNEXPECTED
#endif

			(void)memset(buf, 'A' + (get_counter(args) % 26), sizeof(buf));

			for (i = 16; i < sizeof(buf); i += 16) {
				ssize_t ret = sctp_sendmsg(sfd, buf, i,
						NULL, 0, 0, 0,
						LOCALTIME_STREAM, 0, 0);
				if (ret < 0)
					break;
				else {
					inc_counter(args);
					msgs++;
				}
			}
			stress_sctp_sockopts(sfd);
			(void)close(sfd);
		}
	} while (keep_stressing(args));

die_close:
	(void)close(fd);
die:
#if defined(AF_UNIX) &&		\
    defined(HAVE_SOCKADDR_UN)
	if (addr && sctp_domain == AF_UNIX) {
		struct sockaddr_un *addr_un = (struct sockaddr_un *)addr;

		(void)shim_unlink(addr_un->sun_path);
	}
#else
	UNEXPECTED
#endif
	if (pid) {
		(void)kill(pid, SIGKILL);
		(void)shim_waitpid(pid, &status, 0);
	}

	return rc;
}

static void stress_sctp_sigpipe(int signum)
{
	(void)signum;

	sigpipe_count++;
}

/*
 *  stress_sctp
 *	stress SCTP by heavy SCTP network I/O
 */
static int stress_sctp(const stress_args_t *args)
{
	pid_t pid, ppid = getppid();
	int sctp_port = DEFAULT_SCTP_PORT;
	int sctp_domain = AF_INET;
	int ret = EXIT_FAILURE;
	char *sctp_if = NULL;

	(void)stress_get_setting("sctp-domain", &sctp_domain);
	(void)stress_get_setting("sctp-if", &sctp_if);
	(void)stress_get_setting("sctp-port", &sctp_port);

	if (sctp_if) {
		int ret;
		struct sockaddr if_addr;

		ret = stress_net_interface_exists(sctp_if, sctp_domain, &if_addr);
		if (ret < 0) {
			pr_inf("%s: interface '%s' is not enabled for domain '%s', defaulting to using loopback\n",
				args->name, sctp_if, stress_net_domain(sctp_domain));
			sctp_if = NULL;
		}
	}

	if (stress_sighandler(args->name, SIGPIPE, stress_sctp_sigpipe, NULL) < 0)
		return EXIT_FAILURE;

	pr_dbg("%s: process [%" PRIdMAX "] using socket port %d\n",
		args->name, (intmax_t)args->pid,
		sctp_port + (int)args->instance);

	stress_set_proc_state(args->name, STRESS_STATE_RUN);
again:
	pid = fork();
	if (pid < 0) {
		if (stress_redo_fork(errno))
			goto again;
		if (!keep_stressing(args))
			goto finish;
		pr_fail("%s: fork failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	} else if (pid == 0) {
		stress_sctp_client(args, ppid, sctp_port, sctp_domain, sctp_if);
		_exit(EXIT_SUCCESS);
	} else {
		ret = stress_sctp_server(args, pid, ppid, sctp_port, sctp_domain, sctp_if);
	}

finish:
	if (sigpipe_count)
		pr_dbg("%s: caught %" PRIu64 " SIGPIPE signals\n", args->name, sigpipe_count);

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return ret;
}

stressor_info_t stress_sctp_info = {
	.stressor = stress_sctp,
	.class = CLASS_NETWORK,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#else
stressor_info_t stress_sctp_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_NETWORK,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#endif
