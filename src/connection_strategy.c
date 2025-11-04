#include "connection_strategy.h"
#include "csip_coordinator.h"
#include <zephyr/logging/log.h>
#include <string.h>

LOG_MODULE_REGISTER(connection_strategy, LOG_LEVEL_DBG);

/* Forward declarations for strategy handlers */
static int execute_no_bonds_strategy(struct connection_strategy_context *ctx);
static int execute_single_bond_strategy(struct connection_strategy_context *ctx);
static int execute_verified_set_strategy(struct connection_strategy_context *ctx);
static int execute_unverified_set_strategy(struct connection_strategy_context *ctx);
static int execute_multiple_sets_strategy(struct connection_strategy_context *ctx);

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
    LOG_INF("  Will search for set pair after CSIP discovery");

    // TODO (Step 5): After CSIP discovery callback, trigger scan for RSI with matching SIRK

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
