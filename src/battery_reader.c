#include "battery_reader.h"

LOG_MODULE_REGISTER(battery_reader, LOG_LEVEL_INF);

/* Global state variables */
bool battery_discovered = false;
uint8_t battery_level = 0;

/* GATT handles for Battery Service */
static uint16_t battery_level_handle = 0;
static uint16_t battery_level_ccc_handle = 0;

/* Notification callback for battery level updates */
static uint8_t battery_notify_cb(struct bt_conn *conn,
								 struct bt_gatt_subscribe_params *params,
								 const void *data, uint16_t length)
{
	if (!data)
	{
		LOG_INF("Battery level notifications unsubscribed");
		params->value_handle = 0;
		return BT_GATT_ITER_STOP;
	}

	if (length != 1)
	{
		LOG_WRN("Unexpected battery level length: %u", length);
		return BT_GATT_ITER_CONTINUE;
	}

	battery_level = *(uint8_t *)data;
	LOG_INF("Battery level notification: %u%%", battery_level);

	return BT_GATT_ITER_CONTINUE;
}

/* Subscription parameters for battery level notifications */
static struct bt_gatt_subscribe_params battery_subscribe_params = {
	.notify = battery_notify_cb,
	.value = BT_GATT_CCC_NOTIFY,
};

/* Read callback for battery level characteristic */
static uint8_t battery_read_cb(struct bt_conn *conn, uint8_t err,
							   struct bt_gatt_read_params *params,
							   const void *data, uint16_t length)
{
	if (err)
	{
		LOG_ERR("Battery level read failed (err %u)", err);
		return BT_GATT_ITER_STOP;
	}

	if (!data)
	{
		LOG_DBG("Battery level read complete");
		return BT_GATT_ITER_STOP;
	}

	if (length != 1)
	{
		LOG_WRN("Unexpected battery level length: %u", length);
		return BT_GATT_ITER_STOP;
	}

	battery_level = *(uint8_t *)data;
	LOG_INF("Battery level read: %u%%", battery_level);

	ble_cmd_complete(0);

	return BT_GATT_ITER_STOP;
}

/* Read parameters for battery level */
static struct bt_gatt_read_params battery_read_params = {
	.func = battery_read_cb,
	.handle_count = 1,
};

/* Discovery callback for Battery Service characteristics */
static uint8_t discover_char_cb(struct bt_conn *conn,
                                 const struct bt_gatt_attr *attr,
                                 struct bt_gatt_discover_params *params)
{
	if (!attr) {
		LOG_DBG("Discovery complete for type %d", params->type);
		
		if (params->type == BT_GATT_DISCOVER_DESCRIPTOR) {
			/* We finished searching for CCC descriptor */
			if (battery_level_ccc_handle == 0) {
				/* Didn't find CCC - notifications not available */
				LOG_WRN("CCC descriptor not found - notifications not available");
			}
		}
		
		/* If we have the characteristic handle, mark discovery as complete */
		if (battery_level_handle != 0) {
			battery_discovered = true;
			// Complete the discovery command
			ble_cmd_complete(0);
			LOG_DBG("Battery Service discovery complete (handle: 0x%04x, CCC: 0x%04x)", 
			        battery_level_handle, battery_level_ccc_handle);
		} else {
			LOG_ERR("Battery Service discovery completed but no characteristic found");
		}
		
		return BT_GATT_ITER_STOP;
	}

	LOG_DBG("Found attribute at handle %u, type %d", attr->handle, params->type);

	if (params->type == BT_GATT_DISCOVER_CHARACTERISTIC) {
		struct bt_gatt_chrc *chrc = (struct bt_gatt_chrc *)attr->user_data;
		
		if (!bt_uuid_cmp(chrc->uuid, BT_UUID_BAS_BATTERY_LEVEL)) {
			LOG_DBG("Found Battery Level characteristic at handle %u (properties 0x%02x)", 
			        chrc->value_handle, chrc->properties);
			battery_level_handle = chrc->value_handle;
			
			/* Check if notifications are supported based on properties */
			if (chrc->properties & BT_GATT_CHRC_NOTIFY) {
				LOG_DBG("Characteristic supports notifications, discovering CCC");
				/* Try to discover CCC descriptor */
				static struct bt_gatt_discover_params discover_params;
				memset(&discover_params, 0, sizeof(discover_params));
				discover_params.uuid = BT_UUID_GATT_CCC;
				discover_params.type = BT_GATT_DISCOVER_DESCRIPTOR;
				discover_params.start_handle = chrc->value_handle + 1;
				discover_params.end_handle = params->end_handle;
				discover_params.func = discover_char_cb;
				
				int err = bt_gatt_discover(conn, &discover_params);
				if (err) {
					LOG_WRN("Failed to discover CCC (err %d) - proceeding without notifications", err);
				}
			} else {
				LOG_WRN("Characteristic does not support notifications");
			}

			battery_discovered = true;
			return BT_GATT_ITER_STOP;
		}
	} else if (params->type == BT_GATT_DISCOVER_DESCRIPTOR) {
		struct bt_gatt_chrc *chrc = (struct bt_gatt_chrc *)attr->user_data;
		
		if (!bt_uuid_cmp(chrc->uuid, BT_UUID_GATT_CCC)) {
			LOG_DBG("Found CCC descriptor at handle %u", attr->handle);
			battery_level_ccc_handle = attr->handle;
			battery_discovered = true;
			return BT_GATT_ITER_STOP;
		}
	}

	return BT_GATT_ITER_CONTINUE;
}

/* Discovery callback for Battery Service */
static uint8_t discover_service_cb(struct bt_conn *conn,
								   const struct bt_gatt_attr *attr,
								   struct bt_gatt_discover_params *params)
{
	if (!attr)
	{
		LOG_WRN("Battery Service not found");
		return BT_GATT_ITER_STOP;
	}

	struct bt_gatt_service_val *svc = (struct bt_gatt_service_val *)attr->user_data;

	LOG_DBG("Found Battery Service at handle 0x%04X-0x%04X",
			attr->handle, svc->end_handle);

	LOG_DBG("Discover characteristics within Battery Service");

	static struct bt_gatt_discover_params discover_params;
	memset(&discover_params, 0, sizeof(discover_params));
	discover_params.uuid = NULL;
	discover_params.type = BT_GATT_DISCOVER_CHARACTERISTIC;
	discover_params.start_handle = attr->handle + 1;
	discover_params.end_handle = svc->end_handle;
	discover_params.func = discover_char_cb;

	int err = bt_gatt_discover(conn, &discover_params);
	if (err)
	{
		LOG_ERR("Failed to discover characteristics (err %d)", err);
	}

	return BT_GATT_ITER_STOP;
}

/* Discover Battery Service on connected device */
int battery_discover()
{
	if (current_conn_ctx->state != CONN_STATE_BONDED)
	{
		LOG_WRN("Not starting Battery Service discovery - wrong state: %d", current_conn_ctx->state);
		return -EINVAL;
	}

	LOG_DBG("Starting Battery Service discovery");

	if (!battery_discovered)
	{
		static struct bt_gatt_discover_params discover_params;
		memset(&discover_params, 0, sizeof(discover_params));
		discover_params.uuid = BT_UUID_BAS;
		discover_params.type = BT_GATT_DISCOVER_PRIMARY;
		discover_params.start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE;
		discover_params.end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE;
		discover_params.func = discover_service_cb;

		int err = bt_gatt_discover(current_conn_ctx->conn, &discover_params);
		if (err)
		{
			LOG_ERR("Battery Service discovery failed (err %d)", err);
			return err;
		}
	}

	return 0;
}

/* Read battery level */
int battery_read_level(struct connection_context *ctx)
{
	if (!battery_discovered || battery_level_handle == 0)
	{
		LOG_WRN("Battery Service not discovered");
		return -ENOENT;
	}

	if (!ctx->conn)
	{
		LOG_ERR("Invalid connection");
		return -EINVAL;
	}

	LOG_DBG("Reading battery level from handle %u", battery_level_handle);

	battery_read_params.single.handle = battery_level_handle;
	battery_read_params.single.offset = 0;

	int err = bt_gatt_read(ctx->conn, &battery_read_params);
	if (err)
	{
		LOG_ERR("Battery level read failed (err %d)", err);
		return err;
	}

	return 0;
}

/* Subscribe to battery level notifications */
int battery_subscribe_notifications(struct bt_conn *conn)
{
	if (!battery_discovered || battery_level_handle == 0)
	{
		LOG_WRN("Battery Service not discovered");
		return -ENOENT;
	}

	if (!conn)
	{
		LOG_ERR("Invalid connection");
		return -EINVAL;
	}

	if (battery_level_ccc_handle == 0)
	{
		LOG_WRN("CCC descriptor not found, notifications may not be supported");
		return -ENOTSUP;
	}

	LOG_INF("Subscribing to battery level notifications");

	battery_subscribe_params.value_handle = battery_level_handle;
	battery_subscribe_params.ccc_handle = battery_level_ccc_handle;

	int err = bt_gatt_subscribe(conn, &battery_subscribe_params);
	if (err)
	{
		LOG_ERR("Battery notification subscription failed (err %d)", err);
		return err;
	}

	LOG_INF("Successfully subscribed to battery level notifications");
	return 0;
}

/* Reset battery reader state */
void battery_reader_reset(void)
{
	battery_discovered = false;
	battery_level_handle = 0;
	battery_level_ccc_handle = 0;
	battery_level = 0;
	LOG_DBG("Battery reader state reset");
}