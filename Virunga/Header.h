#pragma once
#pragma once
#define _CRT_SECURE_NO_WARNINGS
#include <iostream>
#include <Windows.h>
#include <psapi.h>
#include <winternl.h>
#include <ntstatus.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <tchar.h>

#define IOCTL_MAP_PHYSICAL_ADDR    0x80102040
#define IOCTL_WINIO_UNMAPPHYSADDR  0x80102044

#define OFF_FRAME_FILTERS 0xB0    // 0x48 (RegisteredFilters) + 0x68 (rList)
#define OFF_FRAME_VOLUMES 0x130   // 0xC8 (AttachedVolumes) + 0x68 (rList)
#define OFF_FILTER_NAME    0x40    // Name dans _FLT_FILTER
#define OFF_FILTER_LINKS   0x10    // 

//#define ACTIVE_PROCESS_LINKS_OFFSET 0x1d8 - Win11 25h2
//#define IMAGE_FILE_NAME_OFFSET 0x338 - Win11 25h2
//#define UNIQUE_PROCESS_ID_OFFSET 0x1d0 - Win11 25h2
//#define TOKEN_OFFSET 0x248 - Win11 25h2
//#define PsInitialSystemProcessOffset 0xFC4AA8 - Win11 25h2
BYTE* g_PhysBase;
UINT64 g_PhysSize;
UINT64 g_Cr3;

typedef struct PHYSICAL_MEMORY_READ {

    uint64_t  size;
    uint64_t  addr;
    uint64_t  unk1;
    uint64_t  outPtr;
    uint64_t  unk2;

}PHYSICAL_MEMORY_READ, * PPHYSICAL_MEMORY_READ;

typedef struct _EDR_CALLBACK_NODE {
    UINT64 NodeAddress;      // Adresse du _CALLBACK_NODE
    UINT64 OriginalFlink;
    UINT64 OriginalBlink;
    CHAR DriverName[32];
    BOOL IsRemoved;
} EDR_CALLBACK_NODE;

EDR_CALLBACK_NODE FoundNodes[256];
int NodeCount = 0;

const char* blacklist[] = {
    ""
};