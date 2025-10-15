#include "vcp_controller.h"

LOG_MODULE_REGISTER(vcp_controller, LOG_LEVEL_INF);

/* Global state variables */
struct bt_vcp_vol_ctlr *vol_ctlr;
bool vcp_discovered = false;
bool volume_direction = true;

/* Command queue state */
static sys_slist_t vcp_cmd_queue;
static struct k_mutex vcp_queue_mutex;
static struct k_sem vcp_cmd_sem;
static struct k_work_delayable vcp_cmd_timeout_work;
static struct k_work_delayable vcp_cmd_retry_work;
static struct vcp_cmd *current_cmd = NULL;
static bool vcp_cmd_in_progress = false;

/* Memory pool for commands */
K_MEM_SLAB_DEFINE(vcp_cmd_slab, sizeof(struct vcp_cmd), VCP_CMD_QUEUE_SIZE, 4);

/* Forward declarations */
static void vcp_process_next_command(void);
static void vcp_cmd_timeout_handler(struct k_work *work);
static void vcp_cmd_retry_handler(struct k_work *work);  // NEW

/* Command queue initialization */
static int vcp_queue_init(void)
{
    sys_slist_init(&vcp_cmd_queue);
    k_mutex_init(&vcp_queue_mutex);
    k_sem_init(&vcp_cmd_sem, 0, 1);
    k_work_init_delayable(&vcp_cmd_timeout_work, vcp_cmd_timeout_handler);
    k_work_init_delayable(&vcp_cmd_retry_work, vcp_cmd_retry_handler);  // NEW
    
    return 0;
}

/* Allocate a command from memory pool */
static struct vcp_cmd *vcp_cmd_alloc(void)
{
    struct vcp_cmd *cmd;
    
    if (k_mem_slab_alloc(&vcp_cmd_slab, (void **)&cmd, K_NO_WAIT) != 0) {
        LOG_ERR("Failed to allocate command - queue full");
        return NULL;
    }
    
    memset(cmd, 0, sizeof(struct vcp_cmd));
    return cmd;
}

/* Free a command back to memory pool */
static void vcp_cmd_free(struct vcp_cmd *cmd)
{
    if (cmd) {
        k_mem_slab_free(&vcp_cmd_slab, (void *)cmd);
    }
}

/* Enqueue a command */
static int vcp_cmd_enqueue(struct vcp_cmd *cmd)
{
    if (!cmd) {
        return -EINVAL;
    }
    
    k_mutex_lock(&vcp_queue_mutex, K_FOREVER);
    sys_slist_append(&vcp_cmd_queue, &cmd->node);
    k_mutex_unlock(&vcp_queue_mutex);
    
    // Signal the processing thread
    k_sem_give(&vcp_cmd_sem);
    
    LOG_DBG("Command enqueued, type: %d", cmd->type);
    return 0;
}

/* Dequeue a command */
static struct vcp_cmd *vcp_cmd_dequeue(void)
{
    struct vcp_cmd *cmd = NULL;
    
    k_mutex_lock(&vcp_queue_mutex, K_FOREVER);
    sys_snode_t *node = sys_slist_get(&vcp_cmd_queue);
    if (node) {
        cmd = CONTAINER_OF(node, struct vcp_cmd, node);
    }
    k_mutex_unlock(&vcp_queue_mutex);
    
    return cmd;
}

/* Execute a single command */
static int vcp_execute_command(struct vcp_cmd *cmd)
{
    int err = 0;

    // DISCOVER command doesn't need vol_ctlr or vcp_discovered
    // All other commands require both
    if (cmd->type != VCP_CMD_DISCOVER) {
        if (!vcp_discovered || !vol_ctlr) {
            LOG_WRN("VCP not ready");
            return -ENOTCONN;
        }
    }

    bt_security_t sec = bt_conn_get_security(conn_ctx->conn);
    LOG_DBG("Executing command type %d, security: %d", cmd->type, sec);
    
    switch (cmd->type) {
    case VCP_CMD_DISCOVER:
        err = bt_vcp_vol_ctlr_discover(conn_ctx->conn, &vol_ctlr);
        break;
    case VCP_CMD_VOLUME_UP:
        err = bt_vcp_vol_ctlr_vol_up(vol_ctlr);
        break;
        
    case VCP_CMD_VOLUME_DOWN:
        err = bt_vcp_vol_ctlr_vol_down(vol_ctlr);
        break;
        
    case VCP_CMD_SET_VOLUME:
        err = bt_vcp_vol_ctlr_set_vol(vol_ctlr, cmd->d0);
        break;
        
    case VCP_CMD_MUTE:
        err = bt_vcp_vol_ctlr_mute(vol_ctlr);
        break;
        
    case VCP_CMD_UNMUTE:
        err = bt_vcp_vol_ctlr_unmute(vol_ctlr);
        break;
        
    case VCP_CMD_READ_STATE:
        err = bt_vcp_vol_ctlr_read_state(vol_ctlr);
        break;
        
    case VCP_CMD_READ_FLAGS:
        err = bt_vcp_vol_ctlr_read_flags(vol_ctlr);
        break;
        
    default:
        LOG_ERR("Unknown command type: %d", cmd->type);
        err = -EINVAL;
        break;
    }
    
    if (err) {
        LOG_ERR("Command execution failed: type=%d, err=%d", cmd->type, err);
    } else {
        LOG_DBG("Command initiated successfully: type=%d", cmd->type);
    }
    
    return err;
}

/* NEW: Retry handler - re-executes the current command */
static void vcp_cmd_retry_handler(struct k_work *work)
{
    if (!current_cmd) {
        LOG_ERR("Retry handler called but no current command");
        vcp_cmd_in_progress = false;
        vcp_process_next_command();
        return;
    }
    
    LOG_DBG("Retrying command type %d (attempt %d)", 
            current_cmd->type, current_cmd->retry_count);
    
    // Re-execute the command
    int err = vcp_execute_command(current_cmd);
    
    if (err) {
        LOG_ERR("Retry failed to initiate command: %d", err);
        vcp_cmd_free(current_cmd);
        current_cmd = NULL;
        vcp_cmd_in_progress = false;
        vcp_process_next_command();
        return;
    }
    
    // Start timeout for the retry
    k_work_schedule(&vcp_cmd_timeout_work, K_MSEC(VCP_CMD_TIMEOUT_MS));
}

/* UPDATED: Handle command timeout */
static void vcp_cmd_timeout_handler(struct k_work *work)
{
    LOG_ERR("VCP command timeout");
    
    if (!current_cmd) {
        LOG_WRN("Timeout but no current command");
        vcp_cmd_in_progress = false;
        vcp_process_next_command();
        return;
    }
    
    LOG_ERR("Command type %d timed out", current_cmd->type);
    
    // Check if we should retry on timeout
    if (current_cmd->retry_count < VCP_CMD_MAX_RETRIES) {
        current_cmd->retry_count++;
        LOG_INF("Retrying timed-out command type %d (attempt %d)", 
                current_cmd->type, current_cmd->retry_count);
        
        // Schedule retry (keeps vcp_cmd_in_progress = true and current_cmd valid)
        k_work_schedule(&vcp_cmd_retry_work, K_MSEC(500));
        return;
    }
    
    // Max retries exceeded
    LOG_ERR("Command type %d exceeded max retries", current_cmd->type);
    vcp_cmd_free(current_cmd);
    current_cmd = NULL;
    vcp_cmd_in_progress = false;
    
    // Process next command
    vcp_process_next_command();
}

/* UPDATED: Mark command as complete (called from callbacks) */
static void vcp_cmd_complete(int err)
{
    // Cancel timeout
    k_work_cancel_delayable(&vcp_cmd_timeout_work);

    if (!current_cmd) {
        LOG_WRN("Command complete but no current command");
        return;
    }

    if (err) {
        LOG_WRN("Command failed: type=%d, err=%d, retry=%d/%d",
                current_cmd->type, err, (current_cmd->retry_count)+1, VCP_CMD_MAX_RETRIES);

        if (++(current_cmd->retry_count) < VCP_CMD_MAX_RETRIES) {
            LOG_DBG("Retrying command type %d (attempt %d)",
                    current_cmd->type, current_cmd->retry_count);

            // Keep vcp_cmd_in_progress = true and current_cmd valid
            // Schedule retry after delay
            k_work_schedule(&vcp_cmd_retry_work, K_MSEC(500));
            return;
        }

        LOG_ERR("Command failed permanently: type=%d", current_cmd->type);
        if (err == 15) {
            disconnect(conn_ctx->conn, NULL);
        }
    } else {
        LOG_DBG("Command completed successfully: type=%d", current_cmd->type);
    }

    // Free the command
    vcp_cmd_free(current_cmd);
    current_cmd = NULL;
    vcp_cmd_in_progress = false;

    // Notify BLE manager of completion
    ble_cmd_complete(err);

    // Process next command
    vcp_process_next_command();
}

/* Process the next command in the queue */
static void vcp_process_next_command(void)
{
    if (vcp_cmd_in_progress) {
        LOG_DBG("Command already in progress, skipping");
        return;
    }
    
    struct vcp_cmd *cmd = vcp_cmd_dequeue();
    if (!cmd) {
        LOG_DBG("No commands in queue");
        return;
    }
    
    current_cmd = cmd;
    vcp_cmd_in_progress = true;
    
    // Execute the command
    int err = vcp_execute_command(cmd);
    
    if (err) {
        // Command failed to initiate
        LOG_ERR("Failed to initiate command: %d", err);
        
        if (err == -ENOTCONN) {
            // VCP not ready, re-queue and try later
            k_mutex_lock(&vcp_queue_mutex, K_FOREVER);
            sys_slist_prepend(&vcp_cmd_queue, &cmd->node);
            k_mutex_unlock(&vcp_queue_mutex);
            current_cmd = NULL;
            vcp_cmd_in_progress = false;
            
            // Retry after delay
            k_work_schedule(&vcp_cmd_timeout_work, K_MSEC(1000));
        } else {
            // Other error, drop command
            vcp_cmd_free(cmd);
            current_cmd = NULL;
            vcp_cmd_in_progress = false;
            
            // Try next command
            vcp_process_next_command();
        }
        return;
    }
    
    // Start timeout for this command
    k_work_schedule(&vcp_cmd_timeout_work, K_MSEC(VCP_CMD_TIMEOUT_MS));
}

/* Command processing thread */
static void vcp_cmd_thread()
{   
    LOG_INF("VCP command thread started");
    
    while (1) {
        // Wait for a command to be enqueued
        k_sem_take(&vcp_cmd_sem, K_FOREVER);
        
        // Process the next command
        vcp_process_next_command();
    }
}

/* Command thread */
K_THREAD_DEFINE(vcp_cmd_thread_id, 1024, vcp_cmd_thread, NULL, NULL, NULL, 7, 0, 0);

int vcp_cmd_discover(void)
{
    struct vcp_cmd *cmd = vcp_cmd_alloc();
    if (!cmd) {
        return -ENOMEM;
    }
    
    cmd->type = VCP_CMD_DISCOVER;
    return vcp_cmd_enqueue(cmd);
}

int vcp_cmd_read_state(void)
{
	struct vcp_cmd *cmd = vcp_cmd_alloc();
	if (!cmd) {
		return -ENOMEM;
	}
	
	cmd->type = VCP_CMD_READ_STATE;
	return vcp_cmd_enqueue(cmd);
}

int vcp_cmd_read_flags(void)
{
	struct vcp_cmd *cmd = vcp_cmd_alloc();
	if (!cmd) {
		return -ENOMEM;
	}
	
	cmd->type = VCP_CMD_READ_FLAGS;
	return vcp_cmd_enqueue(cmd);
}

int vcp_cmd_volume_up(void)
{
    struct vcp_cmd *cmd = vcp_cmd_alloc();
    if (!cmd) {
        return -ENOMEM;
    }
    
    cmd->type = VCP_CMD_VOLUME_UP;
    return vcp_cmd_enqueue(cmd);
}

int vcp_cmd_volume_down(void)
{
    struct vcp_cmd *cmd = vcp_cmd_alloc();
    if (!cmd) {
        return -ENOMEM;
    }
    
    cmd->type = VCP_CMD_VOLUME_DOWN;
    return vcp_cmd_enqueue(cmd);
}

int vcp_cmd_set_volume(uint8_t volume)
{
    struct vcp_cmd *cmd = vcp_cmd_alloc();
    if (!cmd) {
        return -ENOMEM;
    }
    
    cmd->type = VCP_CMD_SET_VOLUME;
    cmd->d0 = volume;
    return vcp_cmd_enqueue(cmd);
}

int vcp_cmd_mute(void)
{
    struct vcp_cmd *cmd = vcp_cmd_alloc();
    if (!cmd) {
        return -ENOMEM;
    }
    
    cmd->type = VCP_CMD_MUTE;
    return vcp_cmd_enqueue(cmd);
}

int vcp_cmd_unmute(void)
{
    struct vcp_cmd *cmd = vcp_cmd_alloc();
    if (!cmd) {
        return -ENOMEM;
    }
    
    cmd->type = VCP_CMD_UNMUTE;
    return vcp_cmd_enqueue(cmd);
}

/* VCP callback implementations */
static void vcp_state_cb(struct bt_vcp_vol_ctlr *vol_ctlr, int err,
                         uint8_t volume, uint8_t mute)
{
    if (err) {
        LOG_ERR("VCP state error (err %d)", err);
        vcp_cmd_complete(err);
        return;
    }
    
    float volume_percent = (float)volume * 100.0f / 255.0f;
    LOG_INF("VCP state - Volume: %u%%, Mute: %u", (uint8_t)(volume_percent), mute);

    if (volume >= 255) {
        volume_direction = false;
    } else if (volume <= 0) {
        volume_direction = true;
    }

    // Mark as complete only if this was a READ_STATE command
    if (current_cmd && current_cmd->type == VCP_CMD_READ_STATE) {
        vcp_cmd_complete(0);
    }
}

static void vcp_flags_cb(struct bt_vcp_vol_ctlr *vol_ctlr, int err, uint8_t flags)
{
    if (err) {
        LOG_ERR("VCP flags error (err %d)", err);
        vcp_cmd_complete(err);
        return;
    }

    LOG_DBG("VCP flags: 0x%02X", flags);
    
    // Mark as complete only if this was a READ_FLAGS command
    if (current_cmd && current_cmd->type == VCP_CMD_READ_FLAGS) {
        vcp_cmd_complete(0);
    }
}

static void vcp_discover_cb(struct bt_vcp_vol_ctlr *vcp_vol_ctlr, int err,
                uint8_t vocs_count, uint8_t aics_count)
{
    if (err) {
        LOG_ERR("VCP discovery failed (err %d)", err);
        vcp_cmd_complete(err);
        return;
    }

    LOG_INF("VCP discovery complete - VOCS: %u, AICS: %u", vocs_count, aics_count);

    vol_ctlr = vcp_vol_ctlr;

	vcp_discovered = true;

	// Mark discovery command as complete
	vcp_cmd_complete(0);

	// Initial reads
	vcp_cmd_read_state();
	vcp_cmd_read_flags();
}

static void vcp_vol_down_cb(struct bt_vcp_vol_ctlr *vol_ctlr, int err)
{
    if (err) {
        LOG_ERR("VCP volume down error (err %d)", err);
    } else {
        LOG_INF("Volume down success");
    }
    
    vcp_cmd_complete(err);
}

static void vcp_vol_up_cb(struct bt_vcp_vol_ctlr *vol_ctlr, int err)
{
    if (err) {
        LOG_ERR("VCP volume up error (err %d)", err);
    } else {
        LOG_INF("Volume up success");
    }
    
    vcp_cmd_complete(err);
}

static void vcp_mute_cb(struct bt_vcp_vol_ctlr *vol_ctlr, int err)
{
    if (err) {
        LOG_ERR("VCP mute error (err %d)", err);
    } else {
        LOG_INF("Mute success");
    }
    
    vcp_cmd_complete(err);
}

static void vcp_unmute_cb(struct bt_vcp_vol_ctlr *vol_ctlr, int err)
{
    if (err) {
        LOG_ERR("VCP unmute error (err %d)", err);
    } else {
        LOG_INF("Unmute success");
    }
    
    vcp_cmd_complete(err);
}

static void vcp_vol_up_unmute_cb(struct bt_vcp_vol_ctlr *vol_ctlr, int err)
{
    if (err) {
        LOG_ERR("VCP volume up and unmute error (err %d)", err);
    } else {
        LOG_INF("Volume up and unmute success");
    }
    
    vcp_cmd_complete(err);
}

static void vcp_vol_down_unmute_cb(struct bt_vcp_vol_ctlr *vol_ctlr, int err)
{
    if (err) {
        LOG_ERR("VCP volume down and unmute error (err %d)", err);
    } else {
        LOG_INF("Volume down and unmute success");
    }
    
    vcp_cmd_complete(err);
}

static struct bt_vcp_vol_ctlr_cb vcp_callbacks = {
    .state = vcp_state_cb,
    .flags = vcp_flags_cb,
    .discover = vcp_discover_cb,
    .vol_down = vcp_vol_down_cb,
    .vol_up = vcp_vol_up_cb,
    .mute = vcp_mute_cb,
    .unmute = vcp_unmute_cb,
    .vol_up_unmute = vcp_vol_up_unmute_cb,
    .vol_down_unmute = vcp_vol_down_unmute_cb,
    .vol_set = NULL,
};

/* Initialize VCP controller */
int vcp_controller_init(void)
{
    int err;

    err = bt_vcp_vol_ctlr_cb_register(&vcp_callbacks);
    if (err) {
        LOG_ERR("Failed to register VCP callbacks (err %d)", err);
        return err;
    }

    err = vcp_queue_init();
    if (err) {
        LOG_ERR("Failed to initialize VCP queue (err %d)", err);
        return err;
    }

    vcp_controller_reset();

    LOG_INF("VCP controller initialized");

    return 0;
}

/* Reset VCP controller state */
void vcp_controller_reset(void)
{
    vcp_discovered = false;
    vol_ctlr = NULL;

    // Clear command queue
    k_mutex_lock(&vcp_queue_mutex, K_FOREVER);
    struct vcp_cmd *cmd;
    while ((cmd = (struct vcp_cmd *)sys_slist_get(&vcp_cmd_queue)) != NULL) {
        vcp_cmd_free(cmd);
    }
    k_mutex_unlock(&vcp_queue_mutex);

    // Cancel any pending command
    if (current_cmd) {
        vcp_cmd_free(current_cmd);
        current_cmd = NULL;
    }

    vcp_cmd_in_progress = false;
    k_work_cancel_delayable(&vcp_cmd_timeout_work);
    k_work_cancel_delayable(&vcp_cmd_retry_work);
}