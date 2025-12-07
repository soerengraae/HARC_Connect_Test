# Zephyr Patches for HARC Connect Test

This directory contains patches for the Zephyr RTOS that add handle caching functionality for Bluetooth Audio profiles.

## Zephyr Version

These patches are based on Zephyr version: **v4.2.0-3327-gf539c41d7b1**

## Patches

### 0001-Bluetooth-audio-HAS-Add-handle-getter-setter-APIs-fo.patch

**Purpose**: Adds getter and setter APIs for HAS (Hearing Access Service) client to enable GATT handle caching.

**Changes**:
- `include/zephyr/bluetooth/audio/has.h`: Added `bt_has_client_get_handles()` and `bt_has_client_set_handles()` API declarations
- `subsys/bluetooth/audio/has_client.c`: Implemented handle extraction and injection functions

**Benefits**:
- Enables caching of GATT attribute handles in NVS
- Allows skipping service discovery on reconnection
- Reduces reconnection time from ~300-500ms to ~10-50ms for HAS
- Graceful error handling when cached handles become invalid

## Applying Patches

### Automatic (via west)

If using west workspace management, patches can be applied automatically during `west update` by configuring `west.yml`:

```yaml
manifest:
  projects:
    - name: zephyr
      url: https://github.com/zephyrproject-rtos/zephyr
      revision: v4.2.0  # or your desired version
      patches:
        - path: patches/0001-Bluetooth-audio-HAS-Add-handle-getter-setter-APIs-fo.patch
```

### Manual

Apply patches manually using:

```bash
cd path/to/zephyr
git am path/to/HARC_Connect_Test/patches/*.patch
```

Or without committing:

```bash
cd path/to/zephyr
git apply path/to/HARC_Connect_Test/patches/*.patch
```

## Regenerating Patches

If you need to regenerate patches after modifying Zephyr:

```bash
cd path/to/zephyr
git add <modified-files>
git commit -m "Descriptive commit message"
git format-patch HEAD~1 -o path/to/HARC_Connect_Test/patches/
```

## Future Patches

Additional patches will be added for:
- VCP (Volume Control Profile) handle caching
- CSIP (Coordinated Set Identification Profile) handle caching
- BAS (Battery Service) handle caching

## Upstreaming

These patches could potentially be upstreamed to the Zephyr project. The APIs follow Zephyr conventions and provide useful functionality for audio profile applications requiring fast reconnection.

## Notes

- These patches modify internal Zephyr structures and may need updates when upgrading Zephyr versions
- Always test thoroughly after applying patches
- Keep Zephyr version pinned in your project to avoid compatibility issues
