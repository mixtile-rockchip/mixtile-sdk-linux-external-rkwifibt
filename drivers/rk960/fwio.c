/*
 * Copyright (c) 2022, Fuzhou Rockchip Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/vmalloc.h>
#include <linux/sched.h>
#include <linux/firmware.h>

#include "rk960.h"
#include "hwio.h"
#include "hwbus.h"
#include "debug.h"
#include "bh.h"

#include "fwio.h"
//#include "fw_data.h"

#define RK96x_LOADER_NAME "rk96x_wifi_loader.bin"

#define RK962_EFUSE     0x0962

#define RK960_FW1       "rk960_wifi_rf.bin"
#define RK960_FW2       "rk960_wifi.bin"
#define RK960_FW1_V2    "rk960_wifi_rf_v2.bin"
#define RK960_FW2_V2    "rk960_wifi_v2.bin"
#define RK96x_SDD       "rk96x_sdd.txt"
#define RK962_FW1       "rk962_wifi_rf.bin"
#define RK962_FW2       "rk962_wifi.bin"
#define RK962_FW1_V2    "rk962_wifi_rf_v2.bin"
#define RK962_FW2_V2    "rk962_wifi_v2.bin"
#define RK962_FW2_ALIAS "rk962_fw.bin"

enum {
    RK960_C1 = 0,
    RK960_D1,
    RK96X_E1,
    RK96X_E2
};
typedef struct
{
    uint16_t patch;
    uint8_t minor;
    uint8_t major;
}fw_version_t;

static fw_version_t rom_version;
static int loader_type = RK96X_E1;

struct io_cmd {
	u32 id;
	u32 length;
	u32 addr;
	u32 code_copy_addr;
#ifdef FW_DOWNLOAD_CHECK
	u32 hash_value;
#endif
};

/* Board config structure for rk962 */
typedef struct
{
	/* Bit mapping must match bootloader's board_Config_t structure */
	u32 res0_0:3;				// bits 0-2: reserved (bootloader: gpio1a1)
	u32 clk_32k_sel:2;			// bits 3-4: 0:gpll_div, 1:external 32K crystal, 2:in-chip 32K clock
	u32 wdt_en:1;				// bit 5: 0:disable wdt, 1:enable wdt
	u32 res0_6:2;				// bits 6-7: reserved (bootloader: gpio0_int_en, mailbox_en)
	u32 wdt_timeout:8;			// bits 8-15: wdt timeout for seconds, actual reset time >= wdt_timeout * 2
	u32 res0_16:3;				// bits 16-18: reserved (bootloader: use_sdio_data_copy_optimization, config_wlan_mcu_jtag_enable, config_dbg_bus_en)
	u32 trace_timestamp_en:1;	// bit 19: 0:log without timestamp, 1:log with timestamp
	u32 res0_20:12;				// bits 20-31: reserved (bootloader: use_rx_beacon_abort_optimization, group_key_update_en, device_do_association, tcp_connect_server_en, device_support_DHCP, device_wifi_only_en, gpio2_int_en, reserved)
	u32 res1;				    // bits 32-63: reserved (bootloader: reserved2)
} board_config_t;

typedef struct {
	board_config_t mask;
	board_config_t config;
} board_config_set_t;

static const char * fw1_name;
static const char * fw2_name;

static s8 rk960_atoi(u8 * s)
{
	int num = 0, flag = 0;
	int i;

	for (i = 0; i <= strlen(s); i++) {
		if (s[i] >= '0' && s[i] <= '9')
			num = num * 10 + s[i] - '0';
		else if (s[0] == '-' && i == 0)
			flag = 1;
		else
			break;
	}

	if (flag == 1)
		num = num * -1;

	return num;
}

static int rk960_comma_del(u8 * s, int size)
{
	int i, num = 0;

	for (i = 0; i < size; i++) {
		if (s[i] == ',') {
			s[i] = 0;
			num++;
		}
	}
	return num;
}

#define RK960_TXPWR_COMP_GET(n)  \
    if (rk960_comma_del(p, strlen(p)) != 13) {          \
        result = -1;                                    \
        break;                                          \
    }                                                   \
    for (i = 0; i < 14; i++) {                          \
        hw_priv->txpwr_comp_tbl[i][n] = rk960_atoi(p);  \
        p += strlen(p) + 1;                             \
    }

int rk960_sdd_parse(struct rk960_common *hw_priv, u8 * sdd, int size)
{
	u8 *line = sdd;
	u8 *p;
	int offset = 0;
	int result = 0;
	int i;
        int flag = 0;

        if (!sdd || !size)
                return 0;

	while (1) {
		if (offset + 2 >= size)
			break;
		if (sdd[offset] == 0xd && sdd[offset + 1] == 0xa) {
			sdd[offset] = 0;
			sdd[offset + 1] = 0;

			// get a line to parse
			if (line[0] == '#') {
				//
			} else if (strstr(line, "txpwr_dsss_comp=") != NULL) {
				p = line + strlen("txpwr_dsss_comp=");
				RK960_TXPWR_COMP_GET(0);
                                flag |= BIT(0);
			} else if (strstr(line, "txpwr_cck_comp=") != NULL) {
				p = line + strlen("txpwr_cck_comp=");
				RK960_TXPWR_COMP_GET(1);
                                flag |= BIT(1);
			} else if (strstr(line, "txpwr_bpsk_12_comp=") != NULL) {
				p = line + strlen("txpwr_bpsk_12_comp=");
				RK960_TXPWR_COMP_GET(2);
                                flag |= BIT(2);
			} else if (strstr(line, "txpwr_bpsk_34_comp=") != NULL) {
				p = line + strlen("txpwr_bpsk_34_comp=");
				RK960_TXPWR_COMP_GET(3);
                                flag |= BIT(3);
			} else if (strstr(line, "txpwr_qpsk_12_comp=") != NULL) {
				p = line + strlen("txpwr_qpsk_12_comp=");
				RK960_TXPWR_COMP_GET(4);
                                flag |= BIT(4);
			} else if (strstr(line, "txpwr_qpsk_34_comp=") != NULL) {
				p = line + strlen("txpwr_qpsk_34_comp=");
				RK960_TXPWR_COMP_GET(5);
                                flag |= BIT(5);
			} else if (strstr(line, "txpwr_16qam_12_comp=") != NULL) {
				p = line + strlen("txpwr_16qam_12_comp=");
				RK960_TXPWR_COMP_GET(6);
                                flag |= BIT(6);
			} else if (strstr(line, "txpwr_16qam_34_comp=") != NULL) {
				p = line + strlen("txpwr_16qam_34_comp=");
				RK960_TXPWR_COMP_GET(7);
                                flag |= BIT(7);
			} else if (strstr(line, "txpwr_64qam_23_comp=") != NULL) {
				p = line + strlen("txpwr_64qam_23_comp=");
				RK960_TXPWR_COMP_GET(8);
                                flag |= BIT(8);
			} else if (strstr(line, "txpwr_64qam_34_comp=") != NULL) {
				p = line + strlen("txpwr_64qam_34_comp=");
				RK960_TXPWR_COMP_GET(9);
                                flag |= BIT(9);
			} else if (strstr(line, "txpwr_64qam_56_comp=") != NULL) {
				p = line + strlen("txpwr_64qam_56_comp=");
				RK960_TXPWR_COMP_GET(10);
                                flag |= BIT(10);
			}

			line = sdd + offset + 2;
		}
		offset += 1;
	}

        if (flag != 0x7ff)
                return -1;
        
	return result;
}

void rk960_free_firmware_buf(struct firmware_info *fw_info)
{
	RK960_DEBUG_FW("%s\n", __func__);

#ifdef FW_LOADER_FROM_FOPEN
	if (fw_info->sdd_data)
		vfree(fw_info->sdd_data);
	fw_info->sdd_data = NULL;
#else
        if (fw_info->loder_data_r)
                release_firmware(fw_info->loder_data_r);
        if (fw_info->fw_data_r)
                release_firmware(fw_info->fw_data_r);
        if (fw_info->fw_rfcal_data_r)
                release_firmware(fw_info->fw_rfcal_data_r);
        if (fw_info->sdd_data_r)
                release_firmware(fw_info->sdd_data_r);
#endif
	if (fw_info->fw_data)
		kfree(fw_info->fw_data);
	fw_info->fw_data = NULL;

	if (fw_info->buf_data)
		kfree(fw_info->buf_data);
	fw_info->buf_data = NULL;

	if (fw_info->fw_start_data)
		kfree(fw_info->fw_start_data);
	fw_info->fw_start_data = NULL;
}

int rk960_alloc_firmware_buf(struct firmware_info *fw_info)
{
#ifdef FW_LOADER_FROM_FOPEN
	fw_info->sdd_data = vmalloc(MAX_SDD_BUF_SIZE);
	if (fw_info->sdd_data == NULL) {
		RK960_ERROR_FW("alloc sdd_data failed\n");
		return -ENOMEM;
	}
#endif
	fw_info->fw_data = kmalloc(fw_info->fw_size, GFP_KERNEL);
	if (fw_info->fw_data == NULL) {
		RK960_ERROR_FW("alloc fw_data failed\n");
		goto err;
	}

	fw_info->buf_data = kmalloc(fw_info->buf_size, GFP_KERNEL);
	if (fw_info->buf_data == NULL) {
		RK960_ERROR_FW("alloc buf_data failed\n");
		goto err;
	}

	fw_info->fw_start_data = kmalloc(sizeof(struct io_cmd), GFP_KERNEL);
	if (fw_info->fw_start_data == NULL) {
		RK960_ERROR_FW("alloc fw_start_data failed\n");
		goto err;
	}
	memset(fw_info->fw_start_data, 0, sizeof(struct io_cmd));

	return 0;

err:
	rk960_free_firmware_buf(fw_info);
	return -ENOMEM;
}

#ifndef FW_LOADER_PATH_FROM_PARM
static const char *const fw_path[] = {
	"/etc/firmware",
	"/vendor/etc/firmware",
	"/lib/firmware",
	"/system/etc/firmware",
	"/oem/usr/ko",
	"/data",
	"/oem/usr/ko/rk96x/fw"
};
#endif

#ifndef FW_LOADER_FROM_FOPEN
static int rk960_get_firmware_info(struct rk960_common *priv,
                struct firmware_info *fw_info, enum rk960_download_fw_name_e fw_type)
{
	int ret = 0;

	if (fw_info->fw_saved)
		return 0;

    switch (fw_type) {
        case WIFI_FW_LOADER:
            ret = request_firmware(&fw_info->loder_data_r,
                            RK96x_LOADER_NAME, priv->pdev);
            if (ret) {
                    RK960_ERROR_FW("Can't load firmware file %s.\n",
                            RK96x_LOADER_NAME);
            } else {
                    RK960_INFO_FW("%s: loaded firmware %s\n",
                            __func__, RK96x_LOADER_NAME);
            }
            break;
        case WIFI_FW_SDD:
            ret = request_firmware(&fw_info->sdd_data_r,
                            RK96x_SDD, priv->pdev);
            if (ret) {
                    RK960_ERROR_FW("Can't load firmware file %s.\n",
                            RK96x_SDD);
            } else {
                    RK960_INFO_FW("%s: loaded firmware %s\n",
                            __func__, RK96x_SDD);
           }
           break;
        case WIFI_FW_RF:
            ret = request_firmware(&fw_info->fw_rfcal_data_r,
                            fw1_name, priv->pdev);
            if (ret) {
                    RK960_ERROR_FW("Can't load firmware file %s.\n",
                            fw1_name);
            } else {
                    RK960_INFO_FW("%s: loaded firmware %s\n",
                            __func__, fw1_name);
            }
            break;
        case WIFI_FW:
            ret = request_firmware(&fw_info->fw_data_r,
                            fw2_name, priv->pdev);
            if (ret) {
                    ret = request_firmware(&fw_info->fw_data_r,
                            RK962_FW2_ALIAS, priv->pdev);
                    if (ret) {
                            RK960_ERROR_FW("Can't load firmware file %s.\n",
                                    fw2_name);
                            goto firmware_release;
                    } else {
                            RK960_INFO_FW("%s: loaded firmware %s\n",
                                    __func__, RK962_FW2_ALIAS);
                    }
            } else {
                    RK960_INFO_FW("%s: loaded firmware %s\n",
                            __func__, fw2_name);
            }
            break;
        default:
            break;
    }
    return 0;

firmware_release:
        if (fw_info->loder_data_r)
                release_firmware(fw_info->loder_data_r);
        if (fw_info->fw_data_r)
                release_firmware(fw_info->fw_data_r);
        if (fw_info->fw_rfcal_data_r)
                release_firmware(fw_info->fw_rfcal_data_r);
        if (fw_info->sdd_data_r)
                release_firmware(fw_info->sdd_data_r);

    return ret;
}
#endif

static void rk_wifi_update_fw_name(u32 chip_name)
{
    if((0x1== rom_version.major) && (0x2 == rom_version.minor))
    {
        if(RK962_EFUSE == chip_name)
        {
            fw1_name = RK962_FW1_V2;
            fw2_name = RK962_FW2_V2;
        }
        else
        {
            fw1_name = RK960_FW1_V2;
            fw2_name = RK960_FW2_V2;
        }
    }
    else if((0x1== rom_version.major) && (0x1 == rom_version.minor))
    {
        if(RK962_EFUSE == chip_name)
        {
            fw1_name = RK962_FW1;
            fw2_name = RK962_FW2;
        }
        else
        {
            fw1_name = RK960_FW1;
            fw2_name = RK960_FW2;
        }
    }
    RK960_INFO_FW("fw1_name: %s, fw2_name: %s.\n", fw1_name, fw2_name);
}

static int set_loader_type(int type)
{
    loader_type = type;
    return 0;
}

static int set_rom_verison(fw_version_t ver)
{
    rom_version = ver;
    return 0;
}

static int rk960_get_rom_version(struct rk960_common *priv, u16 *rom_version)
{
	int ret;
	struct firmware_info *fw = &priv->firmware;
    fw_version_t rom_ver;

	memset(fw->buf_data, 0, 16);
	ret = rk960_reg_read(priv, SDIO_ROM_VER_ADDR, fw->buf_data, 10);
	if (ret) {
		RK960_ERROR_FW("%s: read rom_ver failed (%d)\n", __func__, ret);
		return -1;
	}

	RK960_INFO_FW("%s: rom_ver:", __func__);
	print_hex_dump(KERN_INFO, " ", DUMP_PREFIX_NONE,
		16, 1, fw->buf_data, 10, 1);
	*rom_version = (fw->buf_data[1]<<8) | (fw->buf_data[0]);
	rom_ver.major = fw->buf_data[1];
	rom_ver.minor = fw->buf_data[0];
	RK960_INFO_FW("rom_version: 0x%x.\n", *rom_version);
	set_rom_verison(rom_ver);
	RK960_INFO_FW("rom_ver.major: 0x%x. rom_ver.minor: 0x%x.\n", rom_ver.major, rom_ver.minor);
	if (rom_ver.major == 1 && rom_ver.minor == 1)
	{
		set_loader_type(RK96X_E1);
	}
	else
	{
		set_loader_type(RK96X_E2);
	}
	return 0;
}

static int rk960_get_chip_name(struct rk960_common *priv, u32 *chip_name)
{
	int ret;
	struct firmware_info *fw = &priv->firmware;

	memset(fw->buf_data, 0, 24);
	ret = rk960_reg_read(priv, SDIO_CHIP_NAME_ADDR, fw->buf_data, 20);
	if (ret) {
		RK960_ERROR_FW("%s: read rom_ver failed (%d)\n", __func__, ret);
		return -1;
	}

	RK960_INFO_FW("%s: chip_name:", __func__);
        print_hex_dump(KERN_INFO, " ", DUMP_PREFIX_NONE,
                16, 1, fw->buf_data, 20, 1);

	*chip_name = (fw->buf_data[18]<<8) | (fw->buf_data[19]);
	RK960_INFO_FW("chip_name: 0x%x.\n", *chip_name);

	return 0;
}

static unsigned int do_js_hash(unsigned int hash, unsigned char *buf, unsigned int len)
{
    unsigned int i  = 0;

    for(i = 0; i < len; i++) {
        hash ^= ((hash << 5) + buf[i] + (hash >> 2));
    }
    return hash;
}

#ifdef FW_LOADER_FROM_FOPEN
static int get_loader_type(void)
{
    return loader_type;
}

static fw_version_t get_rom_verison(void)
{
    return rom_version;
}

static int rk_wifi_verify_fw_version(struct file *fw_fd)
{
    fw_version_t fw_ver;
    fw_version_t rom_ver;
    loff_t fw_ver_offset = 0;

    rom_ver = get_rom_verison();
    if (rom_ver.major == 1 && rom_ver.minor == 1)
    {
        fw_ver_offset = RK96X_FW_VERSION_ADDR_V1_1;
    }
    else
    {
        fw_ver_offset = RK96X_FW_VERSION_ADDR;
    }
    fw_fd->f_pos = 0;
    if (kernel_read(fw_fd, &fw_ver, sizeof(fw_ver), &fw_ver_offset) < sizeof(fw_ver))
    {
        RK960_ERROR_FW("%s read fw_ver faild", __func__);
        return -1;
    }
    RK960_INFO_FW("fw_ver[%d.%d] rom_ver[%d.%d].", fw_ver.major, fw_ver.minor, rom_ver.major, rom_ver.minor);

    if ((fw_ver.major == rom_ver.major) && (fw_ver.minor == rom_ver.minor))
    {
        return 0;
    }
    else
    {
        return -1;
    }
}

static int rk_wifi_verify_fw_svn(struct file *fw_fd, loff_t position, int svn_limit)
{
    char svn_str[8] = {0};
    unsigned long svn;

    fw_fd->f_pos = 0;
    if (kernel_read(fw_fd, svn_str, sizeof(svn_str), &position) == 0)
    {
        RK960_ERROR_FW("%s read svn_str faild", __func__);
        return -1;
    }

    if (svn_str[7] != '\0')
    {
        svn_str[7] = '\0';
        RK960_ERROR_FW("svn number too long");
    }

    if (kstrtoul(svn_str, 10, &svn))
    {
        RK960_ERROR_FW("%s ,svn [%lu].", __func__, svn);
        return -EINVAL;
    }
    if (svn >= svn_limit)
    {
        RK960_INFO_FW("%s svn [%lu] is ok.", __func__, svn);
        return 0;
    }
    else
    {
        RK960_INFO_FW("svn [%lu] too old, need update to [%d]", svn, svn_limit);
        return -1;
    }
}

static int rk_wifi_check_fw_svn_version(struct file *fw_fd, const char * fw_name)
{
    int svn_limit = 0;
    int fw_svn_offset = 0;
    int type = get_loader_type();
    RK960_INFO_FW("%s, type: %d, fw_name: %s\n", __func__, type, fw_name);

    if (strstr(fw_name, RK960_FW1) || strstr(fw_name, RK960_FW1_V2))
    {
        if (type == RK96X_E2)
        {
            fw_svn_offset = RK960_FW1_SVN_VERSION_ADDR;
            svn_limit = RK960_FW1_SVN_VERSION_LIMIT;
        }
        else
        {
            fw_svn_offset = RK960_FW1_SVN_VERSION_ADDR_V1_1;
            svn_limit = RK960_FW1_SVN_VERSION_LIMIT_V1_1;
        }
    }
    else if (strstr(fw_name, RK960_FW2) || strstr(fw_name, RK960_FW2_V2))
    {
        if (rk_wifi_verify_fw_version(fw_fd) < 0) {
            RK960_ERROR_FW("%s: firmware version mismatch\n", __func__);
            return -EINVAL;
        }
        if (type == RK96X_E2)
        {
            fw_svn_offset = RK960_FW2_SVN_VERSION_ADDR;
            svn_limit = RK960_FW2_SVN_VERSION_LIMIT;
        }
        else
        {
            fw_svn_offset = RK960_FW2_SVN_VERSION_ADDR_V1_1;
            svn_limit = RK960_FW2_SVN_VERSION_LIMIT_V1_1;
        }
    }
    else if (strstr(fw_name, RK962_FW1) || strstr(fw_name, RK962_FW1_V2))
    {
        if (type == RK96X_E2)
        {
            fw_svn_offset = RK962_FW1_SVN_VERSION_ADDR;
            svn_limit = RK962_FW1_SVN_VERSION_LIMIT;
        }
        else
        {
            fw_svn_offset = RK962_FW1_SVN_VERSION_ADDR_V1_1;
            svn_limit = RK962_FW1_SVN_VERSION_LIMIT_V1_1;
        }
    }
    else if (strstr(fw_name, RK962_FW2) || strstr(fw_name, RK962_FW2_V2))
    {
        if (rk_wifi_verify_fw_version(fw_fd) < 0) {
            RK960_ERROR_FW("%s: firmware version mismatch\n", __func__);
            return -EINVAL;
        }
        if (type == RK96X_E2)
        {
            fw_svn_offset = RK962_FW2_SVN_VERSION_ADDR;
            svn_limit = RK962_FW2_SVN_VERSION_LIMIT;
        }
        else
        {
            fw_svn_offset = RK962_FW2_SVN_VERSION_ADDR_V1_1;
            svn_limit = RK962_FW2_SVN_VERSION_LIMIT_V1_1;
        }
    }

    return rk_wifi_verify_fw_svn(fw_fd, fw_svn_offset, svn_limit);
}
#endif

static int rk960_load_and_download_fw(struct rk960_common *priv,
                                    const char *name,
                                    int start_fw,
                                    enum rk960_download_fw_name_e fw_type)
{
    int ret = -ENOENT;
    struct firmware_info *fw = &priv->firmware;
    struct file *file = NULL;
    struct io_cmd *scmd;
    char path[320];
#ifdef FW_LOADER_PATH_FROM_PARM
    const char *fw_path;
#else
    int i;
#endif
    int read_size;
    int total_size = 0;
    int sdio_cmd_addr, write_addr_block_size;
#ifdef FW_LOADER_FROM_FOPEN
    int find = 0;
#else
    int load_size = 0;
    unsigned char *load_data = NULL;
    int j, cnt, left_size;
#endif
#ifdef FW_DOWNLOAD_CHECK
    uint32_t hash = 0;
#endif

#ifdef FW_LOADER_FROM_FOPEN
    memset(fw->fw_data, 0, fw->fw_size);
    // Find firmware file
#ifdef FW_LOADER_PATH_FROM_PARM
	fw_path = rk960_get_fw_path_param();
	if (fw_path == NULL) {
		RK960_ERROR_FW("%s: fw_path is NULL\n", __func__);
		ret = -ENOENT;
		goto out;
	}
	sprintf(path, "%s/%s", fw_path, name);
	file = filp_open(path, O_RDONLY, 0);
	if (!IS_ERR(file)) {
		find = 1;
	}
#else
    for (i = 0; i < ARRAY_SIZE(fw_path); i++) {
        if (!fw_path[i][0])
            continue;

        sprintf(path, "%s/%s", fw_path[i], name);
        file = filp_open(path, O_RDONLY, 0);
        if (!IS_ERR(file)) {
            find = 1;
            break;
        }
    }
#endif
    if (!find) {
        RK960_ERROR_FW("%s: can't find %s\n", __func__, name);
        ret = -ENOENT;
        goto out;
    }
    if (!(strstr(name, RK96x_LOADER_NAME)))
    {
        ret = rk_wifi_check_fw_svn_version(file, name);
        if (ret < 0) {
            RK960_ERROR_FW("%s: firmware version/svn check failed (%d)\n", __func__, ret);
            goto out;
        }
    }
    file->f_pos = 0;
    RK960_DEBUG_FW("%s: found %s\n", __func__, path);
#endif
    RK960_DEBUG_FW("%s: rk960_get_file_size(path)=%d, fw->buf_size: %d.\n", __func__, rk960_get_file_size(path), fw->buf_size);

    // Configure write parameters based on chip
    if (priv->chip_id == RK960_DEVICE_ID_D) {
        sdio_cmd_addr = (fw->buf_size > 64 * 1024) ?
                       SDIO_CMD_ADDR_VER_D : SDIO_CMD_ADDR_VER_ROM;
        write_addr_block_size = 512;
    } else {
        sdio_cmd_addr = SDIO_CMD_ADDR_VER_ABC;
        write_addr_block_size = 1;
    }

#ifndef FW_LOADER_FROM_FOPEN
    switch (fw_type) {
        case WIFI_FW_LOADER:
            if (fw->loder_data_r) {
                load_size = fw->loder_data_r->size;
                load_data = (unsigned char *)fw->loder_data_r->data;
            }
            break;
        case WIFI_FW_RF:
            if (fw->fw_rfcal_data_r) {
                load_size = fw->fw_rfcal_data_r->size;
                load_data = (unsigned char *)fw->fw_rfcal_data_r->data;
            }
            break;
        case WIFI_FW:
            if (fw->fw_data_r) {
                load_size = fw->fw_data_r->size;
                load_data = (unsigned char *)fw->fw_data_r->data;
            }
            break;
        default:
            break;
    }
#endif
    // Read and download firmware in chunks
    RK960_INFO_FW("Start downloading firmware %s\n", name);
#ifdef FW_LOADER_FROM_FOPEN
    while (1) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 14, 0)
        read_size = kernel_read(file, fw->fw_data, fw->fw_size, &file->f_pos);
#else
        read_size = kernel_read(file, file->f_pos, fw->fw_data, fw->fw_size);
        if (read_size > 0)
            file->f_pos += read_size;
#endif
        RK960_INFO_FW("read_size %d\n", read_size);
        if (read_size <= 0)
            break;
        read_size = ALIGN(read_size, 1024);
#else
    cnt = ((load_size - 1) / fw->fw_size) + 1;
    left_size = load_size;
    for (j = 0; j < cnt; j++) {
        if (left_size >= fw->fw_size) {
            read_size = fw->fw_size;
        } else if (left_size > 0) {
            read_size = left_size;
        } else {
            break;
        }
        memset(fw->fw_data, 0, fw->fw_size);
        memcpy(fw->fw_data, load_data + j * fw->fw_size,
            read_size);
        left_size -= read_size;
#endif
        if (WIFI_FW_LOADER == fw_type) {
            // Write loader to device
            ret = rk960_reg_write(priv,
                            SDIO_LOADER_VCT_ADDR / write_addr_block_size,
                            fw->fw_data,
                            ALIGN(read_size, 1024));
        } else if ((WIFI_FW_RF == fw_type) || (WIFI_FW == fw_type)) {
            // Write fw to device
            ret = rk960_reg_write(priv,
                            total_size / write_addr_block_size,
                            fw->fw_data,
                            ALIGN(read_size, 1024));
        } else if ((WIFI_FW_SDD == fw_type)) {
            if(read_size <= fw->sdd_size)
            {
                memcpy(fw->sdd_data, fw->fw_data, read_size);
                goto out;
            }
            else
               RK960_ERROR_FW("%s: file(%s) size %d exceed SDD buf(%d)\n",
                        __func__, path, read_size, fw->sdd_size);
            }
        if (ret) {
            RK960_ERROR_FW("Write failed (%d), size = %d\n",
                          ret, read_size);
            goto out;
        }

#ifdef FW_DOWNLOAD_CHECK
        hash = do_js_hash(hash, fw->fw_data, read_size);
#endif
        total_size += read_size;
    }

    fw->useful_code_size = total_size;

    // Prepare and send start command
    scmd = (struct io_cmd *)fw->fw_start_data;
#ifdef FW_DOWNLOAD_CHECK
    scmd->id = BOOTUP_CRC_CMD;
    scmd->length = 12;
    scmd->hash_value = hash;
#else
    scmd->id = SDIO_START_CMD_ID;
    scmd->length = 4;
#endif
    scmd->addr = (fw_type == WIFI_FW_LOADER) ?
                 SDIO_LOADER_VCT_ADDR : SDIO_FIRMWARE_VCT_ADDR;
    scmd->code_copy_addr = fw->useful_code_size;

    if (start_fw) {
        ret = rk960_reg_write(priv, sdio_cmd_addr, scmd,
                             sizeof(struct io_cmd));
        if (ret) {
            RK960_ERROR_FW("Start firmware failed (%d)\n", ret);
            goto out;
        }

        if (fw_type == WIFI_FW_LOADER) {
            // Get ROM version and chip name after loader
            u16 rom_version;
            u32 chip_name;
            msleep(10);
            rk960_get_rom_version(priv, &rom_version);
            rk960_get_chip_name(priv, &chip_name);
            rk_wifi_update_fw_name(chip_name);
        } else if (fw_type == WIFI_FW_RF) {
			u16 ctrl_reg;
			int to_count = 500;
			while (to_count--) {
				rk960_bh_read_ctrl_reg(priv, &ctrl_reg);
				if (ctrl_reg) {
					RK960_INFO_FW("rfcal complete %x\n", ctrl_reg);
					__rk960_clear_irq(priv);
					break;
				}
				msleep(10);
			}
			if (to_count <= 0) {
				RK960_ERROR_FW("rfcal failed\n");
				ret = -1;
				goto out;
			}
        }
    } else {
        priv->fw_scmd = scmd;
        priv->sdio_cmd_addr = sdio_cmd_addr;
    }

    RK960_INFO_FW("Firmware download completed successfully.\n");

out:
    if (file && !IS_ERR(file))
        filp_close(file, NULL);

    return ret;
}

int rk960_set_fw_board_config(struct rk960_common *priv)
{
	int ret;
	board_config_set_t board_config_set;
	struct io_cmd *scmd_copy;
	int sdio_cmd_addr = priv->sdio_cmd_addr;

	scmd_copy = kmalloc(sizeof(struct io_cmd), GFP_KERNEL);
	if (!scmd_copy) {
		RK960_ERROR_FW("%s: Failed to allocate memory for scmd_copy\n", __func__);
		return -ENOMEM;
	}

	memset(scmd_copy, 0, sizeof(struct io_cmd));
	memset(&board_config_set, 0, sizeof(board_config_set_t));
#if 0
    /* set board config */
	board_config_set.config.trace_timestamp_en = 0;  /* disable timestamp in log */
	board_config_set.config.wdt_timeout = 10;        /* set watchdog timeout to 10 seconds */
	board_config_set.mask.trace_timestamp_en = 0x1;   /* mask for trace_timestamp_en (1 bit field, set to 1) */
	board_config_set.mask.wdt_timeout = 0xFF;         /* mask for wdt_timeout (8 bits field, all 8 bits must be 1) */
#endif
	/* Prepare io_cmd for board config */
	scmd_copy->id = BOOT_UPGRADE_BOARD_CONFIG;
	scmd_copy->length = sizeof(board_config_set_t);
	/* Put board_config_set.mask (first u32) into scmd->addr, config (second u32) into scmd->code_copy_addr */
	/* board_config_t is a struct with bitfields, convert to u32 by accessing the first u32 directly */
	scmd_copy->addr = *((u32 *)&board_config_set.mask);  /* Get first u32 of mask (bitfields) */
	scmd_copy->code_copy_addr = *((u32 *)&board_config_set.config);  /* Get first u32 of config (bitfields) */

	RK960_DEBUG_FW("%s: board_config_set: mask u32=0x%x, config u32=0x%x\n",
		      __func__, *((u32 *)&board_config_set.mask), *((u32 *)&board_config_set.config));

	ret = rk960_reg_write(priv, sdio_cmd_addr, (void *)scmd_copy, sizeof(struct io_cmd));
	if (ret) {
		RK960_ERROR_FW("%s: Send board_config cmd failed (%d)\n", __func__, ret);
		kfree(scmd_copy);
		return ret;
	}

	kfree(scmd_copy);

	return 0;
}

int rk960_start_fw(struct rk960_common *priv)
{
        int ret;
        struct io_cmd *scmd = priv->fw_scmd;
        int sdio_cmd_addr = priv->sdio_cmd_addr;

	ret =
	    rk960_reg_write(priv, sdio_cmd_addr, (void *)scmd,
			    sizeof(struct io_cmd));
	if (ret) {
		RK960_ERROR_FW("%s: start fw failed (%d)\n", __func__, ret);
                return -1;
	}
        RK960_INFO_FW("%s: start fw success\n", __func__);

	//mdelay(20);
	return 0;
}

/* return 0 means fw download success, otherwise fail */
int rk960_load_firmware(struct rk960_common *priv, int start_fw)
{
    struct firmware_info *fw_info;
    int ret;

	fw_info = &priv->firmware;
    fw_info->fw_size = MAX_FW_BUF_SIZE;
    fw_info->buf_size = MAX_BUF_SIZE;

	RK960_INFO_FW("%s: priv->chip_id=0x%x,fw_info->buf_size=%d\n",__func__, priv->chip_id, fw_info->buf_size);

#ifdef SUPPORT_FWCR
    if (priv->fw_hotboot)
        return 0;
#endif

    if (rk960_alloc_firmware_buf(fw_info) != 0) {
        RK960_ERROR_FW("Failed to allocate firmware buffer\n");
        return -ENOMEM;
    }

#ifndef FW_LOADER_FROM_FOPEN
    /* get info of loader */
    if (rk960_get_firmware_info(priv, fw_info, WIFI_FW_LOADER)) {
        RK960_ERROR_FW("%s: get loader firmeware error!.\n", __func__);
        ret = -ENOENT;
        goto fw_out;
    }
    /* get info of sdd */
    if (rk960_get_firmware_info(priv, fw_info, WIFI_FW_SDD)) {
        RK960_ERROR_FW("%s: get sdd firmeware error!.\n", __func__);
        ret = -ENOENT;
        goto fw_out;
    }
#endif
    // Download loader
    ret = rk960_load_and_download_fw(priv, RK96x_LOADER_NAME, 1, WIFI_FW_LOADER);
    if (ret)
        goto fw_out;

    // loader SDD
    ret = rk960_load_and_download_fw(priv, RK96x_SDD, 0, WIFI_FW_SDD);
    //if (ret)
    //    goto fw_out;
#ifdef FW_LOADER_FROM_FOPEN
	if (rk960_sdd_parse(priv, fw_info->sdd_data, fw_info->sdd_size)) {
#else
        if (rk960_sdd_parse(priv,
                        fw_info->sdd_data_r ?
                        (unsigned char *)fw_info->sdd_data_r->data:NULL,
                        fw_info->sdd_data_r ? fw_info->sdd_data_r->size:0)) {
#endif
		RK960_ERROR_FW("%s: sdd parse error!.\n", __func__);
		ret = -ENOENT;
		goto fw_out;
	}
	//fw_info->fw_saved = 1;

#ifndef FW_LOADER_FROM_FOPEN
    /* get info of rf firmware */
    if (rk960_get_firmware_info(priv, fw_info, WIFI_FW_RF)) {
        RK960_ERROR_FW("%s: get rf fw error!.\n", __func__);
        ret = -ENOENT;
        goto fw_out;
    }
    /* get info of rompatch */
    if (rk960_get_firmware_info(priv, fw_info, WIFI_FW)) {
        RK960_ERROR_FW("%s: get firmeware error!.\n", __func__);
        ret = -ENOENT;
        goto fw_out;
    }
#endif
    // Download RF calibration firmware
    ret = rk960_load_and_download_fw(priv, fw1_name, 1, WIFI_FW_RF);
    if (ret)
        goto fw_out;

    // Download main firmware
    ret = rk960_load_and_download_fw(priv, fw2_name, start_fw, WIFI_FW);
    if (ret)
        goto fw_out;

    return ret;
fw_out:
    rk960_free_firmware_buf(fw_info);
    return ret;
}
