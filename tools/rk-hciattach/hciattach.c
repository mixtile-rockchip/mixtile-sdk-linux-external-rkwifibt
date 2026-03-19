/*
 *
 *  BlueZ - Bluetooth protocol stack for Linux
 *
 *  Copyright (C) 2000-2001  Qualcomm Incorporated
 *  Copyright (C) 2002-2003  Maxim Krasnyansky <maxk@qualcomm.com>
 *  Copyright (C) 2002-2010  Marcel Holtmann <marcel@holtmann.org>
 *
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */
#define _GNU_SOURCE

#ifndef LOG_TAG
#define LOG_TAG "hciattach"
#endif

#define _GNU_SOURCE

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
//#include <utils/Log.h>

//#include <android/log.h>

#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#ifndef ANDROID
#include <syslog.h>
#endif
#include <termios.h>
#include <time.h>
#include <sys/time.h>
#include <sys/poll.h>
#include <poll.h>
#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/timerfd.h>
// #include <stdbool.h>
#include "hciattach.h"

#define RFKILL_NODE	"/sys/class/rfkill/rfkill0/state"

#ifdef NEED_PPOLL
#include "ppoll.h"
#endif

/* #define SCHED_ENABLE */

#ifdef SCHED_ENABLE
#include <sched.h>
#endif


static volatile sig_atomic_t __io_canceled = 0;

static void sig_hup(int sig)
{
	BT_WARN("signal hup.");
}

static void sig_term(int sig)
{
	switch (sig) {
	case SIGINT:
		BT_WARN("signal int.");
		break;
	case SIGTERM:
		BT_WARN("signal term.");
		break;
	}
	__io_canceled = 1;
}

static void sig_alarm(int sig)
{
	BT_ERROR("Initialization timed out. Time: %s", getCurrentTime());
	exit(1);
}

static int uart_speed(int s)
{
	switch (s) {
	case 9600:
		return B9600;
	case 19200:
		return B19200;
	case 38400:
		return B38400;
	case 57600:
		return B57600;
	case 115200:
		return B115200;
	case 230400:
		return B230400;
	case 460800:
		return B460800;
	case 500000:
		return B500000;
	case 576000:
		return B576000;
	case 921600:
		return B921600;
	case 1000000:
		return B1000000;
	case 1152000:
		return B1152000;
	case 1500000:
		return B1500000;
	case 2000000:
		return B2000000;
#ifdef B2500000
	case 2500000:
		return B2500000;
#endif
#ifdef B3000000
	case 3000000:
		return B3000000;
#endif
#ifdef B3500000
	case 3500000:
		return B3500000;
#endif
#ifdef B4000000
	case 4000000:
		return B4000000;
#endif
	default:
		return B57600;
	}
}

int set_speed(int fd, struct termios *ti, int speed)
{

	if (fd < 0 || ti == NULL || speed <= 0) {
		BT_ERROR("Invalid parameters for set_speed");
		return -EINVAL;
	}

	if (cfsetospeed(ti, uart_speed(speed)) < 0)
		return -errno;

	if (cfsetispeed(ti, uart_speed(speed)) < 0)
		return -errno;

	if (tcsetattr(fd, TCSANOW, ti) < 0)
		return -errno;

	if (tcflush(fd, TCIOFLUSH) < 0)
		return -errno;

	return 0;
}

static int rockchip_init(int fd, struct uart_t *u, struct termios *ti)
{
	BT_DBG("Rockchip Bluetooth init uart with init speed:%d, type:HCI UART %s",
		u->init_speed,
		(u->proto == HCI_UART_H4) ? "H4" : "H5");
	return rk_bt_init(fd, u, ti);
}

struct uart_t uart[] = {
	{ "any",     0x0000, 0x0000, HCI_UART_H4,   115200, 0, 115200, FLOW_CTL, 0, NULL, NULL, NULL},

	/* Rockchip Bluetooth H4 */
	{ "rockchip",  0x0000, 0x0000, HCI_UART_H4,  115200, 0, 115200, AUDIO_VOHCI, 0x0060, NULL, rockchip_init, NULL },

	{ "rockchip_h5",  0x0000, 0x0000, HCI_UART_3WIRE,  115200, 0, 115200, 0, 0x0060, NULL, rockchip_init, NULL },

	{ NULL, 0 }
};

static struct uart_t * get_by_id(int m_id, int p_id)
{
	int i;
	for (i = 0; uart[i].type; i++) {
		if (uart[i].m_id == m_id && uart[i].p_id == p_id)
			return &uart[i];
	}
	return NULL;
}

static struct uart_t * get_by_type(char *type)
{
	int i;
	for (i = 0; uart[i].type; i++) {
		if (!strcmp(uart[i].type, type))
			return &uart[i];
	}
	return NULL;
}

/* Initialize UART driver */
static int init_uart(char *dev, struct uart_t *u, int send_break, int raw)
{
	struct termios ti;
	int fd, i;
	unsigned long flags = 0;

	if (raw)
		flags |= 1 << HCI_UART_RAW_DEVICE;

	fd = open(dev, O_RDWR | O_NOCTTY);
	if (fd < 0) {
		BT_ERROR("Can't open serial port, %d, %s", errno,
		       strerror(errno));
		return -1;
	}

	tcflush(fd, TCIOFLUSH);

	if (tcgetattr(fd, &ti) < 0) {
		BT_ERROR("Can't get port settings, %d, %s", errno,
		       strerror(errno));
		return -1;
	}

	cfmakeraw(&ti);

	ti.c_cflag |= CLOCAL;
	// if (u->flags & FLOW_CTL)
	// 	ti.c_cflag |= CRTSCTS;
	// else
	ti.c_cflag &= ~CRTSCTS;

	if (tcsetattr(fd, TCSANOW, &ti) < 0) {
		BT_ERROR("Can't set port settings, %d, %s", errno,
		       strerror(errno));
		return -1;
	}

	/* Set initial baudrate */
	if (set_speed(fd, &ti, u->init_speed) < 0) {
		BT_ERROR("Can't set initial baud rate, %d, %s", errno,
		       strerror(errno));
		return -1;
	}

	tcflush(fd, TCIOFLUSH);

	if (send_break) {
		tcsendbreak(fd, 0);
		usleep(500000);
	}

	if (u->init && u->init(fd, u, &ti) < 0)
		return -1;

	tcflush(fd, TCIOFLUSH);

	/* Set actual baudrate
	 * There is no need to change baudrate after uart init
	 * */
	/* if (set_speed(fd, &ti, u->speed) < 0) {
	 * 	perror("Can't set baud rate");
	 * 	return -1;
	 * }
	 */

	/* Set TTY to N_HCI line discipline */
	i = N_HCI;
	if (ioctl(fd, TIOCSETD, &i) < 0) {
		BT_ERROR("Can't set line discipline %d, %s", errno,
		       strerror(errno));
		return -1;
	}

	if (flags && ioctl(fd, HCIUARTSETFLAGS, flags) < 0) {
		BT_ERROR("Can't set UART flags %d, %s", errno, strerror(errno));
		return -1;
	}

	if (ioctl(fd, HCIUARTSETPROTO, u->proto) < 0) {
		BT_ERROR("Can't set device %d, %s", errno, strerror(errno));
		return -1;
	}

	if (u->post && u->post(fd, u, &ti) < 0)
		return -1;

	return fd;
}

int reset_bluetooth(void)
{

	int fd;
	char state[2];
	int result;

	/* power off and power on BT */
	fd = open(RFKILL_NODE, O_RDWR);
	if (fd < 0) {
		BT_ERROR("Cannot open %s, %d %s", RFKILL_NODE, errno,
				strerror(errno));
		return -1;
	}
	state[0] = '0';
	state[1] = '\0';
	result = write(fd, state, strlen(state) + 1);
	if (result != (strlen(state) + 1)) {
		BT_ERROR("Cannot write 0 to rfkill state %d %s", errno,
				strerror(errno));
		close(fd);
		return -1;
	}

	usleep(500000);

	state[0] = '1';
	state[1] = '\0';
	result = write(fd, state, strlen(state) + 1);
	if (result != (strlen(state) + 1)) {
		BT_ERROR("Cannot write 1 to rfkill state %d %s", errno,
				strerror(errno));
		close(fd);
		return -1;
	}

	usleep(500000);
	close(fd);

	return 0;
}

static void usage(void)
{
	BT_INFO("hciattach - HCI UART driver initialization utility");
	BT_INFO("Usage:");
	BT_INFO("\thciattach [options] <tty> <type> [speed] [flow|noflow] [sleep|nosleep] [bdaddr]");
	BT_INFO("");
	BT_INFO("Options:");
	BT_INFO("\t-b           Send break signal during initialization");
	BT_INFO("\t-n           Do not detach from terminal (stay in foreground)");
	BT_INFO("\t-p           Print PID when detaching");
	BT_INFO("\t-t timeout   Set initialization timeout in seconds (default: 5)");
	BT_INFO("\t-s speed     Set initial UART speed before device setup");
	BT_INFO("\t-m mode      Set loader mode (1 or 2)");
	BT_INFO("\t-r           Enable raw mode (no line discipline)");
	BT_INFO("\t-i prefix[=value]  Set SCO audio parameter");
	BT_INFO("\t   prefix:   vohci (default) or pcm");
	BT_INFO("\t   value:    hex/decimal Voice_Setting (default: 0x60)");
	BT_INFO("\t   examples: -i, -i vohci, -i pcm=0x60");
	BT_INFO("\t-l           List supported UART types and exit");
	BT_INFO("");
	BT_INFO("Arguments:");
	BT_INFO("\ttty          TTY device path (e.g., ttyS0 or /dev/ttyS0)");
	BT_INFO("\ttype         UART type name (e.g., 'any', 'rockchip')");
	BT_INFO("\tspeed        UART baud rate (e.g., 115200, optional)");
	BT_INFO("\tflow/noflow  Enable/disable hardware flow control (optional)");
	BT_INFO("\tsleep/nosleep Enable/disable power management (optional)");
	BT_INFO("\tbdaddr       Bluetooth device address (XX:XX:XX:XX:XX:XX, optional)");
}

int main(int argc, char *argv[])
{
	struct uart_t *u = NULL;
	int detach, printpid, raw, opt, i, n, ld, err;
	int to = 5;
	int flag = -1;
	int LOADER_MODE = 1;
	int init_speed = 0;
	int send_break = 0;
	int vohci = 1;
	char *vohci_prefix = "vohci";
	char *pcm_prefix = "pcm";
	int sco_para = 0x0060;
	char *numbers = NULL;
	char *endptr = NULL;
	pid_t pid;
	struct sigaction sa;
	struct pollfd p;
	sigset_t sigs;
	char dev[PATH_MAX];
#ifdef SCHED_ENABLE
	struct sched_param sched_par;
#endif

	detach = 1;
	printpid = 0;
	raw = 0;

	while ((opt=getopt(argc, argv, "bnpt:s:m:lri:")) != EOF) {
		switch(opt) {
		case 'b':
			send_break = 1;
			break;

		case 'n':
			detach = 0;
			break;

		case 'p':
			printpid = 1;
			break;

		case 't':
			to = atoi(optarg);
			break;

		case 's':
			init_speed = atoi(optarg);
			break;

		case 'm':
			flag = atoi(optarg);
			if(flag == 1 || flag == 2){
				LOADER_MODE = flag;
				break;
			}else{
				BT_INFO("Invalid parameter please try again\n");
				exit(0);
			}

		case 'l':
			for (i = 0; uart[i].type; i++) {
				BT_INFO("%-10s0x%04x,0x%04x", uart[i].type,
							uart[i].m_id, uart[i].p_id);
			}
			exit(0);

		case 'r':
			raw = 1;
			break;
		case 'i':
			if (strncmp(optarg, vohci_prefix, strlen(vohci_prefix)) == 0) {
				numbers = optarg + strlen(vohci_prefix) + 1; //skip '='
				vohci = 1;
			}
			else if (strncmp(optarg, pcm_prefix, strlen(pcm_prefix)) == 0) {
				numbers = optarg + strlen(pcm_prefix) + 1; //skip '='
				vohci = 0;
			}
			sco_para = strtol(numbers, &endptr, 0);
			if (numbers == endptr || errno == ERANGE) {
				sco_para = 0x60;
			}
			break;
		default:
			usage();
			exit(1);
		}
	}

	n = argc - optind;
	if (n < 2) {
		BT_ERROR("Not enough arguments. Please provide at least a device path and a device type.");
		usage();
		exit(1);
	}

	for (n = 0; optind < argc; n++, optind++) {
		char *opt;

		opt = argv[optind];

		switch(n) {
		case 0:
			dev[0] = 0;
			if (!strchr(opt, '/'))
				strcpy(dev, "/dev/");
			strcat(dev, opt);
			break;

		case 1:
			if (strchr(argv[optind], ',')) {
				int m_id, p_id;
				sscanf(argv[optind], "%x,%x", &m_id, &p_id);
				u = get_by_id(m_id, p_id);
			} else {
				u = get_by_type(opt);
			}

			if (!u) {
				BT_ERROR("Unknown device type or id");
				exit(1);
			}

			break;

		case 2:
			u->speed = atoi(argv[optind]);
			break;

		case 3:
			if (!strcmp("flow", argv[optind]))
				u->flags |=  FLOW_CTL;
			else
				u->flags &= ~FLOW_CTL;
			break;

		case 4:
			if (!strcmp("sleep", argv[optind]))
				u->flags |= ENABLE_PM;
			else
				u->flags &= ~ENABLE_PM;
			break;

		case 5:
			u->bdaddr = argv[optind];
			break;
		}
	}

	if (vohci) {
		u->flags |= AUDIO_VOHCI;
	} else {
		u->flags &= ~AUDIO_VOHCI;
	}
	u->sco_para = sco_para;

	if (!u) {
		BT_ERROR("Unknown device type or id");
		exit(1);
	}

start:

#ifdef SCHED_ENABLE
	BT_INFO("Increase the priority of the process with set sched");
	memset(&sched_par, 0, sizeof(sched_par));
	sched_par.sched_priority = 99;
	err = sched_setscheduler(0, SCHED_FIFO, &sched_par);
	if (err == -1) {
		BT_ERROR("Call sched_setscheduler error, %s",
			strerror(errno));
	}
/* #else
 * 	BT_INFO("Increase the priority of the process with nice");
 * 	err = nice(-20);
 * 	if (err == -1) {
 * 		BT_ERROR("Call nice error, %s", strerror(errno));
 * 	}
 */
#endif

	/* If user specified a initial speed, use that instead of
	   the hardware's default */
	if (init_speed)
		u->init_speed = init_speed;

	if(LOADER_MODE == 1 || LOADER_MODE == 2)
		u->loader_mode = LOADER_MODE;

	memset(&sa, 0, sizeof(sa));
	sa.sa_flags   = SA_NOCLDSTOP;
	sa.sa_handler = sig_alarm;
	sigaction(SIGALRM, &sa, NULL);

	/* 10 seconds should be enough for initialization */
	alarm(to);

	n = init_uart(dev, u, send_break, raw);
	if (n < 0) {
		BT_ERROR("Can't initialize device %d, %s", errno,
		       strerror(errno));
		exit(1);
	}

	BT_INFO("Device setup complete. Time: %s", getCurrentTime());
	BT_INFO("Please wait ...");

	alarm(0);

	memset(&sa, 0, sizeof(sa));
	sa.sa_flags   = SA_NOCLDSTOP;
	sa.sa_handler = SIG_IGN;
	sigaction(SIGCHLD, &sa, NULL);
	sigaction(SIGPIPE, &sa, NULL);

	sa.sa_handler = sig_term;
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGINT,  &sa, NULL);

	sa.sa_handler = sig_hup;
	sigaction(SIGHUP, &sa, NULL);

	if (detach) {
		if ((pid = fork())) {
			if (printpid)
				BT_INFO("%d", pid);
			return 0;
		}

		for (i = 0; i < 20; i++)
			if (i != n)
				close(i);
	}

	p.fd = n;
	p.events = POLLERR | POLLHUP;

	sigfillset(&sigs);
	sigdelset(&sigs, SIGCHLD);
	sigdelset(&sigs, SIGPIPE);
	sigdelset(&sigs, SIGTERM);
	sigdelset(&sigs, SIGINT);
	sigdelset(&sigs, SIGHUP);

	while (!__io_canceled) {
		p.revents = 0;
		err = ppoll(&p, 1, NULL, &sigs);
		if (err < 0 && errno == EINTR) {
			BT_INFO("Got EINTR.");
			continue;
		} if (err){
			if (p.revents & POLLERR) {
				BT_INFO("Poll error event.");
			}
			else if (p.revents & POLLHUP) {
				BT_INFO("Poll hangup event.");
			}
			break;
		}
	}

	BT_INFO("err %d, p->revents %04x", err, p.revents);

	/* Restore TTY line discipline */
	BT_INFO("Restore TTY line discipline");
	ld = N_TTY;
	int retries = 3;
	while (retries-- > 0) {
		if (ioctl(n, TIOCSETD, &ld) >= 0) {
			break;
		}
		BT_ERROR("Can't restore line discipline %d, %s", errno, strerror(errno));
		usleep(100000); // 等待 100 毫秒
	}

	if (retries <= 0) {
		BT_ERROR("Failed to restore line discipline after multiple attempts.");
		exit(1);
	}

	if (p.revents & (POLLERR | POLLHUP)) {
		BT_INFO("Recover...");
		reset_bluetooth();
		goto start;
	}

	return 0;
}

