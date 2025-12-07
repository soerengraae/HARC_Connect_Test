#include "battery_reader.h"
#include "bas_settings.h"
#include "devices_manager.h"
#include "app_controller.h"
#include "display_manager.h"

LOG_MODULE_REGISTER(battery_reader, LOG_LEVEL_DBG);

/* Global state variables */
bool battery_discovered = false;
uint8_t battery_level = 0;

/* Read callback for battery level characteristic */
static uint8_t battery_read_cb(struct bt_conn *conn, uint8_t err,
							   struct bt_gatt_read_params *params,
							   const void *data, uint16_t length)
{
	struct device_context *ctx = devices_manager_get_device_context_by_conn(conn);

	if (err)
	{
		LOG_ERR("Battery level read failed (err %u) [DEVICE ID %d]", err, ctx->device_id);
		return BT_GATT_ITER_STOP;
	}

	if (!data)
	{
		LOG_DBG("Battery level read complete [DEVICE ID %d]", ctx->device_id);
		return BT_GATT_ITER_STOP;
	}

	if (length != 1)
	{
		LOG_WRN("Unexpected battery level length: %u [DEVICE ID %d]", length, ctx->device_id);
		return BT_GATT_ITER_STOP;
	}

	ctx->bas_ctlr.battery_level = *(uint8_t *)data;
	LOG_INF("Battery level read: %u%% [DEVICE ID %d]", ctx->bas_ctlr.battery_level, ctx->device_id);

	/* Update display with battery level */
	display_manager_update_battery(ctx->device_id, ctx->bas_ctlr.battery_level);

	ble_cmd_complete(ctx->device_id, 0);

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
	struct device_context *ctx = devices_manager_get_device_context_by_conn(conn);
	struct bt_gatt_chrc *chrc;
	
	if (!attr) {
		LOG_DBG("Discovery complete for type %d [DEVICE ID %d]", params->type, ctx->device_id);

		/* If we have the characteristic handle, mark discovery as complete */
		if (ctx->bas_ctlr.battery_level_handle != 0) {
			ctx->info.bas_discovered = true;

			/* Cache handles for future reconnections */
			struct bt_bas_handles handles = {
				.service_handle = ctx->bas_ctlr.battery_service_handle,
				.service_handle_end = ctx->bas_ctlr.battery_service_handle_end,
				.battery_level_handle = ctx->bas_ctlr.battery_level_handle,
			};
			bas_settings_store_handles(&ctx->info.addr, &handles);

			// Complete the discovery command
			LOG_DBG("Battery Service discovery complete (handle: 0x%04x, CCC: 0x%04x) [DEVICE ID %d]",
			        ctx->bas_ctlr.battery_level_handle, ctx->bas_ctlr.battery_level_ccc_handle, ctx->device_id);

			app_controller_notify_bas_discovered(ctx->device_id, 0);
			ble_cmd_complete(ctx->device_id, 0);
		} else {
			LOG_ERR("Battery Service discovery completed but no characteristic found [DEVICE ID %d]", ctx->device_id);
			app_controller_notify_bas_discovered(ctx->device_id, -EINVAL);
			ble_cmd_complete(ctx->device_id, -EINVAL);
		}

		return BT_GATT_ITER_STOP;
	}

	LOG_DBG("[ATTRIBUTE] handle 0x%04X [DEVICE ID %d]", attr->handle, ctx->device_id);

	if (params->type == BT_GATT_DISCOVER_CHARACTERISTIC) {
		chrc = (struct bt_gatt_chrc *)attr->user_data;
		
		if (!bt_uuid_cmp(chrc->uuid, BT_UUID_BAS_BATTERY_LEVEL)) {
			LOG_DBG("Found Battery Level characteristic at handle 0x%04X (properties 0x%02X) [DEVICE ID %d]", chrc->value_handle, chrc->properties, ctx->device_id);
			ctx->bas_ctlr.battery_level_handle = chrc->value_handle;

			// return BT_GATT_ITER_STOP;
		}
	}

	return BT_GATT_ITER_CONTINUE;
}

/* Discovery callback for Battery Service */
static uint8_t discover_service_cb(struct bt_conn *conn,
								   const struct bt_gatt_attr *attr,
								   struct bt_gatt_discover_params *params)
{
	struct device_context *ctx = devices_manager_get_device_context_by_conn(conn);

	if (!attr)
	{
		LOG_WRN("Battery Service not found [DEVICE ID %d]", ctx->device_id);
		return BT_GATT_ITER_STOP;
	}

	struct bt_gatt_service_val *svc = (struct bt_gatt_service_val *)attr->user_data;

	LOG_DBG("Found Battery Service at handle 0x%04X-0x%04X [DEVICE ID %d]", attr->handle, svc->end_handle, ctx->device_id);
	ctx->bas_ctlr.battery_service_handle = attr->handle;
	ctx->bas_ctlr.battery_service_handle_end = svc->end_handle;
	LOG_DBG("Discover characteristics within Battery Service [DEVICE ID %d]", ctx->device_id);

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
		LOG_ERR("Failed to discover characteristics (err %d) [DEVICE ID %d]", err, ctx->device_id);
	}

	return BT_GATT_ITER_STOP;
}

/* Discover Battery Service on connected device */
int battery_discover(uint8_t device_id)
{
	struct device_context *ctx = devices_manager_get_device_context_by_id(device_id);

	if (!ctx || !ctx->conn)
	{
		LOG_ERR("Invalid connection context [DEVICE ID %d]", ctx->device_id);
		return -EINVAL;
	}

	if (ctx->state != CONN_STATE_READY)
	{
		LOG_WRN("Not starting Battery Service discovery - wrong state: %d [DEVICE ID %d]", ctx->state, ctx->device_id);
		return -EINVAL;
	}

	LOG_DBG("Starting Battery Service discovery [DEVICE ID %d]", ctx->device_id);

	if (!ctx->info.bas_discovered)
	{
		/* Try to load cached handles first */
		struct bt_bas_handles cached_handles;
		int load_err = bas_settings_load_handles(&ctx->info.addr, &cached_handles);
		if (load_err == 0) {
			LOG_INF("Loaded cached BAS handles - skipping discovery [DEVICE ID %d]", device_id);
			ctx->bas_ctlr.battery_service_handle = cached_handles.service_handle;
			ctx->bas_ctlr.battery_service_handle_end = cached_handles.service_handle_end;
			ctx->bas_ctlr.battery_level_handle = cached_handles.battery_level_handle;
			ctx->info.bas_discovered = true;

			app_controller_notify_bas_discovered(ctx->device_id, 0);
			ble_cmd_complete(ctx->device_id, 0);
			return 0;
		}

		/* No cached handles, perform full discovery */
		static struct bt_gatt_discover_params discover_params;
		memset(&discover_params, 0, sizeof(discover_params));
		discover_params.uuid = BT_UUID_BAS;
		discover_params.type = BT_GATT_DISCOVER_PRIMARY;
		discover_params.start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE;
		discover_params.end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE;
		discover_params.func = discover_service_cb;

		int err = bt_gatt_discover(ctx->conn, &discover_params);
		if (err)
		{
			LOG_ERR("Battery Service discovery failed (err %d) [DEVICE ID %d]", err, ctx->device_id);
			return err;
		}
	} else {
		LOG_DBG("Battery Service already discovered [DEVICE ID %d]", ctx->device_id);
	}

	return 0;
}

/* Read battery level */
int battery_read_level(uint8_t device_id)
{
	struct device_context *ctx = devices_manager_get_device_context_by_id(device_id);
	
	if (!ctx->conn)
	{
		LOG_ERR("Invalid connection [DEVICE ID %d]", ctx->device_id);
		return -EINVAL;
	}

	if (!ctx->info.bas_discovered || ctx->bas_ctlr.battery_level_handle == 0)
	{
		LOG_WRN("Battery Service not discovered [DEVICE ID %d]", ctx->device_id);
		return -ENOENT;
	}

	LOG_DBG("Reading battery level from handle 0x%04X [DEVICE ID %d]", ctx->bas_ctlr.battery_level_handle, ctx->device_id);

	battery_read_params.single.handle = ctx->bas_ctlr.battery_level_handle;
	battery_read_params.single.offset = 0;

	int err = bt_gatt_read(ctx->conn, &battery_read_params);
	if (err)
	{
		LOG_ERR("Battery level read failed (err %d) [DEVICE ID %d]", err, ctx->device_id);
		return err;
	}

	return 0;
}

/* Reset battery reader state */
void battery_reader_reset(uint8_t device_id)
{
	struct device_context *ctx = devices_manager_get_device_context_by_id(device_id);

	ctx->info.bas_discovered = false;
	ctx->bas_ctlr.battery_level_handle = 0;
	ctx->bas_ctlr.battery_level_ccc_handle = 0;
	ctx->bas_ctlr.battery_level = 0;
	LOG_DBG("Battery reader state reset [DEVICE ID %d]", ctx->device_id);
}

int battery_reader_init(void) {
	// This does nothing but I wanted consistency lol, feel free to remove if you want :)
	LOG_INF("Battery reader initialized");

	return 0;
}