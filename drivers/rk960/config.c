#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/string.h>
#include <linux/etherdevice.h>
#include <linux/random.h>

#include "rk960.h"
#include "config.h"
#include "debug.h"
#include "bh.h"
#include "wsm.h"

/* Data directory parameter */
char data_dir_param[256] = "data";
module_param_string(data_dir, data_dir_param, sizeof(data_dir_param), 0644);
MODULE_PARM_DESC(data_dir, "Data directory path (default: data)");

/* Configuration variables */
static int jtag_debug = 0;
module_param(jtag_debug, int, 0644);
MODULE_PARM_DESC(jtag_debug, "jtag debug");

static int fw_debug_log = -1;
module_param(fw_debug_log, int, 0644);
MODULE_PARM_DESC(fw_debug_log, "fw debug log level");

static int tx_agg_num = -1;
module_param(tx_agg_num, int, 0644);
MODULE_PARM_DESC(tx_agg_num, "tx aggregation number");

static int sgi = 0;
module_param(sgi, int, 0644);
MODULE_PARM_DESC(sgi, "short gi");

static int stbc = 0;
module_param(stbc, int, 0644);
MODULE_PARM_DESC(stbc, "stbc");

static int ht_support = 1;
module_param(ht_support, int, 0644);
MODULE_PARM_DESC(ht_support, "ht support");

static int mcs_mask = 0xFF;
module_param(mcs_mask, int, 0644);
MODULE_PARM_DESC(mcs_mask, "mcs mask");

static int sdio_width = -1;
module_param(sdio_width, int, 0644);
MODULE_PARM_DESC(sdio_width, "sdio width");

static int lpo_source = 1;
module_param(lpo_source, int, 0644);
MODULE_PARM_DESC(lpo_source, "LPO source (1=external, 2=internal)");

static int csync_en = -1;
module_param(csync_en, int, 0644);
MODULE_PARM_DESC(csync_en, "CSYNC enable (1=enable, 0=disable, -1=default)");

#ifdef FW_LOADER_PATH_FROM_PARM
static char fw_path_buffer[256] = "/oem/usr/ko/rk96x/fw";
static char *fw_path = fw_path_buffer;
module_param(fw_path, charp, 0644);
MODULE_PARM_DESC(fw_path, "fw path");
#endif

/* Configuration value access functions */
int config_get_jtag_debug(void)
{
	return jtag_debug;
}

int config_get_fw_debug_log(void)
{
	return fw_debug_log;
}

int config_get_tx_agg_num(void)
{
	return tx_agg_num;
}

int config_get_sgi(void)
{
	return sgi;
}

int config_get_stbc(void)
{
	return stbc;
}

int config_get_ht_support(void)
{
	return ht_support;
}

int config_get_mcs_mask(void)
{
	return mcs_mask;
}

int config_get_sdio_width(void)
{
	return sdio_width;
}

void config_set_sdio_width(int value)
{
	sdio_width = value;
}

int config_get_lpo_source(void)
{
	return lpo_source;
}

void config_set_lpo_source(int value)
{
	lpo_source = value;
}

int config_get_csync_en(void)
{
	return csync_en;
}

void config_set_csync_en(int value)
{
	csync_en = value;
}

#ifdef FW_LOADER_PATH_FROM_PARM
const char *config_get_fw_path(void)
{
	return fw_path;
}

void config_set_fw_path(const char *value)
{
	if (value && strlen(value) > 0) {
		size_t len = strlen(value);
		if (len < sizeof(fw_path_buffer)) {
			strncpy(fw_path_buffer, value, sizeof(fw_path_buffer) - 1);
			fw_path_buffer[sizeof(fw_path_buffer) - 1] = '\0';
		}
	}
}
#endif

int parse_config_line(char *line, char **key, char **value)
{
	char *equals = strchr(line, '=');
	char *newline;

	if (!equals)
		return -1;

	*equals = '\0';
	*key = line;

	newline = strchr(equals + 1, '\n');
	if (newline)
		*newline = '\0';

	*value = equals + 1;

	while (**key == ' ' || **key == '\t')
		(*key)++;
	for (equals = *key; *equals; equals++)
		;
	while (equals > *key && (equals[-1] == ' ' || equals[-1] == '\t'))
		equals--;
	*equals = '\0';

	while (**value == ' ' || **value == '\t')
		(*value)++;
	for (equals = (char *)*value; *equals; equals++)
		;
	while (equals > *value && (equals[-1] == ' ' || equals[-1] == '\t'))
		equals--;
	*equals = '\0';

	return 0;
}

/* Setter functions */
static void set_jtag_debug(const char *value)
{
	int v;
	if (!kstrtoint(value, 10, &v)) {
		jtag_debug = v;
		RK960_DEBUG_MAIN("Set jtag_debug=%d\n", v);
	}
}

static void set_sgi(const char *value)
{
	int v;
	if (!kstrtoint(value, 10, &v)) {
		sgi = v;
		RK960_DEBUG_MAIN("Set sgi=%d\n", v);
	}
}

static void set_stbc(const char *value)
{
	int v;
	if (!kstrtoint(value, 10, &v)) {
		stbc = v;
		RK960_DEBUG_MAIN("Set stbc=%d\n", v);
	}
}

static void set_ht_support(const char *value)
{
	int v;
	if (!kstrtoint(value, 10, &v)) {
		ht_support = v;
		RK960_DEBUG_MAIN("Set ht_support=%d\n", v);
	}
}

static void set_mcs_mask(const char *value)
{
	int v;
	if (!kstrtoint(value, 0, &v)) {
		mcs_mask = v;
		RK960_DEBUG_MAIN("Set mcs_mask=0x%x\n", v);
	}
}

static void set_fw_debug_log(const char *value)
{
	int v;
	if (!kstrtoint(value, 0, &v)) {
		fw_debug_log = v;
		RK960_DEBUG_MAIN("Set fw_debug_log=%d\n", v);
	}
}

static void set_tx_agg_num(const char *value)
{
	int v;
	if (!kstrtoint(value, 10, &v)) {
		tx_agg_num = v;
		RK960_DEBUG_MAIN("Set tx_agg_num=%d\n", v);
	}
}

static void set_sdio_width(const char *value)
{
	int v;
	if (!kstrtoint(value, 10, &v)) {
		config_set_sdio_width(v);
		RK960_DEBUG_MAIN("Set sdio_width=%d\n", v);
	}
}

static void set_lpo_source(const char *value)
{
	int v;
	if (!kstrtoint(value, 10, &v)) {
		config_set_lpo_source(v);
		RK960_DEBUG_MAIN("Set lpo_source=%d\n", v);
	}
}

static void set_csync_en(const char *value)
{
	int v;
	if (!kstrtoint(value, 10, &v)) {
		config_set_csync_en(v);
		RK960_DEBUG_MAIN("Set csync_en=%d\n", v);
	}
}

static void set_fw_no_sleep(const char *value)
{
	int v;
	if (!kstrtoint(value, 10, &v)) {
		fw_no_sleep_set(v);
		RK960_DEBUG_MAIN("Set fw_no_sleep=%d\n", v);
	}
}

static void set_debug_level(const char *value)
{
	unsigned int v;
	if (!kstrtouint(value, 0, &v)) {
		debug_level_set(v);
		RK960_DEBUG_MAIN("Set debug_level=0x%x\n", v);
	}
}

#ifdef FW_LOADER_PATH_FROM_PARM
static void set_fw_path(const char *value)
{
	if (value && value[0]) {
		config_set_fw_path(value);
		RK960_DEBUG_MAIN("Set fw_path=%s\n", value);
	}
}
#endif

#ifdef CONFIG_OFFLINE_LOG_SUPPORT
static void set_offline_log_en(const char *value)
{
	unsigned int v;
	if (!kstrtouint(value, 10, &v)) {
		offline_log_en_set(v);
		RK960_DEBUG_MAIN("Set offline_log_en=%u\n", v);
	}
}

static void set_offline_log_file_path(const char *value)
{
	if (value && value[0]) {
		offline_log_file_path_set(value);
		RK960_DEBUG_MAIN("Set offline_log_file_path=%s\n", offline_log_file_path_param());
	}
}
#endif

#ifdef SUPPORT_RK962_POWERSAVE
/* MAC address configuration variables */
char wifi_user_config_file[128] = {0};
unsigned char config_mac_address[ETH_ALEN] = {0};

static int mac_address_from_config_initialized = 0;

/* MAC address configuration functions */
int rk960_mac_check_config_file_exist(const char *path)
{
	struct file *filp = filp_open(path, O_RDONLY, 0);
	int ret = 0;

	if (IS_ERR(filp)) {
		if (PTR_ERR(filp) == -ENOENT) {
			RK960_DEBUG_MAIN("Not exist: %s\n", path);
		} else {
			RK960_DEBUG_MAIN("Other error: %ld\n", PTR_ERR(filp));
		}
		ret = (int)PTR_ERR(filp);
	} else {
		filp_close(filp, NULL);
		RK960_DEBUG_MAIN("File exist: %s\n", path);
		ret = 0;
	}
	return ret; /* 0: exist, non-zero: not found */
}

int rk960_mac_create_tmp_config_file(const char *path)
{
	struct file *filp;
	loff_t pos = 0;
	ssize_t ret;
	const char *data = "mac_address=0\n";
	size_t len = strlen(data);

	filp = filp_open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (IS_ERR(filp)) {
		RK960_ERROR_MAIN("Failed to open file: %ld\n", PTR_ERR(filp));
		return PTR_ERR(filp);
	}

	ret = kernel_write(filp, data, len, &pos);
	if (ret < 0) {
		RK960_ERROR_MAIN("Failed to write file: %zd\n", ret);
		filp_close(filp, NULL);
		return ret;
	} else if (ret != len) {
		RK960_ERROR_MAIN("Partial write: %zd/%zu bytes\n", ret, len);
		filp_close(filp, NULL);
		return -EIO;
	}

	filp_close(filp, NULL);
	RK960_DEBUG_MAIN("File created and written: %s\n", path);
	return 0;
}

unsigned char* rk960_read_mac_addr_from_config(void)
{
	struct file *file;
	loff_t pos = 0;
	char *macPointer, *macEnd;
	ssize_t read_bytes = 0;
	ssize_t total_bytes = 0;
	size_t capacity = 4096;
	char *buffer = kmalloc(capacity, GFP_KERNEL);
	char mac_addr_string[18];
	char *dst, *src, *end;
	unsigned int mac_bytes[6];
	int numScanned;
	int i;

	if (!buffer) {
		return NULL;
	}
	memset(buffer, 0, capacity);

	file = filp_open(wifi_user_config_file, O_RDONLY, 0);
	if (IS_ERR(file)) {
		RK960_ERROR_MAIN("Read config: error opening file\n");
		kfree(buffer);
		return NULL;
	}

	pos = 0;
	while ((read_bytes = kernel_read(file, buffer + total_bytes,
					capacity - total_bytes, &pos)) > 0) {
		total_bytes += read_bytes;
	}
	filp_close(file, NULL);

	if (total_bytes <= 0 || total_bytes >= capacity) {
		RK960_ERROR_MAIN("Read config: error reading file\n");
		kfree(buffer);
		return NULL;
	}

	macPointer = strstr(buffer, "mac_address=");
	if (!macPointer) {
		RK960_DEBUG_MAIN("Read config: mac_address= not found\n");
		kfree(buffer);
		return NULL;
	}

	macPointer += strlen("mac_address=");
	macEnd = strchr(macPointer, '\n');
	if (macEnd) {
		*macEnd = '\0';
	}
	strncpy(mac_addr_string, macPointer, sizeof(mac_addr_string) - 1);
	mac_addr_string[sizeof(mac_addr_string) - 1] = '\0';

	if (strcmp(mac_addr_string, "0") == 0) {
		RK960_DEBUG_MAIN("Read config: MAC address is default value 0\n");
		kfree(buffer);
		return NULL;
	}

	dst = mac_addr_string;
	src = mac_addr_string;

	while (*src && (*src == ' ' || *src == '\t' || *src == '\r' || *src == '\n')) {
		src++;
	}

	while (*src) {
		if (*src >= ' ' && *src <= '~') {
			*dst++ = *src;
		}
		src++;
	}
	*dst = '\0';

	end = dst - 1;
	while (end >= mac_addr_string && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')) {
		*end = '\0';
		end--;
	}

	numScanned = sscanf(mac_addr_string, "%2x:%2x:%2x:%2x:%2x:%2x",
				&mac_bytes[0], &mac_bytes[1], &mac_bytes[2],
				&mac_bytes[3], &mac_bytes[4], &mac_bytes[5]);
	if (numScanned == 6) {
		for (i = 0; i < 6; i++) {
			config_mac_address[i] = (unsigned char)mac_bytes[i];
		}
	} else {
		if (mac_pton(mac_addr_string, config_mac_address) != 0) {
			RK960_ERROR_MAIN("Read config: failed to parse MAC: '%s'\n", mac_addr_string);
			kfree(buffer);
			return NULL;
		}
	}

	if (!is_valid_ether_addr(config_mac_address)) {
		RK960_ERROR_MAIN("Read config: invalid MAC address: %pM\n", config_mac_address);
		kfree(buffer);
		return NULL;
	}

	kfree(buffer);
	RK960_DEBUG_MAIN("Read config: MAC address: %pM\n", config_mac_address);
	return config_mac_address;
}

void rk960_write_mac_addr_to_config(unsigned char *mac)
{
	struct file *file;
	loff_t pos = 0;
	char *macPointer, *macEnd;
	ssize_t read_bytes = 0;
	ssize_t total_bytes = 0;
	size_t capacity = 4096;
	char *buffer = kmalloc(capacity, GFP_KERNEL);
	char mac_addr_string[18];
	char new_line[32];
	ssize_t write_bytes = 0;
	ssize_t total_written = 0;
	int old_len, new_len, tail_len;

	if (!buffer) {
		return;
	}
	memset(buffer, 0, capacity);

	sprintf(mac_addr_string, "%02x:%02x:%02x:%02x:%02x:%02x",
		mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

	file = filp_open(wifi_user_config_file, O_RDONLY, 0);
	if (IS_ERR(file)) {
		RK960_ERROR_MAIN("Write config: error opening file for read\n");
		kfree(buffer);
		return;
	}

	pos = 0;
	while ((read_bytes = kernel_read(file, buffer + total_bytes,
					capacity - total_bytes, &pos)) > 0) {
		total_bytes += read_bytes;
	}
	filp_close(file, NULL);

	if (total_bytes <= 0 || total_bytes >= capacity) {
		RK960_ERROR_MAIN("Write config: error reading file or file too large\n");
		kfree(buffer);
		return;
	}

	macPointer = strstr(buffer, "mac_address=");
	if (!macPointer) {
		sprintf(new_line, "mac_address=%s\n", mac_addr_string);
		if (total_bytes + strlen(new_line) >= capacity) {
			RK960_ERROR_MAIN("Write config: buffer overflow\n");
			kfree(buffer);
			return;
		}
		memcpy(buffer + total_bytes, new_line, strlen(new_line));
		total_bytes += strlen(new_line);
		goto write_file;
	}

	macPointer += strlen("mac_address=");
	macEnd = strchr(macPointer, '\n');
	if (!macEnd) {
		RK960_ERROR_MAIN("Write config: invalid config file format\n");
		kfree(buffer);
		return;
	}

	old_len = macEnd - macPointer;
	new_len = strlen(mac_addr_string);
	tail_len = total_bytes - (macEnd - buffer) - 1;

	if (new_len != old_len && tail_len > 0)
		memmove(macPointer + new_len + 1, macEnd + 1, tail_len);

	memcpy(macPointer, mac_addr_string, new_len);
	macPointer[new_len] = '\n';

	total_bytes += (new_len - old_len);

write_file:
	file = filp_open(wifi_user_config_file, O_WRONLY | O_TRUNC, 0);
	if (IS_ERR(file)) {
		RK960_ERROR_MAIN("Write config: error opening file for write\n");
		kfree(buffer);
		return;
	}

	pos = 0;
	while (total_written < total_bytes) {
		write_bytes = kernel_write(file, buffer + total_written,
					total_bytes - total_written, &pos);
		if (write_bytes < 0) {
			RK960_ERROR_MAIN("Write config: failed to write file\n");
			break;
		}
		total_written += write_bytes;
	}

	vfs_fsync(file, 0);
	filp_close(file, NULL);
	kfree(buffer);

	RK960_DEBUG_MAIN("Write config: MAC address written: %s\n", mac_addr_string);
}

unsigned char* rk960_check_and_read_mac_from_config(void)
{
	char config_path[256];

	memset(wifi_user_config_file, 0, sizeof(wifi_user_config_file));
	config_get_full_path(config_path, sizeof(config_path), "rk96x_wifi.conf");
	strcpy(wifi_user_config_file, config_path);

	if (rk960_mac_check_config_file_exist(config_path) == 0) {
		return rk960_read_mac_addr_from_config();
	}

	return NULL;
}

int config_get_mac_address_initialized(void)
{
	return mac_address_from_config_initialized;
}

static void set_mac_address_from_config(const char *value)
{
	int v;
	unsigned char random_mac[ETH_ALEN];
	unsigned char *config_mac = NULL;
	int ret;

	if (kstrtoint(value, 10, &v))
		return;

	RK960_DEBUG_MAIN("Set mac_address_from_config=%d\n", v);

	if (mac_address_from_config_initialized)
		return;
	mac_address_from_config_initialized = 1;

	memset(wifi_user_config_file, 0, sizeof(wifi_user_config_file));
	config_get_full_path(wifi_user_config_file, sizeof(wifi_user_config_file), "rk96x_wifi.conf");

	config_mac = rk960_check_and_read_mac_from_config();

	if (config_mac != NULL && is_valid_ether_addr(config_mac)) {
		RK960_DEBUG_MAIN("wifi mac addr from config: %pM\n", config_mac);
		return;
	}

	if (v != 0 || config_mac != NULL)
		return;

	if (rk960_mac_check_config_file_exist(wifi_user_config_file) != 0) {
		ret = rk960_mac_create_tmp_config_file(wifi_user_config_file);
		if (ret != 0) {
			RK960_ERROR_MAIN("Failed to create config file: %s\n", wifi_user_config_file);
			return;
		}
		RK960_INFO_MAIN("Created config file: %s\n", wifi_user_config_file);
	}

	eth_random_addr(random_mac);
	RK960_DEBUG_MAIN("wifi mac addr from random: %pM\n", random_mac);

	if (is_valid_ether_addr(random_mac)) {
		RK960_INFO_MAIN("write random mac addr to config and as fixed mac addr.\n");
		rk960_write_mac_addr_to_config(random_mac);
	}
}
#endif

/* Configuration items array */
const struct config_item config_items[] = {
	{ "debug_level", "0x6", set_debug_level },
	{ "sdio_width", "-1", set_sdio_width },
	{ "fw_no_sleep", "0", set_fw_no_sleep },
#ifdef FW_LOADER_PATH_FROM_PARM
	{ "fw_path", "/oem/usr/ko/rk96x/fw", set_fw_path },
#endif
	{ "lpo_source", "1", set_lpo_source },
	{ "csync_en", "-1", set_csync_en },
	{ "jtag_debug", "0", set_jtag_debug },
	{ "sgi", "0", set_sgi },
	{ "stbc", "0", set_stbc },
	{ "ht_support", "1", set_ht_support },
	{ "mcs_mask", "0xFF", set_mcs_mask },
	{ "fw_debug_log", "-1", set_fw_debug_log },
	{ "tx_agg_num", "-1", set_tx_agg_num },
#ifdef SUPPORT_RK962_POWERSAVE
	{ "mac_address", "0", set_mac_address_from_config },
#endif
#ifdef CONFIG_OFFLINE_LOG_SUPPORT
	{ "offline_log_en", "0", set_offline_log_en },
	{ "offline_log_file_path", "/tmp/offline_log.txt", set_offline_log_file_path },
#endif
};

int config_items_count = ARRAY_SIZE(config_items);
static bool config_items_set[ARRAY_SIZE(config_items)];

void config_apply_kv(const char *key, const char *value)
{
	int i;

	for (i = 0; i < config_items_count; i++) {
		if (!strcmp(key, config_items[i].key)) {
			config_items[i].setter(value);
			config_items_set[i] = true;
			break;
		}
	}
}

void config_apply_defaults(void)
{
	int i;

	for (i = 0; i < config_items_count; i++)
		config_items[i].setter(config_items[i].default_value);
}

static void config_apply_missing_defaults(void)
{
	int i;

	for (i = 0; i < config_items_count; i++) {
		if (!config_items_set[i]) {
			config_items[i].setter(config_items[i].default_value);
			RK960_DEBUG_MAIN("Applied default value for missing config: %s=%s\n",
					config_items[i].key, config_items[i].default_value);
		}
	}
}

static void parse_config_buffer(char *buffer, size_t buffer_size)
{
	char *line = buffer;
	char *end = buffer + buffer_size;
	char line_buf[256];
	char *key, *value;

	memset(config_items_set, 0, sizeof(config_items_set));

	while (line < end) {
		char *line_end;
		size_t line_len;

		line_end = strchr(line, '\n');
		if (!line_end)
			line_end = end;

		line_len = line_end - line;
		if (line_len == 0) {
			line++;
			continue;
		}

		if (line_len >= sizeof(line_buf))
			line_len = sizeof(line_buf) - 1;
		memcpy(line_buf, line, line_len);
		line_buf[line_len] = '\0';

		if (line_buf[0] == '\0' || line_buf[0] == '#') {
			line = line_end + 1;
			continue;
		}

		if (parse_config_line(line_buf, &key, &value) == 0)
			config_apply_kv(key, value);

		line = line_end + 1;
	}
}

int load_wifi_user_config(void)
{
	struct file *filp;
	loff_t pos = 0;
	ssize_t read_len;
	char *buffer;
	int ret = 0;
	char config_path[256];

	buffer = kmalloc(4096, GFP_KERNEL);
	if (!buffer) {
		RK960_ERROR_MAIN("Failed to allocate buffer for config\n");
		return -ENOMEM;
	}

	memset(buffer, 0, 4096);

	config_get_full_path(config_path, sizeof(config_path), "rk96x_wifi.conf");

	filp = filp_open(config_path, O_RDONLY, 0);
	if (IS_ERR(filp)) {
		if (PTR_ERR(filp) == -ENOENT) {
			RK960_INFO_MAIN("Config file not found, using module_param values\n");
			kfree(buffer);
			return 0;
		} else {
			RK960_ERROR_MAIN("Failed to open config file: %ld, using module_param values\n", PTR_ERR(filp));
			kfree(buffer);
			return 0;
		}
	}

	read_len = kernel_read(filp, buffer, 4095, &pos);
	if (read_len < 0) {
		RK960_ERROR_MAIN("Failed to read config file: %zd, using module_param values\n", read_len);
		filp_close(filp, NULL);
		kfree(buffer);
		return 0;
	}
	filp_close(filp, NULL);

	parse_config_buffer(buffer, read_len);
	config_apply_missing_defaults();

	kfree(buffer);
	RK960_DEBUG_MAIN("Loaded config from %s\n", config_path);
	return 0;
}

const char *config_get_data_dir(void)
{
	return data_dir_param;
}

void config_get_full_path(char *full_path, size_t size, const char *filename)
{
	const char *dir = config_get_data_dir();

	if (dir && dir[0] == '/') {
		snprintf(full_path, size, "%s/%s", dir, filename);
	} else {
		snprintf(full_path, size, "/%s/%s", dir, filename);
	}
}
