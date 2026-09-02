# Virunga

> Virunga is a Windows x64 research tool built to validate DetectorOne resilience against callback-blinding techniques.

---

## Overview

Virunga accompanies the DetectorOne demonstration lab. Its purpose is to simulate a BYOVD-style callback tampering scenario and observe how DetectorOne and CanaryMesh react when kernel notification callbacks are enumerated or removed.

In a real BYOVD attack, the attacker often does not unload the EDR driver. Instead, the attacker targets the kernel notification arrays and callback lists used by the EDR, removing or disabling the callback pointers so the sensor stays loaded but becomes blind.

Virunga exists to reproduce that condition in a controlled lab.

---

# PoC

Virunga is a proof-of-concept console application for Windows 11 x64 research environments.

The tool demonstrates:

- physical memory mapping through a vulnerable driver interface;
- kernel virtual-to-physical address translation;
- runtime discovery of `ntoskrnl.exe` base address;
- discovery of process, thread, and image-load notification arrays;
- enumeration of `ObRegisterCallbacks` registrations;
- enumeration of registry callbacks registered through Configuration Manager;
- optional callback neutralization for controlled DetectorOne resilience testing;
- validation of CanaryMesh callback-loss consensus.

The objective is not to build a general offensive toolkit. The objective is to provide a repeatable pressure test for DetectorOne's callback integrity monitoring.

---

# Current Features

- Windows x64 console application.
- Visual Studio 2022 project using platform toolset `v143`.
- Audit mode for callback enumeration.
- Striker mode for callback neutralization in a lab.
- Kernel module base discovery through `EnumDeviceDrivers`.
- Local `ntoskrnl.exe` loading for export and offset resolution.
- Manual page-table walk using recovered kernel CR3.
- Notification callback array discovery for:
  - `PsSetCreateProcessNotifyRoutine`
  - `PsSetCreateThreadNotifyRoutine`
  - `PsSetLoadImageNotifyRoutine`
- Object callback analysis for:
  - `PsProcessType`
  - `PsThreadType`
- Registry callback list analysis through `CmUnRegisterCallback` signature scanning.
- Experimental minifilter discovery helpers for `FLTMGR.sys`.

---

# Project Layout

```text
Virunga
|
|-- Virunga.sln
|-- README.md
|-- Virunga/
|   |-- Virunga.cpp
|   |-- Header.h
|   |-- Virunga.vcxproj
|   `-- Virunga.vcxproj.filters
`-- x64/
    |-- Debug/
    `-- Release/
```

---

# Execution Model

Virunga runs from user mode but requires a kernel primitive exposed by the `WinIo` device interface.

```text
Virunga.exe
    |
    v
\\.\XXX
    |
    v
Physical memory mapping
    |
    v
Kernel address translation
    |
    +--> callback audit
    |
    `--> controlled callback neutralization
```

The current code opens:

```text
\\.\XXX
```

and uses these IOCTL values:

```text
IOCTL_MAP_PHYSICAL_ADDR    = 0x80102040
IOCTL_WINIO_UNMAPPHYSADDR  = 0x80102044
```

Virunga maps physical memory, identifies kernel paging context, translates selected kernel virtual addresses, and inspects callback-related kernel structures.

---

# Modes

## Audit Mode

Audit mode is scan-only. It is used before any destructive test to establish a baseline of registered callbacks.

It enumerates:

- process notify callbacks;
- thread notify callbacks;
- image-load notify callbacks;
- process object callbacks;
- thread object callbacks;
- registry callbacks.

Expected menu:

```text
VIRUNGA MENU
1 : Audit Mode (Scan only)
2 : Striker Mode (Callback Removal)
Choice :
```

Audit mode is the recommended first step when validating DetectorOne because it confirms that DetectorOne callback addresses are visible before the attack simulation.

## Striker Mode

Striker mode is the controlled tamper mode used for the DetectorOne demonstration.

It attempts to affect callback registrations related to selected targets and then allows the defensive stack to react:

```text
Virunga Striker Mode
        |
        v
Callback pointer/list manipulation
        |
        v
DetectorOne callback visibility changes
        |
        v
CanaryMesh Signal A
        |
        v
COLLEGIAL ALERT
```

This mode should only be used in an isolated VM. It can destabilize the operating system and can break security product callbacks by design.

---

# DetectorOne Demonstration Flow

Recommended lab sequence:

1. Boot a Windows x64 VM configured for kernel research.
2. Load `DetectorOne.sys`.
3. Confirm DetectorOne registered process, thread, image-load, and registry callbacks.
4. Confirm CanaryMesh canaries are loaded and registered.
5. Start `DetectorOneEngine.exe` as Administrator.
6. Run `Virunga.exe` as Administrator.
7. Select `Audit Mode` and confirm callback visibility.
8. Select `Striker Mode` in the isolated lab.
9. Observe CanaryMesh detecting callback pointer removal.
10. Confirm repeated collegial alerts while the suspicious driver remains loaded.

Expected CanaryMesh reaction:

```text
[CanaryMesh] SIGNAL A: CALLBACK POINTER REMOVED
[CanaryMesh CONSENSUS] *** COLLEGIAL ALERT ***
[CanaryMesh CONSENSUS] Signal A        = 1 (callback pointer)
```

The intended success condition is not that Virunga "wins". The intended success condition is that DetectorOne's canary layer detects the sensor-blinding attempt and continues producing consensus alerts.

---

# Build Requirements

- Windows 10/11 x64 test machine.
- Visual Studio 2022.
- MSVC platform toolset `v143`.
- Windows SDK.
- Administrator privileges at runtime.
- A lab-only vulnerable driver exposing the `\\.\WinIo` device interface.
- Kernel debugging strongly recommended.
- Snapshot or rollback capability strongly recommended.

---

# Build

From a Developer Command Prompt:

```powershell
msbuild "C:\Users\xxx\source\repos\Virunga\Virunga.sln" /p:Configuration=Release /p:Platform=x64
```

Expected output:

```text
C:\Users\xxx\source\repos\Virunga\x64\Release\Virunga.exe
```

Debug build:

```powershell
msbuild "C:\Users\xxx\source\repos\Virunga\Virunga.sln" /p:Configuration=Debug /p:Platform=x64
```

---

# Runtime Requirements

Virunga must be launched from an elevated console.

```powershell
C:\Users\xxx\source\repos\Virunga\x64\Release\Virunga.exe
```

If `\\.\WinIo` is not available, Virunga cannot map physical memory and will fail early:

```text
[!] Loading driver failed with error : <GetLastError>
[!] Failed to map physical memory
```

This usually means the required lab driver is not loaded, the process is not elevated, or the device name does not match the current driver installation.

---

# Internal Architecture

```text
main
 |
 +--> ShowCleanBanner
 |
 +--> Load local ntoskrnl.exe
 |
 +--> Open \\.\WinIo
 |
 +--> Map physical memory
 |
 +--> Locate kernel CR3 and ntoskrnl base
 |
 +--> Resolve callback structures
 |
 +--> Audit Mode
 |       +--> DisplayNotifyCallbacksDrivers
 |       +--> AuditObRegisterCallbacks
 |       `--> ClearCmRegisterCallback(..., FALSE)
 |
 `--> Striker Mode
         +--> StrikerByBlacklist
         +--> RemoveObRegisterCallbacks
         `--> ClearCmRegisterCallback(..., TRUE)
```

Important implementation areas:

| Area | Functionality |
| --- | --- |
| `ReadMemoryU64` / `WriteMemoryU64` | Helpers for reading and writing mapped physical memory. |
| `VirtualToPhysical` | Manual x64 page-table walk. |
| `GetModuleBase` | Kernel module base discovery through PSAPI. |
| `GetDriverName` | Maps callback function addresses back to driver names. |
| `FindNotifyRoutineArray` | Locates notification arrays behind public `PsSet*NotifyRoutine` exports. |
| `AuditObRegisterCallbacks` | Enumerates process/thread object callbacks. |
| `ClearCmRegisterCallback` | Audits or resets registry callback list state depending on mode. |
| `FindFltGlobals` / `DisplayMinifilters` | Experimental minifilter inspection helpers. |

---

# Relationship With DetectorOne

Virunga is the adversarial validation tool for the DetectorOne stack.

| Virunga action | DetectorOne/CanaryMesh expected reaction |
| --- | --- |
| Enumerate process/image/thread callbacks | DetectorOne remains loaded; callbacks are visible. |
| Remove or blind callback entries | CanaryMesh detects Signal A. |
| Registry callback list manipulation | DetectorOne registry telemetry and callback integrity should show disruption. |
| Keep target driver loaded | CanaryMesh should repeat collegial alerts every 60 seconds. |
| DetectorOne callback missing from arrays | Consensus alert should be produced when quorum is reached. |

This is the core demonstration: Virunga simulates the callback attack, while DetectorOne and CanaryMesh prove they can detect it.

---

# Research Vision

Virunga is designed to support research into:

- BYOVD attack simulation;
- kernel callback integrity monitoring;
- EDR self-protection validation;
- callback array enumeration;
- callback loss detection;
- canary-based sensor resilience;
- blue-team validation of kernel telemetry pipelines.

The long-term value is the feedback loop:

```text
Attack simulation -> DetectorOne telemetry -> Canary consensus -> Engine output -> detection improvement
```

---

# Roadmap

## Completed

- Physical memory mapping path.
- Kernel base discovery.
- Kernel virtual-to-physical translation.
- Process notify callback enumeration.
- Thread notify callback enumeration.
- Image-load notify callback enumeration.
- Object callback enumeration.
- Registry callback audit path.
- Interactive audit/striker menu.
- DetectorOne demonstration compatibility.

## Planned

- Safer target selection through a populated callback target list.
- Dry-run summary before every destructive action.
- Version-aware offsets for newer Windows builds.
- JSON report export for DetectorOneEngine ingestion.
- Better separation between audit logic and tamper logic.
- Optional symbol-assisted structure discovery.
- Cleaner console logging and status codes.

---

# Warning

Virunga intentionally interacts with sensitive Windows kernel memory through a vulnerable-driver-style primitive.

Incorrect offsets, unsupported Windows builds, stale structure assumptions, or unexpected callback layouts can crash the machine, corrupt kernel memory, or disable security callbacks.

Use only inside a dedicated research VM. Do not run this on a production endpoint.

---

# License

Educational and defensive research purposes only.
