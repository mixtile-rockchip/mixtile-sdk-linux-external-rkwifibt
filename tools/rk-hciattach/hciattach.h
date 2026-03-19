/*
 *
 *  BlueZ - Bluetooth protocol stack for Linux
 *
 *  Copyright (C) 2003-2010  Marcel Holtmann <marcel@holtmann.org>
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
#ifndef __HCIATTACH_H__
#define __HCIATTACH_H__
#include <termios.h>

#ifndef N_HCI
#define N_HCI	15
#endif

#define HCIUARTSETPROTO		_IOW('U', 200, int)
#define HCIUARTGETPROTO		_IOR('U', 201, int)
#define HCIUARTGETDEVICE	_IOR('U', 202, int)
#define HCIUARTSETFLAGS		_IOW('U', 203, int)
#define HCIUARTGETFLAGS		_IOR('U', 204, int)

#define HCI_UART_H4	0
#define HCI_UART_BCSP	1
#define HCI_UART_3WIRE	2
#define HCI_UART_H4DS	3
#define HCI_UART_LL	4
#define HCI_UART_ATH3K  5
#define HCI_UART_INTEL	6
#define HCI_UART_BCM	7
#define HCI_UART_QCA	8
#define HCI_UART_AG6XX	9
#define HCI_UART_NOKIA	10
#define HCI_UART_MRVL	11

#define HCI_UART_RAW_DEVICE	0
#define HCI_UART_RESET_ON_INIT	1
#define HCI_UART_CREATE_AMP	2
#define HCI_UART_INIT_PENDING	3
#define HCI_UART_EXT_CONFIG	4
#define HCI_UART_VND_DETECT	5

#ifndef FALSE
#define FALSE  0
#endif

#ifndef TRUE
#define TRUE   (!FALSE)
#endif

#define SCO_PARA_NUM    5
#define FLOW_CTL      0x1
#define AUDIO_VOHCI   0x2
#define ENABLE_PM     0x4

#ifndef B3000000
#define B3000000
#endif

#define UNUSED(x) (void)(x)

#ifdef ANDROID
#include <stdio.h>

#define BT_INFO(param, ...) do {  \
                                                            ALOGI(param, ##__VA_ARGS__);\
                                                            printf("TAG:%s:"param"\n", LOG_TAG, ##__VA_ARGS__);\
                                                    }while(0)

#define BT_DBG(param, ...) do {  \
                                                            ALOGD(param, ##__VA_ARGS__);\
                                                            printf("TAG:%s:"param"\n", LOG_TAG, ##__VA_ARGS__);\
                                                    }while(0)

#define BT_WARN(param, ...) do {  \
                                                                ALOGW(param, ##__VA_ARGS__);\
                                                                printf("TAG:%s:"param"\n", LOG_TAG, ##__VA_ARGS__);\
                                                        }while(0)

#define BT_ERROR(param, ...) do {  \
                                                                ALOGE(param, ##__VA_ARGS__); \
                                                                printf("TAG:%s:"param"\n", LOG_TAG, ##__VA_ARGS__); \
                                                        }while(0)

#else
#define BT_INFO(fmt, arg...) do {  \
                                                            printf("TAG:%s:"fmt"\n", LOG_TAG, ##arg);\
                                                    }while(0)

#define BT_DBG(fmt, arg...) do {  \
                                                            printf("TAG:%s:"fmt"\n", LOG_TAG, ##arg);\
                                                    }while(0)

#define BT_WARN(fmt, arg...) do {  \
                                                                printf("TAG:%s:"fmt"\n", LOG_TAG, ##arg);\
                                                        }while(0)

#define BT_ERROR(fmt, arg...) do {  \
                                                                printf("TAG:%s:"fmt"\n", LOG_TAG, ##arg); \
                                                        }while(0)

#endif

struct uart_t {
	char *type;
	int  m_id;
	int  p_id;
	int  proto;
	int  init_speed;
	int  loader_mode;
	int  speed;
	int  flags;
	int sco_para;
	char *bdaddr;
	int  (*init) (int fd, struct uart_t *u, struct termios *ti);
	int  (*post) (int fd, struct uart_t *u, struct termios *ti);
};

char* getCurrentTime();
int read_hci_event(int fd, unsigned char *buf, int size);
int set_speed(int fd, struct termios *ti, int speed);
int rk_bt_init(int fd, struct uart_t *u, struct termios *ti);
int reset_bluetooth(void);

#endif
