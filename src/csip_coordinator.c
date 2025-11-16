#include "csip_coordinator.h"

LOG_MODULE_REGISTER(csip_coordinator, LOG_LEVEL_DBG);

uint8_t sirk[16] = {0};
uint8_t sirk_none[16] = {0};

static bool rsi_scan_adv_parse(struct bt_data *data, void *user_data)
{
	struct {
		bt_addr_le_t addr;
		uint8_t is_rsi_adv;
	} *info = user_data;
	char addr_str[BT_ADDR_LE_STR_LEN];
	bt_addr_le_to_str(&info->addr, addr_str, sizeof(addr_str));
	if (data->type == BT_DATA_CSIS_RSI) {
		info->is_rsi_adv = 1;
		LOG_DBG("RSI advertisement found from %s", addr_str);
		LOG_HEXDUMP_DBG(data->data, data->data_len, "RSI data:");

		if (memcmp(sirk, sirk_none, 16) != 0) {
			LOG_HEXDUMP_DBG(sirk, 16, "Using SIRK:");
		
			if (bt_csip_set_coordinator_is_set_member(sirk, data)) {
				LOG_INF("RSI matches SIRK");
				return false;
			} else {
				LOG_DBG("RSI does not match SIRK for %s", addr_str);
			}
		}
	}

	return true;
}

void rsi_scan_cb(const bt_addr_le_t *addr, int8_t rssi, uint8_t type,
							struct net_buf_simple *ad)
{
	struct net_buf_simple_state state;
	net_buf_simple_save(ad, &state);
	char addr_str[BT_ADDR_LE_STR_LEN];
	bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));

	struct {
		bt_addr_le_t addr;
		uint8_t is_rsi_adv;
	} info;

	info.is_rsi_adv = 0;
	info.addr = *addr;
	bt_data_parse(ad, rsi_scan_adv_parse, &info);
	if (info.is_rsi_adv) {
		net_buf_simple_restore(ad, &state);
		LOG_HEXDUMP_DBG(ad->data, ad->len, "Advertisement data:");
	}
}

void csip_coordinator_rsi_scan_start(uint8_t device_id) {
	int err;
	err = bt_le_scan_stop();
	if (err)
	{
		LOG_ERR("Stopping existing scan failed (err %d)", err);
		return;
	}

	err = bt_le_scan_start(BT_LE_SCAN_ACTIVE_CONTINUOUS, rsi_scan_cb);
	if (err)
	{
		LOG_ERR("Scanning failed to start (err %d)", err);
		return;
	}

	LOG_INF("Scanning for RSI advertisements started");
}

