#ifndef __RK96X_CONFIG_H__
#define __RK96X_CONFIG_H__

#include <linux/types.h>

/* Configuration setter function type */
typedef void (*config_setter_t)(const char *value);

/* Configuration item structure */
struct config_item {
	const char		*key;
	const char		*default_value;
	config_setter_t	setter;
};

/* Configuration file path */
#define WIFI_USER_CONFIG_PATH "/data/rk96x_wifi.conf"

/* Data directory path management */
extern char data_dir_param[];
const char *config_get_data_dir(void);
void config_get_full_path(char *full_path, size_t size, const char *filename);

/* Configuration management functions */
int load_wifi_user_config(void);
int parse_config_line(char *line, char **key, char **value);
void config_apply_kv(const char *key, const char *value);
void config_apply_defaults(void);

/* Configuration value access functions */
int config_get_jtag_debug(void);
int config_get_fw_debug_log(void);
int config_get_tx_agg_num(void);
int config_get_sgi(void);
int config_get_stbc(void);
int config_get_ht_support(void);
int config_get_mcs_mask(void);
int config_get_sdio_width(void);
void config_set_sdio_width(int value);
int config_get_lpo_source(void);
void config_set_lpo_source(int value);
int config_get_csync_en(void);
void config_set_csync_en(int value);
#ifdef FW_LOADER_PATH_FROM_PARM
const char *config_get_fw_path(void);
void config_set_fw_path(const char *value);
#endif
#ifdef SUPPORT_RK962_POWERSAVE
int config_get_mac_address_initialized(void);
#endif

#ifdef SUPPORT_RK962_POWERSAVE
/* MAC address configuration functions */
int rk960_mac_check_config_file_exist(const char *path);
int rk960_mac_create_tmp_config_file(const char *path);
unsigned char* rk960_read_mac_addr_from_config(void);
void rk960_write_mac_addr_to_config(unsigned char *mac);
unsigned char* rk960_check_and_read_mac_from_config(void);
#endif

#endif /* __RK96X_CONFIG_H__ */
