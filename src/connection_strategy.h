#ifndef CONNECTION_STRATEGY_H
#define CONNECTION_STRATEGY_H

#include "ble_manager.h"
#include <zephyr/bluetooth/bluetooth.h>

/**
 * @brief Connection strategy types based on available bonded devices
 */
enum connection_strategy {
    STRATEGY_NO_BONDS,           // No bonded devices - start fresh pairing
    STRATEGY_SINGLE_BOND,        // One device bonded - search for its pair
    STRATEGY_VERIFIED_SET,       // Two bonds with matching stored SIRKs
    STRATEGY_UNVERIFIED_SET,     // Two bonds, need SIRK verification
    STRATEGY_MULTIPLE_SETS       // 3+ bonds, need selection logic
};

/**
 * @brief Context for connection strategy execution
 */
struct connection_strategy_context {
    enum connection_strategy strategy;
    struct bond_collection bonds;
    uint8_t primary_device_idx;     // Index of device to connect first
    uint8_t secondary_device_idx;   // Index of device to connect second
    bool has_matching_set;          // True if found matching SIRK pair
};

/**
 * @brief Determine the connection strategy based on available bonds
 *
 * @param ctx Pointer to strategy context to fill
 * @return 0 on success, negative error code on failure
 */
int determine_connection_strategy(struct connection_strategy_context *ctx);

/**
 * @brief Execute the determined connection strategy
 *
 * @param ctx Pointer to strategy context
 * @return 0 on success, negative error code on failure
 */
int execute_connection_strategy(struct connection_strategy_context *ctx);

/**
 * @brief Check if two SIRKs match (for set verification)
 *
 * @param sirk1 First SIRK (16 bytes)
 * @param sirk2 Second SIRK (16 bytes)
 * @return true if SIRKs match, false otherwise
 */
bool sirk_match(const uint8_t *sirk1, const uint8_t *sirk2);

#endif /* CONNECTION_STRATEGY_H */
