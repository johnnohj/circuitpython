#include <time.h>
#include "lib/oofatfs/ff.h"

DWORD get_fattime(void) {
    // WebAssembly stub - return a fixed timestamp
    // This represents Jan 1, 2020 00:00:00
    return ((2020 - 1980) << 25) | (1 << 21) | (1 << 16);
}

// WebAssembly stubs for missing FAT filesystem functions
FRESULT f_chdir(FATFS *fs, const TCHAR* path) {
    return FR_NOT_ENABLED;
}

FRESULT f_getcwd(FATFS *fs, TCHAR* buff, UINT len) {
    if (len > 0) {
        buff[0] = '\0';
    }
    return FR_NOT_ENABLED;
}