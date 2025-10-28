# WASM Port Build Flag Analysis

## Current Flag Hierarchy Issues

### File Structure
```
mpconfigport.h                  # Port-wide C defines (MICROPY_* and CIRCUITPY_*)
mpconfigport.mk                 # Port-wide Makefile flags (CIRCUITPY_*)
variants/mpconfigvariant_common.h  # Common variant C defines (MICROPY_*)
variants/*/mpconfigvariant.h    # Variant-specific C defines
variants/*/mpconfigvariant.mk   # Variant-specific Makefile flags
```

### Current Redundancies

1. **MICROPY flags in variant_common.h vs mpconfigport.h**
   - Many MICROPY_* flags are set in both places
   - mpconfigvariant_common.h is included by mpconfigvariant.h, which is included by mpconfigport.h
   - Creates potential for conflicts and confusion

2. **CIRCUITPY flags split across .mk files**
   - Module enables (CIRCUITPY_ANALOGIO, etc.) in mpconfigport.mk
   - But some could be variant-specific

3. **Filesystem flags**
   - Currently set conditionally in Makefile based on USE_SUPERVISOR_WASM
   - Could be in mpconfigvariant.mk instead

## Proposed Clean Structure

### Principle: Clearer Separation of Concerns

**mpconfigport.h** - Port-wide hardware/platform defines only
- Platform capabilities (WASM-specific)
- Hardware interfaces available
- NOT variant-specific settings

**mpconfigport.mk** - Port-wide CircuitPython modules
- CIRCUITPY_* flags for modules available to ALL variants
- Common build settings

**variants/mpconfigvariant.mk** - Variant-specific build flags
- USE_SUPERVISOR_WASM (integrated only)
- Variant-specific CIRCUITPY_* overrides
- Filesystem type selection

**variants/mpconfigvariant_common.h** - ELIMINATE or MINIMIZE
- Move MICROPY_* flags to mpconfigport.h unless variant-specific
- Keep only if truly shared between variants but not port-wide

**variants/*/mpconfigvariant.h** - Variant-specific feature flags
- Feature toggles (CIRCUITPY_STATUS_BAR, etc.)
- Variant-specific MICROPY_* overrides

## Specific Recommendations

### 1. Move from mpconfigvariant_common.h → mpconfigport.h:
- MICROPY_FLOAT_IMPL (port-wide: always double in WASM)
- MICROPY_READER_VFS (port-wide: always enabled)
- MICROPY_PY_SYS_* flags (port-wide platform features)
- MICROPY_REPL_* flags (REPL features for all variants)

### 2. Keep in variant files:
- CIRCUITPY_STATUS_BAR (varies by variant)
- USE_SUPERVISOR_WASM (integrated only)
- Future: memory limits, optional features

### 3. Filesystem Configuration Flow:
```
variants/integrated/mpconfigvariant.mk:
  USE_SUPERVISOR_WASM = 1

Makefile:
  ifeq ($(USE_SUPERVISOR_WASM),1)
    INTERNAL_FLASH_FILESYSTEM = 0
    ...
    include ../../supervisor/supervisor.mk
    # Override filesystem.c with filesystem_wasm.c
  else
    # Minimal supervisor
  endif
```

## Benefits

1. **Clearer ownership** - Each file has a clear purpose
2. **Less duplication** - Settings appear in one logical place
3. **Easier variant creation** - Variants only override what they need
4. **Better maintainability** - Easier to track which flags affect what
5. **Follows CircuitPython patterns** - Similar to other ports

## Implementation Order

1. ✅ Eliminate supervisor_wasm.mk (use conditional logic in Makefile)
2. Review and consolidate mpconfigvariant_common.h → mpconfigport.h
3. Move filesystem flags to variants/*/mpconfigvariant.mk
4. Document remaining flag purposes in comments
