#include "connection_strategy.h"
#include "csip_coordinator.h"
#include <zephyr/logging/log.h>
#include <zephyr/bluetooth/audio/csip.h>
#include <string.h>

LOG_MODULE_REGISTER(connection_strategy, LOG_LEVEL_DBG);

/* Forward declarations for strategy handlers */
static int execute_no_bonds_strategy(struct connection_strategy_context *ctx);
static int execute_single_bond_strategy(struct connection_strategy_context *ctx);
static int execute_verified_set_strategy(struct connection_strategy_context *ctx);
static int execute_unverified_set_strategy(struct connection_strategy_context *ctx);
static int execute_multiple_sets_strategy(struct connection_strategy_context *ctx);
static void rsi_scan_timeout_handler(struct k_work *work);

/* State for RSI scanning */
static struct {
    bool active;
    uint8_t searching_device_id;  // Device ID that's searching for its pair
    uint8_t sirk[CSIP_SIRK_SIZE];
    bool sirk_valid;
    struct k_work_delayable timeout_work;
} rsi_scan_state = {
    .active = false,
    .searching_device_id = 0,
    .sirk_valid = false,
};

/* External functions from ble_manager */
extern void scan_for_HIs(void);
extern int schedule_auto_connect(uint8_t device_id);
extern struct device_context *device_ctx;

/**
 * @brief Check if two SIRKs match
 */
bool sirk_match(const uint8_t *sirk1, const uint8_t *sirk2)
{
    if (!sirk1 || !sirk2) {
        return false;
    }
    return memcmp(sirk1, sirk2, CSIP_SIRK_SIZE) == 0;
}

/**
 * @brief Find matching SIRK pairs in the bond collection
 *
 * @param bonds Pointer to bond collection
 * @param idx1 Pointer to store first device index
 * @param idx2 Pointer to store second device index
 * @return true if found a matching pair, false otherwise
 */
static bool find_matching_sirk_pair(struct bond_collection *bonds, uint8_t *idx1, uint8_t *idx2)
{
    // Look for devices with matching SIRKs
    for (uint8_t i = 0; i < bonds->count; i++) {
        if (!bonds->devices[i].has_sirk) {
            continue;
        }

        for (uint8_t j = i + 1; j < bonds->count; j++) {
            if (!bonds->devices[j].has_sirk) {
                continue;
            }

            if (sirk_match(bonds->devices[i].sirk, bonds->devices[j].sirk)) {
                *idx1 = i;
                *idx2 = j;
                LOG_INF("Found matching SIRK pair: device %d (rank %d) and device %d (rank %d)",
                        i, bonds->devices[i].set_rank,
                        j, bonds->devices[j].set_rank);
                return true;
            }
        }
    }

    return false;
}

/**
 * @brief Determine the connection strategy based on available bonds
 */
int determine_connection_strategy(struct connection_strategy_context *ctx)
{
    if (!ctx) {
        return -EINVAL;
    }

    // Initialize context
    memset(ctx, 0, sizeof(struct connection_strategy_context));

    // Enumerate all bonded devices
    int err = enumerate_bonded_devices(&ctx->bonds);
    if (err) {
        LOG_ERR("Failed to enumerate bonded devices (err %d)", err);
        return err;
    }

    LOG_INF("Determining connection strategy for %d bonded device%s",
            ctx->bonds.count, ctx->bonds.count == 1 ? "" : "s");

    // Determine strategy based on bond count
    if (ctx->bonds.count == 0) {
        ctx->strategy = STRATEGY_NO_BONDS;
        LOG_INF("Strategy: NO_BONDS - will start fresh pairing");
    }
    else if (ctx->bonds.count == 1) {
        ctx->strategy = STRATEGY_SINGLE_BOND;
        ctx->primary_device_idx = 0;
        LOG_INF("Strategy: SINGLE_BOND - will search for pair");
    }
    else if (ctx->bonds.count == 2) {
        // Check if both devices have matching SIRKs
        if (ctx->bonds.devices[0].has_sirk && ctx->bonds.devices[1].has_sirk) {
            if (sirk_match(ctx->bonds.devices[0].sirk, ctx->bonds.devices[1].sirk)) {
                ctx->strategy = STRATEGY_VERIFIED_SET;
                ctx->has_matching_set = true;
                // Connect to device with rank 1 (left) first, then rank 2 (right)
                if (ctx->bonds.devices[0].set_rank == 1) {
                    ctx->primary_device_idx = 0;
                    ctx->secondary_device_idx = 1;
                } else {
                    ctx->primary_device_idx = 1;
                    ctx->secondary_device_idx = 0;
                }
                LOG_INF("Strategy: VERIFIED_SET - matching SIRKs, will connect in rank order");
            } else {
                ctx->strategy = STRATEGY_UNVERIFIED_SET;
                ctx->primary_device_idx = 0;
                ctx->secondary_device_idx = 1;
                LOG_INF("Strategy: UNVERIFIED_SET - SIRKs don't match, need verification");
            }
        } else {
            // At least one doesn't have SIRK stored
            ctx->strategy = STRATEGY_UNVERIFIED_SET;
            ctx->primary_device_idx = 0;
            ctx->secondary_device_idx = 1;
            LOG_INF("Strategy: UNVERIFIED_SET - missing SIRK data, need discovery");
        }
    }
    else {
        // 3+ bonds
        ctx->strategy = STRATEGY_MULTIPLE_SETS;
        // Try to find a matching pair
        uint8_t idx1, idx2;
        if (find_matching_sirk_pair(&ctx->bonds, &idx1, &idx2)) {
            ctx->has_matching_set = true;
            // Prefer the device with lower rank (left ear) as primary
            if (ctx->bonds.devices[idx1].set_rank < ctx->bonds.devices[idx2].set_rank) {
                ctx->primary_device_idx = idx1;
                ctx->secondary_device_idx = idx2;
            } else {
                ctx->primary_device_idx = idx2;
                ctx->secondary_device_idx = idx1;
            }
            LOG_INF("Strategy: MULTIPLE_SETS - found matching pair among %d devices",
                    ctx->bonds.count);
        } else {
            // No matching pair found, use first device
            ctx->primary_device_idx = 0;
            ctx->has_matching_set = false;
            LOG_INF("Strategy: MULTIPLE_SETS - no matching pair, will connect to first device");
        }
    }

    return 0;
}

/**
 * @brief Execute the determined connection strategy
 */
int execute_connection_strategy(struct connection_strategy_context *ctx)
{
    if (!ctx) {
        return -EINVAL;
    }

    LOG_INF("Executing connection strategy: %d", ctx->strategy);

    // Save strategy context to state machine for later reference
    memcpy(&g_conn_state_machine.strategy_ctx, ctx, sizeof(struct connection_strategy_context));
    g_conn_state_machine.phase = PHASE_PRIMARY_CONNECTING;

    switch (ctx->strategy) {
        case STRATEGY_NO_BONDS:
            return execute_no_bonds_strategy(ctx);

        case STRATEGY_SINGLE_BOND:
            return execute_single_bond_strategy(ctx);

        case STRATEGY_VERIFIED_SET:
            return execute_verified_set_strategy(ctx);

        case STRATEGY_UNVERIFIED_SET:
            return execute_unverified_set_strategy(ctx);

        case STRATEGY_MULTIPLE_SETS:
            return execute_multiple_sets_strategy(ctx);

        default:
            LOG_ERR("Unknown connection strategy: %d", ctx->strategy);
            return -EINVAL;
    }
}

/* Strategy handler implementations */

/**
 * @brief Execute NO_BONDS strategy - start fresh pairing
 */
static int execute_no_bonds_strategy(struct connection_strategy_context *ctx)
{
    LOG_INF("No bonded devices - starting active scan for fresh pairing");
    scan_for_HIs();
    return 0;
}

/**
 * @brief Execute SINGLE_BOND strategy - connect to single device, search for pair later
 *
 * This strategy handles the case where we have one bonded device. The workflow is:
 * 1. Connect to the single bonded device (done here)
 * 2. After connection and encryption, discover CSIP to get the SIRK (Step 3)
 * 3. Start scanning for advertisements with matching RSI (Step 5)
 * 4. Connect to the second device when found (Step 4)
 */
static int execute_single_bond_strategy(struct connection_strategy_context *ctx)
{
    struct bonded_device_entry *entry = &ctx->bonds.devices[ctx->primary_device_idx];
    struct device_context *dev_ctx = &device_ctx[0];

    // Copy device info to context
    memset(&dev_ctx->info, 0, sizeof(dev_ctx->info));
    bt_addr_le_copy(&dev_ctx->info.addr, &entry->addr);
    dev_ctx->info.connect = true;
    dev_ctx->info.is_new_device = false;
    dev_ctx->info.searching_for_pair = true;  // Flag that we need to find the pair
    strncpy(dev_ctx->info.name, entry->name, BT_NAME_MAX_LEN - 1);

    // Add to filter accept list
    int err = bt_le_filter_accept_list_add(&entry->addr);
    if (err && err != -EALREADY) {
        LOG_ERR("Failed to add device to filter accept list (err %d)", err);
    }

    err = bt_le_set_rpa_timeout(900);
    if (err) {
        LOG_WRN("Failed to set RPA timeout (err %d)", err);
    }

    char addr_str[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(&entry->addr, addr_str, sizeof(addr_str));
    LOG_INF("Connecting to single bonded device: %s", addr_str);
    LOG_INF("  SIRK: %s, Rank: %d", entry->has_sirk ? "yes" : "no", entry->set_rank);
    LOG_INF("  Will search for set pair after CSIP discovery via RSI scanning");

    return schedule_auto_connect(0);
}

/**
 * @brief Execute VERIFIED_SET strategy - connect to both devices in rank order
 */
static int execute_verified_set_strategy(struct connection_strategy_context *ctx)
{
    struct bonded_device_entry *primary = &ctx->bonds.devices[ctx->primary_device_idx];
    struct bonded_device_entry *secondary = &ctx->bonds.devices[ctx->secondary_device_idx];

    LOG_INF("Connecting to verified set:");

    char addr_str1[BT_ADDR_LE_STR_LEN];
    char addr_str2[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(&primary->addr, addr_str1, sizeof(addr_str1));
    bt_addr_le_to_str(&secondary->addr, addr_str2, sizeof(addr_str2));

    LOG_INF("  Primary (rank %d): %s", primary->set_rank, addr_str1);
    LOG_INF("  Secondary (rank %d): %s", secondary->set_rank, addr_str2);

    // Connect to primary device first (device 0)
    struct device_context *dev_ctx0 = &device_ctx[0];
    memset(&dev_ctx0->info, 0, sizeof(dev_ctx0->info));
    bt_addr_le_copy(&dev_ctx0->info.addr, &primary->addr);
    dev_ctx0->info.connect = true;
    dev_ctx0->info.is_new_device = false;
    strncpy(dev_ctx0->info.name, primary->name, BT_NAME_MAX_LEN - 1);

    // Add both devices to filter accept list
    int err = bt_le_filter_accept_list_add(&primary->addr);
    if (err && err != -EALREADY) {
        LOG_ERR("Failed to add primary device to filter accept list (err %d)", err);
    }

    err = bt_le_filter_accept_list_add(&secondary->addr);
    if (err && err != -EALREADY) {
        LOG_ERR("Failed to add secondary device to filter accept list (err %d)", err);
    }

    err = bt_le_set_rpa_timeout(900);
    if (err) {
        LOG_WRN("Failed to set RPA timeout (err %d)", err);
    }

    // Schedule auto-connect for primary device
    // Secondary device will be connected after primary is ready (in Step 4)
    return schedule_auto_connect(0);
}

/**
 * @brief Execute UNVERIFIED_SET strategy - connect and verify SIRKs
 */
static int execute_unverified_set_strategy(struct connection_strategy_context *ctx)
{
    struct bonded_device_entry *primary = &ctx->bonds.devices[ctx->primary_device_idx];

    LOG_INF("Connecting to unverified set - will discover CSIP and verify");

    char addr_str[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(&primary->addr, addr_str, sizeof(addr_str));
    LOG_INF("  Primary device: %s", addr_str);

    // Connect to first device - we'll verify SIRK after connection
    struct device_context *dev_ctx = &device_ctx[0];
    memset(&dev_ctx->info, 0, sizeof(dev_ctx->info));
    bt_addr_le_copy(&dev_ctx->info.addr, &primary->addr);
    dev_ctx->info.connect = true;
    dev_ctx->info.is_new_device = false;
    strncpy(dev_ctx->info.name, primary->name, BT_NAME_MAX_LEN - 1);

    int err = bt_le_filter_accept_list_add(&primary->addr);
    if (err && err != -EALREADY) {
        LOG_ERR("Failed to add device to filter accept list (err %d)", err);
    }

    err = bt_le_set_rpa_timeout(900);
    if (err) {
        LOG_WRN("Failed to set RPA timeout (err %d)", err);
    }

    // Schedule auto-connect work
    // After connection, CSIP discovery will be triggered automatically (in Step 3)
    return schedule_auto_connect(0);
}

/**
 * @brief Execute MULTIPLE_SETS strategy - handle 3+ bonded devices
 */
static int execute_multiple_sets_strategy(struct connection_strategy_context *ctx)
{
    if (ctx->has_matching_set) {
        // Found a matching set, use verified set strategy
        LOG_INF("Multiple bonds with matching set found - using verified set strategy");
        return execute_verified_set_strategy(ctx);
    } else {
        // No clear match, connect to first device only
        LOG_INF("Multiple bonds, no clear match - connecting to first device");
        return execute_single_bond_strategy(ctx);
    }
}

/* ============================================================================
 * Connection State Machine Implementation
 * ============================================================================ */

/* Global state machine instance */
struct connection_state_machine g_conn_state_machine = {
    .phase = PHASE_IDLE,
    .primary_ready = false,
    .secondary_ready = false,
    .set_verified = false,
};

/**
 * @brief Initialize the connection state machine
 */
void connection_state_machine_init(void)
{
    LOG_INF("Initializing connection state machine");
    memset(&g_conn_state_machine, 0, sizeof(g_conn_state_machine));
    g_conn_state_machine.phase = PHASE_IDLE;

    // Initialize RSI scan timeout work
    k_work_init_delayable(&rsi_scan_state.timeout_work, rsi_scan_timeout_handler);
}

/**
 * @brief Connect to secondary device in verified/unverified set strategies
 */
int connection_state_machine_connect_secondary(void)
{
    struct connection_state_machine *sm = &g_conn_state_machine;
    struct bonded_device_entry *secondary =
        &sm->strategy_ctx.bonds.devices[sm->strategy_ctx.secondary_device_idx];

    LOG_INF("Connecting to secondary device");

    char addr_str[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(&secondary->addr, addr_str, sizeof(addr_str));
    LOG_INF("  Secondary device: %s (rank %d)", addr_str, secondary->set_rank);

    // Set up device context for device 1
    struct device_context *dev_ctx1 = &device_ctx[1];
    memset(&dev_ctx1->info, 0, sizeof(dev_ctx1->info));
    bt_addr_le_copy(&dev_ctx1->info.addr, &secondary->addr);
    dev_ctx1->info.connect = true;
    dev_ctx1->info.is_new_device = false;
    strncpy(dev_ctx1->info.name, secondary->name, BT_NAME_MAX_LEN - 1);

    // Update state machine phase
    sm->phase = PHASE_SECONDARY_CONNECTING;

    // Schedule auto-connect for secondary device
    return schedule_auto_connect(1);
}

/**
 * @brief Handle CSIP discovery completion on either device
 *
 * This is called from csip_coordinator after CSIP discovery completes.
 * It progresses the state machine and triggers next actions based on strategy.
 */
void connection_state_machine_on_csip_discovered(uint8_t device_id)
{
    struct connection_state_machine *sm = &g_conn_state_machine;

    LOG_INF("State machine: CSIP discovered on device %d (phase: %d)", device_id, sm->phase);

    // Determine which device completed discovery
    if (device_id == 0) {
        sm->primary_ready = true;
        sm->phase = PHASE_PRIMARY_DISCOVERING;
    } else if (device_id == 1) {
        sm->secondary_ready = true;
        sm->phase = PHASE_SECONDARY_DISCOVERING;
    }

    // Handle next steps based on strategy
    switch (sm->strategy_ctx.strategy) {
        case STRATEGY_SINGLE_BOND:
            if (sm->primary_ready && device_id == 0) {
                LOG_INF("Single bond: Primary device discovered - starting RSI scanning for pair");
                int err = start_rsi_scan_for_pair(device_id);
                if (err) {
                    LOG_ERR("Failed to start RSI scan (err %d)", err);
                    sm->phase = PHASE_COMPLETED;
                } else {
                    sm->phase = PHASE_SECONDARY_CONNECTING;
                }
            }
            break;

        case STRATEGY_VERIFIED_SET:
            if (sm->primary_ready && !sm->secondary_ready && device_id == 0) {
                LOG_INF("Verified set: Primary ready - connecting to secondary device");
                int err = connection_state_machine_connect_secondary();
                if (err) {
                    LOG_ERR("Failed to connect secondary device (err %d)", err);
                }
            } else if (sm->primary_ready && sm->secondary_ready) {
                LOG_INF("Verified set: Both devices ready - verifying SIRK match");
                sm->phase = PHASE_VERIFYING_SET;

                // Verify that SIRKs actually match
                bool verified = csip_verify_set_membership(0, 1);
                if (verified) {
                    LOG_INF("SIRK verification PASSED - set is valid");
                    sm->set_verified = true;
                    sm->phase = PHASE_COMPLETED;
                } else {
                    LOG_ERR("SIRK verification FAILED - stored SIRKs were incorrect!");
                    sm->set_verified = false;
                    // Could disconnect and retry, or handle error differently
                }
            }
            break;

        case STRATEGY_UNVERIFIED_SET:
            if (sm->primary_ready && !sm->secondary_ready && device_id == 0) {
                LOG_INF("Unverified set: Primary ready - connecting to secondary device");
                int err = connection_state_machine_connect_secondary();
                if (err) {
                    LOG_ERR("Failed to connect secondary device (err %d)", err);
                }
            } else if (sm->primary_ready && sm->secondary_ready) {
                LOG_INF("Unverified set: Both devices ready - verifying SIRK match");
                sm->phase = PHASE_VERIFYING_SET;

                // Verify if SIRKs match
                bool verified = csip_verify_set_membership(0, 1);
                if (verified) {
                    LOG_INF("SIRK verification PASSED - devices are in same set");
                    sm->set_verified = true;
                    sm->phase = PHASE_COMPLETED;
                } else {
                    LOG_WRN("SIRK verification FAILED - devices are NOT in same set");
                    LOG_WRN("TODO: Should disconnect second device and search for correct pair");
                    sm->set_verified = false;
                    // In the future: disconnect device 1 and start RSI scanning for correct pair
                }
            }
            break;

        case STRATEGY_MULTIPLE_SETS:
            // Multiple sets follows either verified or single bond logic
            if (sm->strategy_ctx.has_matching_set) {
                // Same as STRATEGY_VERIFIED_SET
                if (sm->primary_ready && !sm->secondary_ready && device_id == 0) {
                    LOG_INF("Multiple sets (matched): Primary ready - connecting to secondary");
                    int err = connection_state_machine_connect_secondary();
                    if (err) {
                        LOG_ERR("Failed to connect secondary device (err %d)", err);
                    }
                } else if (sm->primary_ready && sm->secondary_ready) {
                    sm->phase = PHASE_VERIFYING_SET;
                    bool verified = csip_verify_set_membership(0, 1);
                    sm->set_verified = verified;
                    sm->phase = verified ? PHASE_COMPLETED : PHASE_VERIFYING_SET;
                }
            } else {
                // Same as STRATEGY_SINGLE_BOND
                if (sm->primary_ready && device_id == 0) {
                    LOG_INF("Multiple sets (unmatched): Starting RSI scanning");
                    int err = start_rsi_scan_for_pair(device_id);
                    if (err) {
                        LOG_ERR("Failed to start RSI scan (err %d)", err);
                        sm->phase = PHASE_COMPLETED;
                    } else {
                        sm->phase = PHASE_SECONDARY_CONNECTING;
                    }
                }
            }
            break;

        case STRATEGY_NO_BONDS:
            // No bonds strategy doesn't use state machine
            LOG_DBG("No bonds strategy - state machine not used");
            break;

        default:
            LOG_ERR("Unknown strategy in state machine: %d", sm->strategy_ctx.strategy);
            break;
    }
}

/* ============================================================================
 * RSI Scanning for Pair Discovery
 * ============================================================================ */

/**
 * @brief Timeout handler for RSI scanning
 *
 * Called when 10 seconds elapse without finding a matching set member.
 */
static void rsi_scan_timeout_handler(struct k_work *work)
{
    if (!rsi_scan_state.active) {
        return;
    }

    LOG_WRN("RSI scan timeout - no matching set member found after 10 seconds [DEVICE ID %d]",
            rsi_scan_state.searching_device_id);

    // Stop scanning
    bt_le_scan_stop();

    // Clear scan state
    rsi_scan_state.active = false;
    rsi_scan_state.sirk_valid = false;

    // Notify user/application that pair discovery failed
    // This could trigger fallback behavior or user notification
}

/**
 * @brief Parse advertisement data for RSI and check if it matches our SIRK
 *
 * @param ad Advertisement data buffer
 * @param sirk SIRK to check against
 * @return true if RSI matches SIRK, false otherwise
 */
static bool check_adv_for_rsi_match(struct net_buf_simple *ad, const uint8_t *sirk)
{
    struct bt_data data;
    uint8_t len;

    while (ad->len > 0) {
        len = net_buf_simple_pull_u8(ad);
        if (len == 0) {
            continue;
        }

        if (len > ad->len) {
            LOG_WRN("Malformed advertisement data");
            break;
        }

        data.type = net_buf_simple_pull_u8(ad);
        len--; // Subtract type byte

        data.data_len = len;
        data.data = ad->data;

        // Check if this is RSI data and if it matches our SIRK
        if (bt_csip_set_coordinator_is_set_member(sirk, &data)) {
            return true;
        }

        net_buf_simple_pull(ad, len);
    }

    return false;
}

static bool rsi_device_found(struct bt_data *data, void *user_data)
{
    struct {
        const bt_addr_le_t *addr;
        int8_t rssi;
        uint8_t connect;
    } *info = user_data;

    char addr_str[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(info->addr, addr_str, sizeof(addr_str));

    if (!rsi_scan_state.active || !rsi_scan_state.sirk_valid) {
        return false;
    }

    // Check if advertisement contains RSI matching our SIRK
    if (data->type == BT_DATA_CSIS_RSI) {
        LOG_HEXDUMP_DBG(data->data, data->data_len, "Found RSI data:");
        LOG_DBG("Found RSI advertisement from %s, rssi: %d", addr_str, info->rssi);
        LOG_HEXDUMP_DBG(rsi_scan_state.sirk, CSIP_SIRK_SIZE, "Checking against SIRK:");
        if (bt_csip_set_coordinator_is_set_member(rsi_scan_state.sirk, data)) {
            LOG_INF("RSI matches SIRK from device %d! Address: %s, RSSI: %d",
            rsi_scan_state.searching_device_id, addr_str, info->rssi);
            info->connect = 1;
            return false;
        }
    }

    return true;
}

/**
 * @brief Scan callback for finding set pair via RSI
 */
static void advertisement_found_cb(const bt_addr_le_t *addr, int8_t rssi, uint8_t type,
                                   struct net_buf_simple *ad)
{
    char addr_str[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));

    struct {
        const bt_addr_le_t *addr;
        int8_t rssi;
        uint8_t connect;
    } info = {
        .addr = addr,
        .rssi = rssi,
        .connect = 0,
    };

    bt_data_parse(ad, rsi_device_found, &info);

    if (info.connect) {
        LOG_INF("Found set member with matching RSI: %s (RSSI: %d)", addr_str, rssi);

        // Cancel timeout since we found the pair
        k_work_cancel_delayable(&rsi_scan_state.timeout_work);

        // Stop scanning
        bt_le_scan_stop();
        rsi_scan_state.active = false;

        // Determine which device slot to use (opposite of searching device)
        uint8_t target_device_id = (rsi_scan_state.searching_device_id == 0) ? 1 : 0;

        // Set up device context for second device
        struct device_context *dev_ctx = &device_ctx[target_device_id];
        if (dev_ctx->conn != NULL) {
            LOG_ERR("Device slot %d already occupied, cannot connect to pair", target_device_id);
            return;
        }

        memset(&dev_ctx->info, 0, sizeof(dev_ctx->info));
        bt_addr_le_copy(&dev_ctx->info.addr, addr);
        dev_ctx->info.connect = true;
        dev_ctx->info.is_new_device = true; // This is a new pairing
        strncpy(dev_ctx->info.name, "HARC HI", BT_NAME_MAX_LEN - 1);

        // Connect to the device
        LOG_INF("Connecting to discovered set member [DEVICE ID %d]", target_device_id);
        int err = bt_conn_le_create(addr, BT_CONN_LE_CREATE_CONN,
                                     BT_LE_CONN_PARAM_DEFAULT, &dev_ctx->conn);
        if (err) {
            LOG_ERR("Failed to create connection to pair device (err %d)", err);
            // Restart scanning on error
            start_rsi_scan_for_pair(rsi_scan_state.searching_device_id);
        }
    }
}

/**
 * @brief Start scanning for set pair using RSI
 *
 * @param device_id Device ID that has SIRK and is searching for its pair
 * @return 0 on success, negative error code on failure
 */
int start_rsi_scan_for_pair(uint8_t device_id)
{
    if (device_id > 1) {
        return -EINVAL;
    }

    LOG_INF("Starting RSI scan for set pair [DEVICE ID %d]", device_id);

    // Get SIRK from the device
    uint8_t sirk[CSIP_SIRK_SIZE];
    uint8_t rank;
    if (!csip_get_sirk(device_id, sirk, &rank)) {
        LOG_ERR("Cannot start RSI scan - SIRK not available for device %d", device_id);
        return -ENOENT;
    }

    LOG_INF("Using SIRK from device %d (rank %d) to search for pair", device_id, rank);
    LOG_HEXDUMP_DBG(sirk, CSIP_SIRK_SIZE, "SIRK:");

    // Save scan state
    rsi_scan_state.active = true;
    rsi_scan_state.searching_device_id = device_id;
    memcpy(rsi_scan_state.sirk, sirk, CSIP_SIRK_SIZE);
    rsi_scan_state.sirk_valid = true;

    // Start active scanning
    struct bt_le_scan_param scan_param = {
        .type = BT_LE_SCAN_TYPE_ACTIVE,
        .interval = BT_GAP_SCAN_FAST_INTERVAL,
        .window = BT_GAP_SCAN_FAST_WINDOW,
    };

    int err = bt_le_scan_start(&scan_param, advertisement_found_cb);
    if (err) {
        LOG_ERR("Failed to start RSI scan (err %d)", err);
        rsi_scan_state.active = false;
        rsi_scan_state.sirk_valid = false;
        return err;
    }

    // Schedule 10-second timeout per CSIP specification
    k_work_schedule(&rsi_scan_state.timeout_work, K_SECONDS(10));

    LOG_INF("RSI scan started successfully (10 second timeout)");
    return 0;
}

/**
 * @brief Stop RSI scanning for pair
 */
void stop_rsi_scan_for_pair(void)
{
    if (rsi_scan_state.active) {
        LOG_INF("Stopping RSI scan");
        k_work_cancel_delayable(&rsi_scan_state.timeout_work);
        bt_le_scan_stop();
        rsi_scan_state.active = false;
        rsi_scan_state.sirk_valid = false;
    }
}
