// SPDX-License-Identifier: GPL-2.0
//#define _GNU_SOURCE
#ifndef LOG_TAG
#define LOG_TAG "hciattach_rk"
#endif
//#include <android/log.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>
#include <time.h>
#include <limits.h>
#include <stdint.h>
#include <stdbool.h>
#include <poll.h>
#include <sys/poll.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/stat.h>
//#include <utils/Log.h>
#include <sys/time.h>
#include <assert.h>

#include "hciattach.h"

#define RK962_FW1_SVN_VERSION_LIMIT_V1_1 11858
#define RK962_FW2_SVN_VERSION_LIMIT_V1_1 11858
#define RK960_FW1_SVN_VERSION_LIMIT 11866
#define RK960_FW2_SVN_VERSION_LIMIT 11866
#define RK962_FW1_SVN_VERSION_LIMIT 11866
#define RK962_FW2_SVN_VERSION_LIMIT 11866
#define RK962_FW3_SVN_VERSION_LIMIT 10573

#define STREAM_TO_UINT16(u16, p) {u16 = ((uint16_t)(*(p)) + (((uint16_t)(*((p) + 1))) << 8)); (p) += 2;}
#define UINT8_TO_STREAM(p, u8)   {*(p)++ = (uint8_t)(u8);}
#define UINT16_TO_STREAM(p, u16) {*(p)++ = (uint8_t)(u16); *(p)++ = (uint8_t)((u16) >> 8);}
#define UINT32_TO_STREAM(p, u32) {*(p)++ = (uint8_t)(u32); *(p)++ = (uint8_t)((u32) >> 8); *(p)++ = (uint8_t)((u32) >> 16); *(p)++ = (uint8_t)((u32) >> 24);}

enum {
    RK960_C1 = 0,
    RK960_D1,
    RK960_E1,
    RK962_E1,
    RK960_E2,
    RK962_E2
};

enum {
    STAGE_UPDATE_LOADER_BAUDRATE = 0,
    STAGE_UPDATE_FW_WORK_BAUDRATE,
};

typedef struct
{
    uint16_t patch;
    uint8_t minor;
    uint8_t major;
}fw_version_t;

#define RK960_C1_STACK_BASE 0x20160000
#define RK960_D1_STACK_BASE 0x20173400
#define RK962_E1_STACK_BASE 0x00213400

#define RK960_C1_LOADER_SRAM_BASE 0x20100000
#define RK960_D1_LOADER_SRAM_BASE 0x20172000
#define RK962_E1_LOADER_SRAM_BASE 0x00212000

#define RK96X_FW_SRAM_BASE 0x00200000

#define RK96X_LOADER_NAME "rk96x_bt_loader.bin"

#define RK960_FW1 "rk960_bt_rf.bin"
#define RK960_FW2 "rk960_bt.bin"
#define RK962_FW1 "rk962_bt_rf.bin"
#define RK962_FW2 "rk962_bt.bin"
#define RK962_FW3 "rk962_bt_mcu.bin"

#define RK960_FW1_V2 "rk960_bt_rf_v2.bin"
#define RK960_FW2_V2 "rk960_bt_v2.bin"
#define RK962_FW1_V2 "rk962_bt_rf_v2.bin"
#define RK962_FW2_V2 "rk962_bt_v2.bin"

#define RK962_FW1_EXECUTION_TIME               400

#define PARAM_ID_BD_ADDRESS 0x01
#define PARAM_ID_DEVICE_NAME 0x02

#define PARAM_ID_SYNC_CONFIG            0x2C
#define PARAM_LEN_SYNC_CONFIG           0x02
// Voice over HCI audio data path
#define AUDIO_VOICE_OVER_HCI            0
// PCM handle by baseband audio data path
#define AUDIO_HW_PCM                    1

#define PARAM_LEN_BD_ADDRESS 6
#define PARAM_LEN_DEVICE_NAME 248
#define BD_ADDR_LEN 6
#define SET_FW_RUN_ADDR_SIZE  4

#define RK96X_FW_VERSION_ADDR_V1_1 0x1000
#define RK96X_FW_VERSION_ADDR 0xC0

#define RK962_FW1_SVN_VERSION_ADDR_V1_1 0x140
#define RK962_FW2_SVN_VERSION_ADDR_V1_1 0x3A58

#define RK960_FW1_SVN_VERSION_ADDR 0xC0
#define RK960_FW2_SVN_VERSION_ADDR 0xC4
#define RK962_FW1_SVN_VERSION_ADDR 0xC0
#define RK962_FW2_SVN_VERSION_ADDR 0xC4
#define RK962_FW3_SVN_VERSION_ADDR 0xC0

#define RETRY_TIMES 3
#define ACK_TIMEOUT_MS 1000


/* sleep mode

    0: disable
    1: UART with Host wake/BT wake out of band signals
*/
#ifndef LPM_SLEEP_MODE
#define LPM_SLEEP_MODE                  1
#endif

/* Host Stack Idle Threshold in 300ms or 25ms

  In sleep mode 1, this is the number of firmware loops executed with no
    activity before the Host wake line is deasserted. Activity includes HCI
    traffic excluding certain sleep mode commands and the presence of SCO
    connections if the "Allow Host Sleep During SCO" flag is not set to 1.
    Each count of this parameter is roughly equivalent to 300ms or 25ms.
*/
#ifndef LPM_IDLE_THRESHOLD
#define LPM_IDLE_THRESHOLD              1
#endif

/* Host Controller Idle Threshold in 300ms or 25ms

    This is the number of firmware loops executed with no activity before the
    HC is considered idle. Depending on the mode, HC may then attempt to sleep.
    Activity includes HCI traffic excluding certain sleep mode commands and
    the presence of ACL/SCO connections.
*/
#ifndef LPM_HC_IDLE_THRESHOLD
#define LPM_HC_IDLE_THRESHOLD           1
#endif

/* BT_WAKE Polarity - 0=Active Low, 1= Active High */
#ifndef LPM_BT_WAKE_POLARITY
#define LPM_BT_WAKE_POLARITY            1    /* maguro */
#endif

/* HOST_WAKE Polarity - 0=Active Low, 1= Active High */
#ifndef LPM_HOST_WAKE_POLARITY
#define LPM_HOST_WAKE_POLARITY          1    /* maguro */
#endif

/* LPM_ALLOW_HOST_SLEEP_DURING_SCO

    When this flag is set to 0, the host is not allowed to sleep while
    an SCO is active. In sleep mode 1, the device will keep the host
    wake line asserted while an SCO is active.
    When this flag is set to 1, the host can sleep while an SCO is active.
    This flag should only be set to 1 if SCO traffic is directed to the PCM
    interface.
*/
#ifndef LPM_ALLOW_HOST_SLEEP_DURING_SCO
#define LPM_ALLOW_HOST_SLEEP_DURING_SCO 1
#endif

/* LPM_COMBINE_SLEEP_MODE_AND_LPM

    In Mode 0, always set byte 7 to 0. In sleep mode 1, device always
    requires permission to sleep between scans / periodic inquiries regardless
    of the setting of this byte. In sleep mode 1, if byte is set, device must
    have "permission" to sleep during the low power modes of sniff, hold, and
    park. If byte is not set, device can sleep without permission during these
    modes. Permission to sleep in Mode 1 is obtained if the BT_WAKE signal is
    not asserted.
*/
#ifndef LPM_COMBINE_SLEEP_MODE_AND_LPM
#define LPM_COMBINE_SLEEP_MODE_AND_LPM  1
#endif

/* LPM_ENABLE_UART_TXD_TRI_STATE

    When set to 0, the device will not tristate its UART TX line before going
    to sleep.
    When set to 1, the device will tristate its UART TX line before going to
    sleep.
*/
#ifndef LPM_ENABLE_UART_TXD_TRI_STATE
#define LPM_ENABLE_UART_TXD_TRI_STATE   0
#endif

/* LPM_PULSED_HOST_WAKE
*/
#ifndef LPM_PULSED_HOST_WAKE
#define LPM_PULSED_HOST_WAKE            0
#endif

//Buffer structure
struct buffer_tag
{
	// length of buffer
	uint8_t length;
	// data of 128 bytes length
	uint8_t data[128];
};

typedef struct hci_op_par_cmd
{
	//Parameter tag
	uint8_t param_tag;
	//Structure buffer
	struct buffer_tag buf;
} hci_op_par_cmd_t;

typedef struct
{
    uint8_t sleep_mode;                     /* 0(disable),1(UART),9(H5) */
    uint8_t host_stack_idle_threshold;      /* Unit scale 300ms/25ms */
    uint8_t host_controller_idle_threshold; /* Unit scale 300ms/25ms */
    uint8_t bt_wake_polarity;               /* 0=Active Low, 1= Active High */
    uint8_t host_wake_polarity;             /* 0=Active Low, 1= Active High */
    uint8_t allow_host_sleep_during_sco;
    uint8_t combine_sleep_mode_and_lpm;
    uint8_t enable_uart_txd_tri_state;      /* UART_TXD Tri-State */
    uint8_t sleep_guard_time;               /* sleep guard time in 12.5ms */
    uint8_t wakeup_guard_time;              /* wakeup guard time in 12.5ms */
    uint8_t txd_config;                     /* TXD is high in sleep state */
    uint8_t pulsed_host_wake;               /* pulsed host wake if mode = 1 */
} bt_lpm_param_t;

typedef struct
{
    uint32_t res0_0:3;
    uint32_t clk_32k_sel:2;   // 0:gpll_div, 1:external 32K crystal, 2:in-chip 32K clock
    uint32_t wdt_en:1;        // 0:disable wdt, 1:enable wdt
    uint32_t res0_6:3;
    uint32_t flow_ctrl_en:1;  // 0:disable flow ctrl, 1:enable flow ctrl
    uint32_t res0_10:1;
    uint32_t wdt_timeout:8;   // wdt timeout for seconds, actual reset time >= wdt_timeout * 2
    uint32_t res0_19:2;
    uint32_t sleep_enable:1;  // Switch of low power, enable default
    uint32_t res0_22:10;
    uint32_t res1;
} board_Config_t;

typedef struct
{
    board_Config_t mask;
    board_Config_t config;
} board_Config_Set_t;

board_Config_Set_t g_board_config = {
    .mask = {
        .res0_0 = 0,
        .clk_32k_sel = 0,
        .wdt_en = 0,
        .res0_6 = 0,
        .flow_ctrl_en = 0,
        .res0_10 = 0,
        .wdt_timeout = 0,
        .res0_19 = 0,
        .sleep_enable = 0,
        .res0_22 = 0,
        .res1 = 0,
    },
    .config = {
        .res0_0 = 0,
        .clk_32k_sel = 1,
        .wdt_en = 1,
        .res0_6 = 0,
        .flow_ctrl_en = 0,
        .res0_10 = 0,
        .wdt_timeout = 1,
        .res0_19 = 0,
        .sleep_enable = 0,
        .res0_22 = 0,
        .res1 = 0,
    },
};

typedef struct
{
    const char * name;
    uint32_t svn_off;
    uint32_t svn_limit;
    uint8_t is_check_rom;
    uint8_t is_check_svn;
} fw_verify_t;

static fw_version_t rom_verison;
static int loader_type = RK962_E1;
static int last_fragment = 0x0;
static int last_opcode = 0x0;
static const char * fw1_name = RK960_FW1;
static const char * fw2_name = RK960_FW2;

static bt_lpm_param_t lpm_param =
{
    LPM_SLEEP_MODE,
    LPM_IDLE_THRESHOLD,
    LPM_HC_IDLE_THRESHOLD,
    LPM_BT_WAKE_POLARITY,
    LPM_HOST_WAKE_POLARITY,
    LPM_ALLOW_HOST_SLEEP_DURING_SCO,
    LPM_COMBINE_SLEEP_MODE_AND_LPM,
    LPM_ENABLE_UART_TXD_TRI_STATE,
    0,  /* not applicable */
    0,  /* not applicable */
    0,  /* not applicable */
    LPM_PULSED_HOST_WAKE
};

static fw_verify_t fw_verify_table[] =
{
    /*name offset limit rom svn*/
    {RK960_FW1, 0, 0, 0, 0},
    {RK960_FW2, 0, 0, 0, 0},
    {RK962_FW1, RK962_FW1_SVN_VERSION_ADDR_V1_1, RK962_FW1_SVN_VERSION_LIMIT_V1_1, 0, 1},
    {RK962_FW2, RK962_FW2_SVN_VERSION_ADDR_V1_1, RK962_FW2_SVN_VERSION_LIMIT_V1_1, 1, 1},
    {RK962_FW3, RK962_FW3_SVN_VERSION_ADDR, RK962_FW3_SVN_VERSION_LIMIT, 0, 1},
    {RK960_FW1_V2, RK960_FW1_SVN_VERSION_ADDR, RK960_FW1_SVN_VERSION_LIMIT, 0, 1},
    {RK960_FW2_V2, RK960_FW2_SVN_VERSION_ADDR, RK960_FW2_SVN_VERSION_LIMIT, 1, 1},
    {RK962_FW1_V2, RK962_FW1_SVN_VERSION_ADDR, RK962_FW1_SVN_VERSION_LIMIT, 0, 1},
    {RK962_FW2_V2, RK962_FW2_SVN_VERSION_ADDR, RK962_FW2_SVN_VERSION_LIMIT, 1, 1},
    { /* sentinel */ },
};

char* getCurrentTime() {
    const size_t buf_size = 32;
    char* timeStr = (char*)malloc(buf_size);
    if (!timeStr) {
        BT_ERROR("Memory allocation failed");
        exit(EXIT_FAILURE);
    }

    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) == -1) {
        BT_ERROR("Failed to get current time");
        free(timeStr);
        exit(EXIT_FAILURE);
    }

    // snprintf(timeStr, buf_size, "%ld-%02ld-%02ld %02ld:%02ld:%02ld.%03ld",
    snprintf(timeStr, buf_size, "%02d:%02d:%02d",
           //  ts.tv_sec / 100000000 + 1970,
           //  (ts.tv_sec / 1000000 % 100),
           //  (ts.tv_sec / 10000 % 100),
             (int)(ts.tv_sec / 3600),     // Сʱ
             (int)(ts.tv_sec % 3600 / 60),
             (int)(ts.tv_sec % 60));
           //  ((ts.tv_nsec % 1000000000) / 1000000));

    return timeStr;
}

static unsigned int do_js_hash(unsigned int hash, unsigned char *buf, unsigned int len)
{
    unsigned int i  = 0;

    for(i = 0; i < len; i++) {
        hash ^= ((hash << 5) + buf[i] + (hash >> 2));
    }
    return hash;
}

#define OSI_NO_INTR(fn)  do {} while ((fn) == -1 && errno == EINTR)

static int UartWrite(int fd, void *data, uint32_t size)
{
    int ret;

    OSI_NO_INTR(ret = write(fd, data, size));
    return ret;
}

static int UartRead(int fd, void *data, uint32_t size)
{
    int ret;

    OSI_NO_INTR(ret = read(fd, data, size));
    return ret;
}

static int UartReadn(int fd, uint8_t *data, uint32_t size)
{
    uint32_t nleft = size;
    int32_t nread;
    BT_INFO("%s toRead size %d, nleft is %d", __func__, size, nleft);

    while(nleft > 0){
        if ((nread = UartRead(fd, data, nleft)) < 0) {
            BT_DBG("%s error:%d\n", __func__, errno);
            if(nleft == size)
                return (-1);
            else
                break;
        }
        else if(nread == 0){
            break; /* EOF */
        }
        nleft -= nread;
        data += nread;
        //BT_DBG("%s toRead left %d", __func__, nleft);
    }

    BT_DBG("%s return %d, nread = %d\n", __func__, (size - nleft), nread);
    return (size - nleft);
 }

static int UartReadTimeoutn(int fd, uint8_t *data, uint32_t size, int msec)
{
    struct pollfd p[1] = {0};
    int result ;
    p[0].fd = fd;
    p[0].events = POLLERR | POLLHUP | POLLIN;
    result = poll(p, 1, msec);

    if (result == 0)
    {
        BT_INFO("bt is not in maskrom");
        exit(0);
    }

    if ((result < 0) || (p[0].revents & (POLLERR | POLLHUP))) {
        BT_WARN("Poll call error, %s", strerror(errno));
        exit(-1);
    }

    return UartReadn(fd, data, size);
 }

static void uart_set_hw_fctrl(int fd, uint8_t hw_fctrl)
{
    struct termios termios_old;

    if (fd == -1)
    {
        BT_ERROR("vnd_userial.fd is -1");
        return;
    }

    tcgetattr(fd, &termios_old);
    if(hw_fctrl)
    {
        if(termios_old.c_cflag & CRTSCTS)
        {
            BT_DBG("userial_vendor_set_hw_fctrl already hw flowcontrol on");
            return;
        }
        else
        {
            termios_old.c_cflag |= CRTSCTS;
            tcsetattr(fd, TCSANOW, &termios_old);
            BT_DBG("userial_vendor_set_hw_fctrl set hw flowcontrol on");
        }
    }
    else
    {
        if(termios_old.c_cflag & CRTSCTS)
        {
            termios_old.c_cflag &= ~CRTSCTS;
            tcsetattr(fd, TCSANOW, &termios_old);
            BT_DBG("userial_vendor_set_hw_fctrl set hw flowcontrol off");
            return;
        }
        else
        {
            BT_DBG("userial_vendor_set_hw_fctrl already hw flowcontrol off");
            return;
        }
    }
}

int set_rom_verison(fw_version_t ver)
{
    rom_verison = ver;
    return 0;
}

fw_version_t get_rom_verison(void)
{
    return rom_verison;
}

int set_loader_type(int type)
{
    loader_type = type;
    return 0;
}

int get_loader_type(void)
{
    return loader_type;
}

int check_loader_type(char * bin)
{
    int fw_fd;
    char fw_path[128] = {0};
    uint32_t loader_stack_base = 0;
    char *rk96x_path = getenv("RK96X_TARGET_PATH");

    if (rk96x_path != NULL) {
        snprintf(fw_path, sizeof(fw_path), "%s/fw/%s", rk96x_path, bin);
        BT_DBG("fw_path is '%s'.", fw_path);
    }
    else {
        BT_ERROR("RK96X_TARGET_PATH is NULL");
        snprintf(fw_path, sizeof(fw_path), "%s/%s", "/vendor/etc/firmware", bin);
        BT_DBG("fw_path is '%s'.", fw_path);
    }

    if ((fw_fd = open(fw_path, O_RDONLY)) == -1) {
        BT_ERROR("vendor lib preload failed to open [%s]", fw_path);
        return (-1);
    }
    read(fw_fd, &loader_stack_base, 4);
    if (loader_stack_base == RK960_D1_STACK_BASE)
        set_loader_type(RK960_D1);
    else if (loader_stack_base == RK960_C1_STACK_BASE)
        set_loader_type(RK960_C1);
    else if (loader_stack_base == RK962_E1_STACK_BASE)
        set_loader_type(RK962_E1);
    else
        set_loader_type(RK962_E1);

    close(fw_fd);

    return 0;
}

int32_t hw_get_loader_load_addr(void)
{
    int loader_type = get_loader_type();
    if (loader_type == RK960_C1)
        return RK960_C1_LOADER_SRAM_BASE;
    else if (loader_type == RK960_D1)
        return RK960_D1_LOADER_SRAM_BASE;
    else if (loader_type == RK962_E1)
        return RK962_E1_LOADER_SRAM_BASE;
    else
        return RK962_E1_LOADER_SRAM_BASE;
}


static int rk_bt_boot(int fd, char* bin)
{
    struct _download_request
    {
        uint32_t tag;//'REQ '
        uint16_t cmd;//0x0471 or 0x0472
        uint32_t data_length;//byte unit
        uint16_t package_size;
        uint16_t timeout;     //ms
        uint32_t load_addr;//secotr unit
        uint32_t data_hash;
        uint8_t  reserved[6];
        uint32_t req_hash;
    } __attribute__((packed));

    struct _download_response
    {
        uint32_t tag;//'ACK '
        uint8_t reserved[6];
        uint8_t cause;
        uint8_t status;//0:ok,1:fail
    } __attribute__((packed));

    uint32_t read_buf[128];
    char fw_path[128] = {0};

    int i;
    uint32_t size, package_size;
    struct _download_request req;
    struct _download_response resp;
    uint8_t *pBuf;

    const char* READY_TAG = "RKUART";
    const uint8_t REQ_TAG[4] = {'R', 'E', 'Q', ' '};
    const uint8_t RESP_TAG[4] = {'A', 'C', 'K', ' '};

    uart_set_hw_fctrl(fd, 0);
    usleep(20 * 1000);

    tcflush(fd, TCIFLUSH);
    tcflush(fd, TCOFLUSH);
    tcflush(fd, TCIOFLUSH);

    package_size = 1024;
    pBuf = (uint8_t *)read_buf;
    size = strlen(READY_TAG)*2-1;

    /*wait device ready*/
    int count = 0, len;
    do
    {
        len = UartReadTimeoutn(fd, pBuf, size, 100);

        for (i=0; i<len; i++)
        {
            //BT_DBG("recv=[%c],hex= [0x%02x], total: %d, cnt:%d", pBuf[i], pBuf[i], len, count);
        }

        if(strstr((const char*)pBuf, READY_TAG)!= NULL)
            break;
    }while(++count);

    int fw_fd;
    pBuf = (uint8_t *) malloc(package_size);
    if(!pBuf)
        return -1;

    char *rk96x_path = getenv("RK96X_TARGET_PATH");
    if (rk96x_path != NULL) {
        snprintf(fw_path, sizeof(fw_path), "%s/fw/%s", rk96x_path, bin);
        BT_DBG("fw_path is '%s'.", fw_path);
    }
    else {
        BT_ERROR("RK96X_TARGET_PATH is NULL");
        snprintf(fw_path, sizeof(fw_path), "%s/%s", "/vendor/etc/firmware", bin);
        BT_DBG("fw_path is '%s'.", fw_path);
    }

    if ((fw_fd = open(fw_path, O_RDONLY)) == -1)
    {
        BT_ERROR("vendor lib preload failed to open [%s], reason:%s", fw_path, strerror(errno));
        free(pBuf);
        pBuf = NULL;
        return (-1);
    }

    uint32_t fileSize = lseek(fw_fd, 0, SEEK_END);
    lseek(fw_fd, 0, SEEK_SET);
    /*download firmware*/
    memset(&req, 0, sizeof(req));
    req.tag = *((uint32_t *)REQ_TAG);
    req.cmd = 0x0471;
    req.data_length = fileSize;//sizeof(DownloadBin);
    req.package_size = package_size;
    req.timeout = 5000; // 5S
    req.load_addr = hw_get_loader_load_addr();
    //req.data_hash = do_js_hash(0, (unsigned char *)DownloadBin, sizeof(DownloadBin));
    //req.req_hash = do_js_hash(0, (unsigned char *)&req, sizeof(req)-4);
    req.data_hash = 0;

    while((len = read(fw_fd, pBuf, package_size)) > 0){
        req.data_hash = do_js_hash(req.data_hash, (unsigned char *)pBuf, len);
    }
    req.req_hash = do_js_hash(0, (unsigned char *)&req, sizeof(req)-4);

    tcflush(fd, TCIFLUSH);
    tcflush(fd, TCOFLUSH);
    tcflush(fd, TCIOFLUSH);
    len = UartWrite(fd, &req, sizeof(req));

    BT_DBG("%s, to write REQ_TAG:%u, writed:%d, fileSize:%d!", __func__, (unsigned int)sizeof(req), len, fileSize);

    do {
        /*read response*/
        len = UartReadn(fd, (uint8_t *)&resp, sizeof(struct _download_response));
        for (i=0; i<len; i++)
        {
            //BT_DBG("RESP_TAG=[%c],hex= [0x%02x], total: %d, cnt:%d", ((uint8_t*)&resp)[i], ((uint8_t*)&resp)[i], len, count);
        }

        BT_DBG("%s, to Read RESP_TAG:%u, readed:%d!\n", __func__, (unsigned int)sizeof(struct _download_response), len);

        if (resp.tag != *((uint32_t *)RESP_TAG)||(resp.status !=0))
        {
            BT_DBG("RESP0 Error:0x%x!\n", resp.tag);
            //userial_vendor_close();
            while(1);
        }
    } while(0);

    lseek(fw_fd, 0, SEEK_SET);
    size = fileSize;
    BT_DBG("fw download size :%u, start...\n", fileSize);
    while (size)
    {
        len = read(fw_fd, pBuf, package_size);

        i = UartWrite(fd, pBuf, len);
        BT_DBG("fw uart towrite:%u, done:%u\n", len, i);
        /*read response*/
        i = UartReadn(fd, (uint8_t *)&resp, sizeof(struct _download_response));
        BT_DBG("fw read RESP:%u, done:%u\n", (unsigned int)sizeof(struct _download_response), i);
        if (resp.tag != *((uint32_t *)RESP_TAG)||(resp.status !=0))
        {
            BT_DBG("!!!!!!!!RESP1 Error:0x%x!, fileSize:%d, left size:%d, status:%d\n", resp.tag, fileSize, size, resp.status);
            free(pBuf);
            pBuf = NULL;
            close(fw_fd);
            return -1;
        }

        size -= len;
    }

    if (pBuf != NULL){
        free(pBuf);
        pBuf = NULL;
    }

    close(fw_fd);

    //usleep(20 * 1000);
    BT_DBG("UARTBoot download OK!\n");
    return 0;
}

static void util_hexdump(const uint8_t *buf, size_t len)
{
    static const char hexdigits[] = "0123456789abcdef";
    char str[16 * 3];
    size_t i;

    if (!buf || !len)
        return;

    for (i = 0; i < len; i++) {
        str[((i % 16) * 3)] = hexdigits[buf[i] >> 4];
        str[((i % 16) * 3) + 1] = hexdigits[buf[i] & 0xf];
        str[((i % 16) * 3) + 2] = ' ';
        if ((i + 1) % 16 == 0) {
            str[16 * 3 - 1] = '\0';
            BT_INFO("%s", (char*)str);
        }
    }

    if (i % 16 > 0) {
        str[(i % 16) * 3 - 1] = '\0';
        BT_INFO("%s", (char*)str);
    }
}
static int start_xfer_wait(int fd, uint8_t *cmd, uint16_t len, uint32_t msec,
               int retry, uint8_t *resp, uint16_t *resp_len)
{
    uint8_t buf[255];
    int result;
    int state = 1;
    int count = 0;
    int params_len;
    struct pollfd p[2];
    uint16_t opcode;

    if (fd == -1 || !cmd || len < 4) {
        BT_ERROR("%s: invalid parameter", __func__);
        return -1;
    }

    opcode = ((uint16_t)cmd[2] << 8) + cmd[1];

start_xfer:
    result = write(fd, cmd, len);
    if (result != len) {
        BT_ERROR("%s: Write cmd %04x error, %s", __func__, opcode,
               strerror(errno));
        return -1;
    }

start_recv:
    memset(buf, 0, sizeof(buf));
    memset(p, 0, sizeof(p));
    state = 1;
    count = 0;
    p[0].fd = fd;
    p[0].events = POLLERR | POLLHUP | POLLIN;
    for (;;) {
        p[0].revents = 0;
        result = poll(p, 1, msec);
        if (result < 0) {
            BT_ERROR("Poll call error, %s", strerror(errno));
            result = -1;
            break;
        }

        if (result == 0) {
            BT_WARN("%s: Timeout", __func__);
            if (retry <= 0) {
                BT_ERROR("%s: Transfer exhausted", __func__);
                tcflush(fd, TCIOFLUSH);
                exit(EXIT_FAILURE);
                return -1;
            }
            retry--;
            usleep(100 * 1000);
            goto start_xfer;
        }

        if (p[0].revents & (POLLERR | POLLHUP)) {
            BT_ERROR("POLLERR or POLLUP happens, %s",
                   strerror(errno));
            result = -1;
            break;
        }

        if (state == 1) {
            result = read(p[0].fd, buf, 1);
            if (result == -1 || result != 1) {
                BT_ERROR("%s: Read pkt type error, %s", __func__,
                       strerror(errno));
                result = -1;
                break;
            }
            if (result == 1 && buf[0] == 0x04) {
                count = 1;
                state = 2;
            }
        } else if (state == 2) {
            result = read(p[0].fd, buf + count, 2);
            if (result == -1 || result != 2) {
                BT_ERROR("%s: Read pkt header error, %s",
                       __func__, strerror(errno));
                break;
            }
            count += result;
            state = 3;
            params_len = buf[2];
            if (params_len + 3 > sizeof(buf)) {
                result = -1;
                BT_ERROR("%s: hci event too long", __func__);
                break;
            }
        } else if (state == 3) {
            result = read(p[0].fd, buf + count, params_len);
            if (result == -1) {
                BT_ERROR("%s: Read pkt payload error, %s",
                       __func__, strerror(errno));
                break;
            }
            count += result;
            params_len -= result;
            if (!params_len)
                break;
        }
    }

    if (result >= 0) {
        if (buf[1] == 0x0e) {
            uint16_t tmp_opcode;

            tmp_opcode = (uint16_t)buf[4] | buf[5] << 8;
            if (tmp_opcode == opcode) {

				if(opcode != last_opcode){
	                BT_INFO("Cmd complete event for cmd %04x",
                    opcode);
					BT_INFO("Ongoing, please wait ... \n");
					last_opcode = opcode;
				}

                /* Status is not zero indicating command not
                 * succeeded */
                if (buf[6])
                    return -1;
                if (!resp)
                    return 0;
                if (*resp_len > count)
                    *resp_len = count;
                memcpy(resp, buf, *resp_len);
                return 0;
            } else {
                BT_WARN("Unexpected cmd complete event, %04x",
                    tmp_opcode);
                return -1;
            }
        } else {
            BT_INFO("%s: Unexpected hci event packet", __func__);
            util_hexdump(buf, count);
            /* Continue receiving */
        }
        goto start_recv;
    }
    return result;
}

//vendor cmd to change uart speed
//rk962 works in 115200 buad in default.
static int rk_bt_hci_change_stage_baudrate(int fd,int stage,int speed)
{
    uint8_t tx_buf[10];
    uint8_t resp[7];//min size.
    uint16_t rlen = sizeof(resp);
    uint8_t *p = tx_buf;
    int result;

    UINT8_TO_STREAM(p, 0x01);
    if (stage == STAGE_UPDATE_LOADER_BAUDRATE) {
        UINT16_TO_STREAM(p, 0xFC18);
    } else if (stage == STAGE_UPDATE_FW_WORK_BAUDRATE) {
        UINT16_TO_STREAM(p, 0xFC84);
    } else {
        UINT16_TO_STREAM(p, 0xFC18);
    }
    *p++ = 6;
    *p++ = 0;
    *p++ = 0;
    UINT32_TO_STREAM(p,speed);

    result = start_xfer_wait(fd, tx_buf, sizeof(tx_buf), ACK_TIMEOUT_MS, RETRY_TIMES, resp, &rlen);

    if (result < 0) {
        BT_ERROR("rk_bt_hci_change_stage_baudrate failed:%d", result);
        return -1;
    }

    if (rlen != 7) {
        BT_ERROR("%s: rk_bt_hci_change_stage_baudrate length %u", __func__, rlen);
        return -1;
    }

    return 0;
}

static int rk_bt_change_stage_baudrate(int fd, int stage, int speed, struct termios *ti)
{
    if (get_loader_type() >= RK960_D1) {
        if (set_speed(fd, ti, 115200) < 0) {
            BT_ERROR("Can't set baud rate: %d, %d", fd, speed);
            return -1;
        }

        if (rk_bt_hci_change_stage_baudrate(fd, stage, speed) < 0) {
            BT_ERROR("Failed to send HCI change baudrate command");
            return -1;
        }
    }

    if (set_speed(fd, ti, speed) < 0) {
        BT_ERROR("Can't set baud rate: %d, %d", fd, speed);
        return -1;
    }

    return 0;
}

#define MIN(a, b) ((a) < (b) ? (a) : (b))
static int rk_bt_read_local_name(int fd, char *name, int len, int timeout)
{
    unsigned char cmd[] = { 0x01, 0x14, 0x0C, 0x00 };
    //local name max len is 248
    unsigned char resp[248+7];
    uint16_t rlen = sizeof(resp);
    unsigned int name_len;
    int result;

    result = start_xfer_wait(fd, cmd, sizeof(cmd), timeout, RETRY_TIMES, resp, &rlen);

    if (result < 0) {
        BT_ERROR("rk bt read local name failed:%d", result);
        goto fail;
    }

    //Num_HCI_Command_Packts(1B) + Command_Opcode(2B) + Status(1B)
    name_len = resp[2] - 4;

    memcpy(name, (char *) &resp[7], MIN(name_len, len));
    name[MIN(name_len, len) - 1] = 0;

    BT_INFO("chip name:%s", name);
    return 0;

fail:
    return -1;
}

static int rk_bt_read_bdaddr(int fd, char *bdaddr, int timeout)
{
    unsigned char cmd[] = { 0x01, 0x09, 0x10, 0x00 };
    unsigned char resp[6+7];
    uint16_t rlen = sizeof(resp);
    int result;

    if (bdaddr == NULL)
    {
        goto fail;
    }

    result = start_xfer_wait(fd, cmd, sizeof(cmd), timeout, RETRY_TIMES, resp, &rlen);

    if (result < 0) {
        BT_ERROR("rk bt read local name failed:%d", result);
        goto fail;
    }

    memcpy(bdaddr, (char *) &resp[7], 6);
    BT_INFO("bdaddr = 0x%02x:%02x:%02x:%02x:%02x:%02x", bdaddr[5], bdaddr[4], bdaddr[3], bdaddr[2], bdaddr[1], bdaddr[0]);
    return 0;

fail:
    return -1;
}

static int hw_strncmp (const char *p_str1, const char *p_str2, const int len)
{
    int i;

    if (!p_str1 || !p_str2)
        return (1);

    for (i = 0; i < len; i++)
    {
        if (toupper(p_str1[i]) != toupper(p_str2[i]))
            return (i+1);
    }

    return 0;
}

#define FW_PATCHFILE_EXTENSION      ".bin"
#define FW_PATCHFILE_EXTENSION_LEN  4
#define FW_PATCHFILE_PATH_MAXLEN    248 /* Local_Name length of return of
                                           HCI_Read_Local_Name */

#define FW_TABLE_VERSION "v0.1 20210726"

/* RK FW auto detection table */
// typedef struct {
//     char *chip_id;
//     char *updated_chip_id;
// } fw_auto_detection_entry_t;

// static const fw_auto_detection_entry_t fw_auto_detection_table[] = {
//     {"rk962","rk962"},    //rk962
//     { NULL, NULL}
// };

#include <ctype.h>

/*
static char *_strupr(char *str){
    char *orign=str;
    for (; *str!='\0'; str++)
        *str = toupper(*str);
    return orign;
}
*/

static char *_strlow(char *str){
    char *orign=str;
    for (; *str!='\0'; str++)
        *str = tolower(*str);
    return orign;
}

static int rk_bt_fetch_fw(const char *chip_name, char *location)
{
    DIR *dirp;
    struct dirent *dp;
    int filenamelen;
    uint8_t retval = FALSE;
    // fw_auto_detection_entry_t *p_entry;
    char chip_id_fw[32] = {0};
    char dir_name[128] = {0};

    char *rk96x_path = getenv("RK96X_TARGET_PATH");
    if (rk96x_path != NULL) {
        snprintf(dir_name, sizeof(dir_name), "%s/fw", rk96x_path);
        BT_DBG("dir_name = '%s'.", dir_name);
    }
    else {
        BT_ERROR("RK96X_TARGET_PATH is NULL");
        snprintf(dir_name, sizeof(dir_name), "%s", "/vendor/etc/firmware");
        BT_DBG("fw_path is '%s'.", dir_name);
    }

    BT_DBG("Chip name = [%s]", chip_name);
    sprintf(chip_id_fw, "%s", chip_name);
    _strlow(chip_id_fw);

    BT_DBG("###ROCKCHIP FW Auto detection patch version = [%s]###", FW_TABLE_VERSION);
    //p_entry = (fw_auto_detection_entry_t *)fw_auto_detection_table;
    //while (p_entry->chip_id != NULL)
    //{
    //    if (strstr(chip_id_fw, p_entry->chip_id)!=NULL)
    //    {
    //        strcpy(chip_id_fw,p_entry->updated_chip_id);
    //        break;
    //    }
    //    p_entry++;
    //}
    // strcat(chip_id_fw, "_bt");
    //  strcat(chip_id_fw, FW_PATCHFILE_EXTENSION);

    BT_DBG("Updated Target name = [%s], dir_name=%s", chip_id_fw, dir_name);

    if ((dirp = opendir(dir_name)) != NULL)
    {
        /* Fetch next filename in patchfile directory */
        while ((dp = readdir(dirp)) != NULL)
        {
            /* Check if filename starts with chip-id name */
            if ((hw_strncmp(dp->d_name, chip_id_fw, strlen(chip_id_fw)) \
                ) == 0)
            {
                /* Check if it has .bin extenstion */
                filenamelen = strlen(dp->d_name);
                if ((filenamelen >= FW_PATCHFILE_EXTENSION_LEN) &&
                    ((hw_strncmp(
                          &dp->d_name[filenamelen-FW_PATCHFILE_EXTENSION_LEN], \
                          FW_PATCHFILE_EXTENSION, \
                          FW_PATCHFILE_EXTENSION_LEN) \
                     ) == 0))
                {
                    BT_INFO("Found patchfile: [%s][%s]", \
                        dir_name, dp->d_name);

                    /* Make sure length does not exceed maximum */
                    if ((filenamelen + strlen(dir_name)) > \
                         FW_PATCHFILE_PATH_MAXLEN)
                    {
                        BT_ERROR("Invalid patchfile name (too long)");
                    }
                    else
                    {
                        memset(location, 0, PATH_MAX);
                        /* Found patchfile. Store location and name */
                        snprintf(location, PATH_MAX, "%s/%s", dir_name, dp->d_name);
                        BT_DBG("Constructed path: %s", location);
                        retval = TRUE;
                    }
                    break;
                }
            }
        }

        closedir(dirp);
    }
    else
    {
        BT_ERROR("Could not open %s", dir_name);
    }

    return (retval);
}

#define PACK_SIZE 255
#define MAX_SEGS 2
static int rk_bt_fw_probe(int fd, int fw_fd, int fwsize, int *position, int timeout)
{
    uint32_t hash = 0;
    uint16_t length;
    uint32_t n_segs = 0;
    uint8_t tx_buf[1024];
    uint8_t resp[7];//min size.
    uint16_t rlen = sizeof(resp);
    uint8_t *p = tx_buf;
    int result;

    struct seg_t {
        uint32_t vmaddr;
        uint32_t length;
        } segs[MAX_SEGS];

    if ((length = read(fw_fd, &n_segs, 4)) < 0) {
        BT_ERROR("Failed to read number of segments.");
        return -1;
    }

    for(int i = 0; i < n_segs && i < MAX_SEGS; i++){
        read(fw_fd, &segs[i].vmaddr, 4);
        read(fw_fd, &segs[i].length, 4);
    }

    if (segs[0].length + segs[1].length + 20 != fwsize) {
        if (n_segs > MAX_SEGS) {
            n_segs = 2;
            segs[0].vmaddr = RK96X_FW_SRAM_BASE;
            segs[0].length = fwsize/2;
            segs[1].vmaddr = RK96X_FW_SRAM_BASE + segs[0].length;
            segs[1].length = fwsize - segs[0].length;

            *position = lseek(fw_fd, 0, SEEK_SET);
        }
        else
        {
            segs[1].length = fwsize - 20 - segs[0].length;
        }
    }

    *position = lseek(fw_fd, 0, SEEK_CUR);

    while((length = read(fw_fd, p, PACK_SIZE)) > 0){
        hash = do_js_hash(hash, (unsigned char *)p, length);
        //BT_DBG("do_js_hash , count=%04d", count);
    }

    UINT8_TO_STREAM(p, 0x01);
    UINT16_TO_STREAM(p, 0xFC2D);
    *p++ = (4 + (n_segs)*(4+4) + 4); /* parameter length: segs + (segs) * (addr size) + hash*/
    UINT32_TO_STREAM(p, n_segs);

    for(uint32_t i = 0; i < n_segs && i < MAX_SEGS; i++){
        UINT32_TO_STREAM(p, segs[i].vmaddr);
        UINT32_TO_STREAM(p, segs[i].length);
    }

    UINT32_TO_STREAM(p, hash);//hash

    tx_buf[3] = (4 + (n_segs)*(4+4) + 4); /* parameter length: segs + (segs) * (addr size) + hash*/

    result = start_xfer_wait(fd, tx_buf, 3+1+(4 + (n_segs)*(4+4) + 4), timeout, RETRY_TIMES, resp, &rlen);

    if (result < 0) {
        BT_ERROR("probe patch failed");
        return -1;
    }

    if (rlen != 7) {
        BT_ERROR("%s: Unexpected length %u", __func__, rlen);
        return -1;
    }

    return 0;
}

static int rk_bt_fw_transfer(int fd, int fw_fd, int timeout)
{
    uint8_t tx_buf[1024] = {0x01, 0x2e, 0xfc, 0x00};
    uint8_t resp[7];//min size.
    uint16_t rlen = sizeof(resp);
    uint8_t *p = tx_buf;
    uint8_t fragment;
    uint16_t index =0;
    int result;

    while((fragment = read(fw_fd, p+4, PACK_SIZE))>0)
    {
        //UINT8_TO_STREAM(p, 0x01);
        //UINT16_TO_STREAM(p, 0xFC2E);
        //UINT8_TO_STREAM(p, fragment);
        tx_buf[0] = 0x01;
        tx_buf[1] = 0x2e;
        tx_buf[2] = 0xfc;
        tx_buf[3] = fragment;

		if(fragment != last_fragment){
            BT_DBG("fragment = %d", fragment);
			BT_DBG("Ongoing, please wait ... \n");
			last_fragment = fragment;
		}

        result = start_xfer_wait(fd, tx_buf, 4 + fragment, timeout, RETRY_TIMES, resp, &rlen);
        if (result < 0) {
            BT_ERROR("Transfer fragment failed, index %d", index);
            return -1;
        }

        if (rlen != 7) {
            BT_ERROR("%s: Unexpected length %u", __func__, rlen);
            return -1;
        }
        index++;
    }

    char* timeStr = getCurrentTime();
    BT_DBG("%s: fw download OK. Time: %s", __func__, timeStr);
    free(timeStr);
    return 0;
}

static int rk_bt_verify_fw_version(const int fw_fd, const int begin)
{
    fw_version_t fw_ver;
    fw_version_t rom_ver;
    int fw_ver_offset = 0;

    rom_ver = get_rom_verison();
    if (rom_ver.major == 1 && rom_ver.minor == 1)
    {
        fw_ver_offset = begin + RK96X_FW_VERSION_ADDR_V1_1;
    }
    else
    {
        fw_ver_offset = begin + RK96X_FW_VERSION_ADDR;
    }
    lseek(fw_fd, fw_ver_offset, SEEK_SET);
    if (read(fw_fd, &fw_ver, sizeof(fw_ver)) < sizeof(fw_ver))
    {
        BT_ERROR("%s read fw_ver faild", __func__);
        return -1;
    }

    BT_INFO("fw_ver[%d.%d] rom_ver[%d.%d]", fw_ver.major, fw_ver.minor, rom_ver.major, rom_ver.minor);

    if ((fw_ver.major == rom_ver.major) && (fw_ver.minor == rom_ver.minor))
    {
        return 0;
    }
    else
    {
        BT_ERROR("error: rom version not match");
        exit(-1);
    }
}

static int rk_bt_verify_fw_svn(int fw_fd, int position, int svn_limit)
{
    char svn_str[8] = {0};
    int svn;
    lseek(fw_fd, position, SEEK_SET);
    if (read(fw_fd, svn_str, sizeof(svn_str)) == 0)
    {
        BT_ERROR("%s read svn_str faild", __func__);
        return -1;
    }

    if (svn_str[7] != '\0')
    {
        svn_str[7] = '\0';
        BT_ERROR("svn number too long");
    }

    svn = atoi(svn_str);
    if (svn >= svn_limit)
    {
        BT_INFO("%s svn [%d] is ok", __func__, svn);
        return 0;
    }
    else
    {
        BT_ERROR("svn [%d] too old, need update to [%d]", svn, svn_limit);
        return -1;
    }
}

static int rk_bt_check_fw_svn_version(int fw_fd, int begin, const char * fw_path)
{
    fw_verify_t *fw = fw_verify_table;
    while (fw->name)
    {
        if (!strstr(fw_path, fw->name))
        {
            fw++;
            continue;
        }

	BT_INFO("check fw %s", fw->name);

        if (fw->is_check_rom)
        {
            rk_bt_verify_fw_version(fw_fd, begin);
        }

        if (fw->is_check_svn)
        {
            return rk_bt_verify_fw_svn(fw_fd, begin+fw->svn_off, fw->svn_limit);
        }

	BT_INFO("%s %s complete!", fw_path, __func__);
	return 0;
    }

    BT_WARN("cannot verify svn number");
    return -1;
}

static int rk_bt_fw_download(int fd, const char *fw_whole)
{
    struct stat st;
    uint32_t fwSize;
    int position;
    int fwfd;

    if (stat(fw_whole, &st) < 0){
        BT_ERROR("Can't access firmware:%s, errno:%s", fw_whole, strerror(errno));
        return -1;
    }

    fwSize = st.st_size;
    if ((fwfd = open(fw_whole, O_RDONLY)) == -1){
        BT_ERROR("vendor lib preload failed to open [%s]", fw_whole);
        return -1;
    }
    else{
        rk_bt_fw_probe(fd, fwfd, fwSize, &position, 1000);
        BT_INFO("lseek position:%d", position);
        rk_bt_check_fw_svn_version(fwfd, position, fw_whole);
        lseek(fwfd, position, SEEK_SET);
        rk_bt_fw_transfer(fd, fwfd, 1000);
        close(fwfd);
        fwfd = -1;
    }

    return 0;
}

static int str_to_bda(const char *string, uint8_t *addr) {
    assert(string != NULL);
    assert(addr != NULL);

    int ret = sscanf(string, "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx",
        &addr[5], &addr[4], &addr[3], &addr[2], &addr[1], &addr[0]);

    BT_INFO("bdaddr = 0x%02x:%02x:%02x:%02x:%02x:%02x", addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);
    return ret;
}

static int bda_to_str(char *string, const uint8_t *addr) {
    assert(string != NULL);
    assert(addr != NULL);

    int ret = sprintf(string, "%02x:%02x:%02x:%02x:%02x:%02x",
        addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);

    BT_DBG("%s %d bdaddr = %s", __func__, ret, string);
    if (ret > 0)
    {
        ret ++;//\0
    }
    return ret;
}

static int rk_bt_set_launch_addr(int fd, uint32_t lunchAddr, int timeout)
{
    uint8_t tx_buf[] = {0x01, 0x85, 0xfc, 0x04, 0x00, 0x00, 0x00, 0x00};
    uint8_t resp[7];//min size.
    uint16_t rlen = sizeof(resp);
    uint8_t *p = tx_buf;
    int result;

    UINT8_TO_STREAM(p, 0x01);
    UINT16_TO_STREAM(p, 0xFC85);
    *p++ = SET_FW_RUN_ADDR_SIZE;
    UINT32_TO_STREAM(p, lunchAddr);

    result = start_xfer_wait(fd, tx_buf, sizeof(tx_buf), timeout, RETRY_TIMES, resp, &rlen);

    if (result < 0) {
        BT_ERROR("set lunchaddr failed:%d", result);
        return -1;
    }

    if (rlen != 7) {
        BT_ERROR("%s: write bdaddr length %u", __func__, rlen);
        return -1;
    }

    return 0;
}


static int rk_bt_write_nvds_addr(int fd, const char *bdaddr, int timeout)
{
    uint8_t tx_buf[4+sizeof(hci_op_par_cmd_t)] = { 0x01, 0x0a, 0xfc, 130, 0x01, 0x06,
                0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff };
    uint8_t resp[7];//min size.
    uint16_t rlen = sizeof(resp);
    uint8_t *p = tx_buf;
    int result;

    hci_op_par_cmd_t wr_nvds_paras;
    UINT8_TO_STREAM(p, 0x01);
    UINT16_TO_STREAM(p, 0xFC0A);
    *p++ = sizeof(hci_op_par_cmd_t);

    memset((uint8_t*)&wr_nvds_paras, 0x00, sizeof(hci_op_par_cmd_t));
    wr_nvds_paras.param_tag = PARAM_ID_BD_ADDRESS;
    wr_nvds_paras.buf.length = PARAM_LEN_BD_ADDRESS;
    if(bdaddr != NULL)
        str_to_bda(bdaddr, (uint8_t *)(wr_nvds_paras.buf.data));//&tx_buf[4]);

    memcpy(p, (uint8_t*)&wr_nvds_paras, sizeof(hci_op_par_cmd_t));

    result = start_xfer_wait(fd, tx_buf, sizeof(tx_buf), timeout, RETRY_TIMES, resp, &rlen);

    if (result < 0) {
        BT_ERROR("write bdaddr failed:%d", result);

        return -1;
    }

    if (rlen != 7) {
        BT_ERROR("%s: write bdaddr length %u", __func__, rlen);
        return -1;
    }

    return 0;
}

static int rk_bt_write_addr(int fd, const char *bdaddr, int timeout)
{
    uint8_t tx_buf[4+sizeof(hci_op_par_cmd_t)] = { 0x01, 0x0a, 0xfc, 130, 0x01, 0x06,
                0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff };
    uint8_t resp[7];//min size.
    uint16_t rlen = sizeof(resp);
    uint8_t *p = tx_buf;
    int result;

    hci_op_par_cmd_t wr_nvds_paras;
    UINT8_TO_STREAM(p, 0x01);
    UINT16_TO_STREAM(p, 0xFC0A);
    *p++ = sizeof(hci_op_par_cmd_t);

    memset((uint8_t*)&wr_nvds_paras, 0x00, sizeof(hci_op_par_cmd_t));
    wr_nvds_paras.param_tag = PARAM_ID_BD_ADDRESS;
    wr_nvds_paras.buf.length = PARAM_LEN_BD_ADDRESS;

    if(bdaddr != NULL)
    {
        wr_nvds_paras.buf.data[0] = bdaddr[0];
        wr_nvds_paras.buf.data[1] = bdaddr[1];
        wr_nvds_paras.buf.data[2] = bdaddr[2];
        wr_nvds_paras.buf.data[3] = bdaddr[3];
        wr_nvds_paras.buf.data[4] = bdaddr[4];
        wr_nvds_paras.buf.data[5] = bdaddr[5];
    }

    memcpy(p, (uint8_t*)&wr_nvds_paras, sizeof(hci_op_par_cmd_t));

    result = start_xfer_wait(fd, tx_buf, sizeof(tx_buf), timeout, RETRY_TIMES, resp, &rlen);

    if (result < 0) {
        BT_ERROR("write bdaddr failed:%d", result);

        return -1;
    }

    if (rlen != 7) {
        BT_ERROR("%s: write bdaddr length %u", __func__, rlen);
        return -1;
    }

    return 0;
}

static int rk_bt_sco_data_path_config(int fd, uint8_t sco_data_path_config, int timeout)
{
    uint8_t tx_buf[4 + sizeof(hci_op_par_cmd_t)] = { 0x01, 0x0a, 0xfc, 130, 0x2C, 0x02,
                                                     0x00, 0x00
                                                   };
    uint8_t resp[7];//min size.
    uint16_t rlen = sizeof(resp);
    uint8_t *p = tx_buf;
    int result;

    hci_op_par_cmd_t wr_nvds_paras;
    UINT8_TO_STREAM(p, 0x01);
    UINT16_TO_STREAM(p, 0xFC0A);
    *p++ = sizeof(hci_op_par_cmd_t);

    memset((uint8_t *)&wr_nvds_paras, 0x00, sizeof(hci_op_par_cmd_t));
    wr_nvds_paras.param_tag = PARAM_ID_SYNC_CONFIG;
    wr_nvds_paras.buf.length = PARAM_LEN_SYNC_CONFIG;
    wr_nvds_paras.buf.data[0] = sco_data_path_config;
    wr_nvds_paras.buf.data[1] = sco_data_path_config;

    memcpy(p, (uint8_t *)&wr_nvds_paras, sizeof(hci_op_par_cmd_t));

    result = start_xfer_wait(fd, tx_buf, sizeof(tx_buf), timeout, RETRY_TIMES, resp, &rlen);

    if (result < 0)
    {
        BT_ERROR("set sync config failed:%d", result);

        return -1;
    }

    if (rlen != 7)
    {
        BT_ERROR("%s: set sync config length %u", __func__, rlen);
        return -1;
    }

    return 0;
}

static int rk_bt_hci_reset(int fd, int timeout)
{
    int result;
    uint8_t cmd[4] = { 0x01, 0x03, 0x0c, 0x00};

    BT_INFO("%s: send hci reset cmd", __func__);

    result = start_xfer_wait(fd, cmd, sizeof(cmd), timeout, RETRY_TIMES, NULL, 0);
    if (result < 0) {
        BT_ERROR("%s: Failed to send reset cmd", __func__);
        return -1;
    }

    return 0;
}

static int rk_bt_hci_lpm_enable(int fd, int turn_on, int timeout)
{
    int result;
    uint8_t cmd[4 + sizeof(bt_lpm_param_t)] = { 0x01, 0x83, 0xfc, 0x00};

    BT_INFO("%s: send hci lpm cmd", __func__);

    cmd[3] = sizeof(bt_lpm_param_t);

    if (turn_on)
    {
        memcpy(&cmd[4], &lpm_param, sizeof(bt_lpm_param_t));
    }
    else
    {
        memset(&cmd[4], 0, sizeof(bt_lpm_param_t));
    }

    result = start_xfer_wait(fd, cmd, sizeof(cmd), timeout, RETRY_TIMES, NULL, 0);
    if (result < 0) {
        BT_ERROR("%s: Failed to send reset cmd", __func__);
        return -1;
    }

    return 0;
}

static int rk_bt_hci_voice_settings_set(int fd, uint16_t vohci_param, int timeout)
{
    int result;
    uint8_t cmd[4 + sizeof(uint16_t)] = { 0x01, 0x26, 0x0c, 0x00};

    BT_INFO("%s: send hci voice setting 0x%x", __func__, vohci_param);

    cmd[3] = sizeof(uint16_t);
    cmd[4] = vohci_param & 0xFFul;
    cmd[5] = vohci_param >> 8;

    result = start_xfer_wait(fd, cmd, sizeof(cmd), timeout, RETRY_TIMES, NULL, 0);
    if (result < 0) {
        BT_ERROR("%s: Failed to send reset cmd", __func__);
        return -1;
    }

    return 0;
}

static int rk_bt_set_board_config(int fd, int flow, int pm, int timeout)
{
    int result;
    uint8_t cmd[4 + sizeof(board_Config_Set_t)] = { 0x01, 0x97, 0xfc, 0x00};

    BT_INFO("%s: send board config cmd", __func__);

    cmd[3] = sizeof(board_Config_Set_t);

    if (pm)
    {
        if (!flow)
        {
            BT_ERROR("%s: sleep enable need flow ctrl eanble", __func__);
        }

        g_board_config.mask.sleep_enable = 1;
        g_board_config.config.sleep_enable = 1;
    }
    else
    {
        g_board_config.mask.sleep_enable = 1;
        g_board_config.config.sleep_enable = 0;
    }

    if (flow)
    {
        g_board_config.mask.flow_ctrl_en = 1;
        g_board_config.config.flow_ctrl_en = 1;
    }
    else
    {
        g_board_config.mask.flow_ctrl_en = 1;
        g_board_config.config.flow_ctrl_en = 0;
    }

    memcpy(&cmd[4], &g_board_config, sizeof(board_Config_Set_t));

    result = start_xfer_wait(fd, cmd, sizeof(cmd), timeout, RETRY_TIMES, NULL, 0);
    if (result < 0) {
        BT_ERROR("%s: Failed to send reset cmd", __func__);
        return -1;
    }

    return 0;
}

static int rk_bt_parse_rom_version(const char *chip_name, fw_version_t *prom_ver)
{
    const char *p = chip_name;
    if (chip_name == NULL)
    {
        return -1;
    }
    //RK960 V1.1
    while (*p != 'V')
    {
        if (*p == '\0')
        {
            goto no_ver;
        }
        p++;
    }
    p++;//skip 'V'
    prom_ver->major = 0;
    while (*p != '.')
    {
        if (*p == '\0')
        {
            goto no_ver;
        }
        prom_ver->major *= 10;
        prom_ver->major += *p - '0';
        p++;
    }
    p++; //skip '.'
    prom_ver->minor = 0;
    while (*p != '\0')
    {
        prom_ver->minor *= 10;
        prom_ver->minor += *p - '0';
        p++;
    }
    return 0;
no_ver:
    prom_ver->minor = 0;
    prom_ver->major = 0;
    prom_ver->patch = 0;
    return -1;
}

static void rk_bt_update_fw_name_and_type(const char *chip_name)
{
    fw_version_t rom_ver;

    if (rk_bt_parse_rom_version(chip_name, &rom_ver) < 0)
    {
        BT_ERROR("No valid version read from loader!");
        return;
    }
    set_rom_verison(rom_ver);
    if (hw_strncmp(chip_name, "rk960", strlen("rk960")) == 0)
    {
        if (rom_ver.major == 1 && rom_ver.minor == 1)
        {
            fw1_name = RK960_FW1;
            fw2_name = RK960_FW2;
            set_loader_type(RK960_E1);
        }
        else
        {
            fw1_name = RK960_FW1_V2;
            fw2_name = RK960_FW2_V2;
            set_loader_type(RK960_E2);
        }
    }
    else
    {
        if (rom_ver.major == 1 && rom_ver.minor == 1)
        {
            fw1_name = RK962_FW1;
            fw2_name = RK962_FW2;
            set_loader_type(RK962_E1);
        }
        else
        {
            fw1_name = RK962_FW1_V2;
            fw2_name = RK962_FW2_V2;
            set_loader_type(RK962_E2);
        }
    }
}

#define BT_USER_CONFIG "/data/bt_user.conf"
#ifdef BT_USER_CONFIG
#define MAC_LINE_LENGTH 64
#define MAC_KEY_LENGTH 7
#define MAC_KEY_NAME "bt_addr"
#define TMP_CONFIG "/data/bt_user.conf.tmp"

static int rk96x_read_bdaddr_from_config(char *bdaddr)
{
    char line[MAC_LINE_LENGTH] = {0};
    char* value = NULL;

    FILE* file = fopen(BT_USER_CONFIG, "r");
    if (!file) {
        perror("Failed to open config file");
        return -1;
    }

    while (fgets(line, sizeof(line), file)) {
        if (hw_strncmp(line, MAC_KEY_NAME, MAC_KEY_LENGTH) == 0)
        {
            strtok(line, "=");
            value = strtok(NULL, "=");
            break;
        }
        else
        {
            memset(line, 0, sizeof(line));
        }
    }

    fclose(file);
    if (NULL != value)
    {
        if (str_to_bda(value, (uint8_t *)bdaddr) != BD_ADDR_LEN)
        {
            memset(bdaddr, 0, BD_ADDR_LEN);
        }
    }
    return 0;
}

static int copy_file(const char *source, const char *destination)
{
    char line[MAC_LINE_LENGTH] = {0};
    FILE *src = fopen(source, "r");
    if (src == NULL)
    {
        perror("Error opening source file");
        return -1;
    }

    FILE *dst = fopen(destination, "w");
    if (dst == NULL)
    {
        perror("Error opening destination file");
        fclose(src);
        return -1;
    }

    while (fgets(line, sizeof(line), src))
    {
        if (fputs(line, dst) == EOF)
        {
            perror("Error writing to destination file");
            fclose(src);
            fclose(dst);
            remove(destination);
            return -1;
        }
    }

    fclose(src);
    fclose(dst);
    return 0;
}

static int rk96x_write_bdaddr_to_config(char *bdaddr)
{
    char line[MAC_LINE_LENGTH] = {0};
    FILE* tmp_file = NULL;
    FILE* file = fopen(BT_USER_CONFIG, "r");
    if (!file)
    {
        perror("Failed to open config file");
        return -1;
    }

    tmp_file = fopen(TMP_CONFIG, "w");
    if (!tmp_file)
    {
        perror("Failed to open tmp config file");
        return -1;
    }

    while (fgets(line, sizeof(line), file))
    {
        if (hw_strncmp(line, MAC_KEY_NAME, MAC_KEY_LENGTH) == 0)
        {
            continue;
        }
        else
        {
            fprintf(tmp_file, "%s", line);
        }
    }
    fclose(file);

    bda_to_str(line, (const uint8_t *)bdaddr);
    fprintf(tmp_file, "%s=%s\n", MAC_KEY_NAME, line);
    fclose(tmp_file);

    if (copy_file(TMP_CONFIG, BT_USER_CONFIG) < 0)
    {
        return -1;
    }

    BT_INFO("%s success!", __func__);
    return 0;
}
#endif

static bool is_valid_ether_addr(const char *addr)
{
    const char null_bdaddr[BD_ADDR_LEN] = {0,0,0,0,0,0};
    const char multicast_bdaddr[BD_ADDR_LEN] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

    if (memcmp(addr, null_bdaddr, sizeof(null_bdaddr)) && memcmp(addr, multicast_bdaddr, sizeof(multicast_bdaddr)))
    {
        return TRUE;
    }
    else
    {
        return FALSE;
    }
}

static void eth_random_addr(char *addr)
{
    uint32_t random;
    struct  timeval    tv;
    struct  timezone   tz;
    addr[5] = 0x22;
    addr[4] = 0x22;
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd == -1)
    {
        perror("Failed to open /dev/urandom");
        gettimeofday(&tv,&tz);
        random = tv.tv_usec;
    }
    else
    {
        if (read(fd, &random, sizeof(random)) != sizeof(random))
        {
            perror("Failed to read from /dev/urandom");
            gettimeofday(&tv,&tz);
            random = tv.tv_usec;
        }
        close(fd);
    }

    addr[3]   = (uint8_t) (random & 0xFF);
    addr[2]   = (uint8_t) ((random >> 8) & 0xFF);
    addr[1] = (uint8_t) ((random >> 16) & 0xFF);
    addr[0] = (uint8_t) ((random >> 24) & 0xFF);
}

int rk_bt_init(int fd, struct uart_t *u, struct termios *ti)
{
    char chip_name[FW_PATCHFILE_PATH_MAXLEN] = {0};
    char fw_path[PATH_MAX];
    char tmp_bdaddr[BD_ADDR_LEN] = {0};
    int speed = u->speed;
    const char *bdaddr = u->bdaddr;
    int loader_mode = u->loader_mode;
    int flag = u->flags;
    int flow = flag & FLOW_CTL;
    int vohci = flag & AUDIO_VOHCI;
    int pm = flag & ENABLE_PM;
    //reset_bluetooth();
    check_loader_type(RK96X_LOADER_NAME);
    rk_bt_boot(fd, RK96X_LOADER_NAME);
    usleep(200*1000);
    if (loader_mode == 1){
        BT_INFO("******DownLoad NetCfg Firmware******\n");

        if (vohci)
        {
            rk_bt_sco_data_path_config(fd, AUDIO_VOICE_OVER_HCI, ACK_TIMEOUT_MS);
        }
        else
        {
            rk_bt_sco_data_path_config(fd, AUDIO_HW_PCM, ACK_TIMEOUT_MS);
        }

        if (get_loader_type() > RK960_D1){
            rk_bt_change_stage_baudrate(fd, STAGE_UPDATE_LOADER_BAUDRATE, speed, ti);
            if (bdaddr)
            {
                rk_bt_write_nvds_addr(fd, bdaddr, ACK_TIMEOUT_MS);
            }
            else
            {
                rk_bt_read_bdaddr(fd, tmp_bdaddr, ACK_TIMEOUT_MS);
                if (!is_valid_ether_addr(tmp_bdaddr))
                {
                    #ifdef BT_USER_CONFIG
                    rk96x_read_bdaddr_from_config(tmp_bdaddr);
                    #endif
                    if (!is_valid_ether_addr(tmp_bdaddr))
                    {
                        eth_random_addr(tmp_bdaddr);
                        assert(is_valid_ether_addr(tmp_bdaddr));
                        #ifdef BT_USER_CONFIG
                        rk96x_write_bdaddr_to_config(tmp_bdaddr);
                        #endif
                    }
                    rk_bt_write_addr(fd, tmp_bdaddr, ACK_TIMEOUT_MS);
                }
            }
            rk_bt_set_launch_addr(fd, RK96X_FW_SRAM_BASE, ACK_TIMEOUT_MS);
            rk_bt_read_local_name(fd, chip_name, sizeof(chip_name), ACK_TIMEOUT_MS);
            rk_bt_update_fw_name_and_type(chip_name);
            if (rk_bt_fetch_fw(fw1_name, fw_path) == FALSE){
                BT_ERROR("Failed to fetch firmware %s, path [%s]", fw1_name, fw_path);
            }
            else{
                rk_bt_fw_download(fd, fw_path);
            }
            usleep(RK962_FW1_EXECUTION_TIME * 1000);
        }

        rk_bt_change_stage_baudrate(fd, STAGE_UPDATE_LOADER_BAUDRATE, speed, ti);
        if (get_loader_type() > RK960_D1){
            rk_bt_set_launch_addr(fd, RK96X_FW_SRAM_BASE, ACK_TIMEOUT_MS);
        }

        if (get_loader_type() >= RK962_E2)
        {
            rk_bt_set_board_config(fd, flow, pm, ACK_TIMEOUT_MS);
        }
        rk_bt_read_local_name(fd, chip_name, sizeof(chip_name), ACK_TIMEOUT_MS);
        if (rk_bt_fetch_fw(fw2_name, fw_path) == FALSE){
            BT_ERROR("Failed to fetch firmware %s, path [%s]", fw2_name, fw_path);
        }
        else{
            rk_bt_fw_download(fd, fw_path);
        }
    }
    else if(loader_mode == 2)
    {
        BT_INFO("******DownLoad Standard Firmware******\n");
        rk_bt_change_stage_baudrate(fd, STAGE_UPDATE_LOADER_BAUDRATE, speed, ti);
        if (get_loader_type() > RK960_D1){
            rk_bt_set_launch_addr(fd, RK96X_FW_SRAM_BASE, ACK_TIMEOUT_MS);
        }
        rk_bt_read_local_name(fd, chip_name, sizeof(chip_name), ACK_TIMEOUT_MS);

        if (rk_bt_fetch_fw(RK962_FW3, fw_path) == FALSE){
            BT_ERROR("Failed to fetch firmware %s, path [%s]", RK962_FW3, fw_path);
        }
        else{
            rk_bt_fw_download(fd, fw_path);
        }
    }else{
        BT_INFO("Currently, firmware download is not supported!\n");
    }

    if (loader_mode == 1)
    {
        usleep(200 * 1000);
        uart_set_hw_fctrl(fd, flow);

        if (flow) {
            ti->c_cflag |= CRTSCTS;
        } else {
            ti->c_cflag &= ~CRTSCTS;
        }
        rk_bt_change_stage_baudrate(fd, STAGE_UPDATE_FW_WORK_BAUDRATE, speed, ti);
        usleep(200 * 1000);
        rk_bt_hci_reset(fd, ACK_TIMEOUT_MS);
        rk_bt_hci_voice_settings_set(fd, (uint16_t)u->sco_para, ACK_TIMEOUT_MS);
        rk_bt_hci_lpm_enable(fd, pm, ACK_TIMEOUT_MS);
    }
    else
    {
        exit(0);
    }

    BT_INFO("rk96x init sucess!!");
    return 0;
}
