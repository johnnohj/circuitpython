// Minimal FatFS configuration for WASI port
// We don't use FatFS, but supervisor/filesystem.h includes vfs_fat.h
// which includes ff.h which requires this file.
#pragma once

#define FFCONF_DEF 80286  // Must match ff.h's expected version

#define FF_FS_READONLY   1
#define FF_FS_MINIMIZE   3
#define FF_USE_STRFUNC   0
#define FF_USE_FIND      0
#define FF_USE_MKFS      0
#define FF_USE_FASTSEEK  0
#define FF_USE_EXPAND    0
#define FF_USE_CHMOD     0
#define FF_USE_LABEL     0
#define FF_USE_FORWARD   0
#define FF_CODE_PAGE     437
#define FF_USE_LFN       0
#define FF_LFN_UNICODE   0
#define FF_STRF_ENCODE   0
#define FF_FS_RPATH      0
#define FF_VOLUMES       1
#define FF_MULTI_PARTITION 0
#define FF_MIN_SS        512
#define FF_MAX_SS        512
#define FF_FS_TINY       1
#define FF_FS_EXFAT      0
#define FF_FS_NORTC      1
#define FF_FS_LOCK       0
#define FF_FS_REENTRANT  0
#define FF_WINDOW_ALIGNMENT 1
