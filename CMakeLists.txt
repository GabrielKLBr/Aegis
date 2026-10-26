cmake_minimum_required(VERSION 3.22...4.0)

project(GuardianAV
    VERSION 1.0.0
    DESCRIPTION "GuardianAV Windows application and native Linux scanner/core tools"
    LANGUAGES CXX)

# Native Linux tools and shared-core checks have no Wine or Windows API link.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    include(cmake/GuardianLinux.cmake)
    return()
endif()
if(NOT WIN32)
    message(FATAL_ERROR "Supported hosts: Windows and Linux x64.")
endif()
if(CMAKE_VERSION VERSION_LESS 4.0)
    message(FATAL_ERROR "The Windows build requires CMake 4.0 or newer.")
endif()

# --- C++ standard ----------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# This project uses headers and translation units, not C++ named modules.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

# Default to a Release build when the generator is single-config.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# --- Sources ---------------------------------------------------------------
add_executable(av
    src/Util.cpp
    src/Sha256.cpp
    src/SignatureDatabase.cpp
    src/Scanner.cpp
    src/Quarantine.cpp
    src/RealTimeMonitor.cpp
    src/WscIntegration.cpp
    src/Sandbox.cpp
    src/microvm/host/Preflight.cpp
    src/main.cpp
)

# Build a Unicode (wmain / wide-API) application.
target_compile_definitions(av PRIVATE UNICODE _UNICODE)

# --- Compiler-specific flags ----------------------------------------------
if(MSVC)
    target_compile_options(av PRIVATE /W4 /permissive- /utf-8)
    # MSVC: wmainCRTStartup is selected automatically when wmain is present.
else()
    # MinGW-w64 / GCC: -municode makes the CRT call wmain().
    target_compile_options(av PRIVATE -Wall -Wextra)
    target_link_options(av PRIVATE -municode -static-libgcc -static-libstdc++)
endif()

# --- Windows libraries -----------------------------------------------------
target_link_libraries(av PRIVATE
    bcrypt        # SHA-256 (CNG)
    wbemuuid      # WMI / Security Center query
    ole32         # COM
    oleaut32      # VARIANT / BSTR
    advapi32      # tokens / security / restricted tokens
    shell32       # SHCreateDirectoryExW / ShellExecute
    user32        # sandbox hidden desktop
)

# --- Harmless behavior sample (used to exercise the sandbox) ---------------
add_executable(sample_behavior src/sample_behavior.cpp)
target_compile_definitions(sample_behavior PRIVATE UNICODE _UNICODE)
if(NOT MSVC)
    target_link_options(sample_behavior PRIVATE -municode)
endif()

# --- MicroVM WHP proof of concept (roadmap step 1) -------------------------
# Standalone tool: creates a WHP partition, maps guest RAM, runs a tiny guest,
# and intercepts its I/O. Links the Windows Hypervisor Platform import library.
add_executable(vmpoc src/microvm/vmm/WhpPoc.cpp)
if(MSVC)
    target_compile_options(vmpoc PRIVATE /W4 /utf-8)
else()
    target_compile_options(vmpoc PRIVATE -Wall -Wextra)
    target_link_options(vmpoc PRIVATE -static-libgcc -static-libstdc++)
endif()
target_link_libraries(vmpoc PRIVATE winhvplatform)

# --- MicroVM device-layer demo (roadmap step 2) ----------------------------
# Engine (MicroVm + IoDevice) plus PvConsole/PvDisk device models, driven by a
# guest that performs a MEMZ-style boot-sector overwrite into a diff image.
add_executable(vmdemo
    src/microvm/vmm/MicroVmMonitor.cpp
    src/microvm/vmm/devices/PvDisk.cpp
    src/microvm/VmDemo.cpp
)
if(MSVC)
    target_compile_options(vmdemo PRIVATE /W4 /utf-8)
else()
    target_compile_options(vmdemo PRIVATE -Wall -Wextra)
    target_link_options(vmdemo PRIVATE -static-libgcc -static-libstdc++)
endif()
target_link_libraries(vmdemo PRIVATE winhvplatform)

# --- MicroVM snapshot / disposable-reset demo (roadmap step 6) --------------
add_executable(vmsnap
    src/microvm/vmm/MicroVmMonitor.cpp
    src/microvm/host/SnapshotManager.cpp
    src/microvm/VmSnap.cpp
)
if(MSVC)
    target_compile_options(vmsnap PRIVATE /W4 /utf-8)
else()
    target_compile_options(vmsnap PRIVATE -Wall -Wextra)
    target_link_options(vmsnap PRIVATE -static-libgcc -static-libstdc++)
endif()
target_link_libraries(vmsnap PRIVATE winhvplatform)

# --- MicroVM NT-personality PoC ("Wine in reverse": 64-bit + syscall trap) --
add_executable(vmsyscall
    src/microvm/vmm/MicroVmMonitor.cpp
    src/microvm/VmSyscall.cpp
)
if(MSVC)
    target_compile_options(vmsyscall PRIVATE /W4 /utf-8)
else()
    target_compile_options(vmsyscall PRIVATE -Wall -Wextra)
    target_link_options(vmsyscall PRIVATE -static-libgcc -static-libstdc++)
endif()
target_link_libraries(vmsyscall PRIVATE winhvplatform)

# --- MicroVM PE loader demo (load + run a real .exe via the NT personality) -
add_executable(vmpe
    src/microvm/vmm/MicroVmMonitor.cpp
    src/microvm/host/PeLoader.cpp
    src/microvm/VmPe.cpp
)
if(MSVC)
    target_compile_options(vmpe PRIVATE /W4 /utf-8)
else()
    target_compile_options(vmpe PRIVATE -Wall -Wextra)
    target_link_options(vmpe PRIVATE -static-libgcc -static-libstdc++)
endif()
target_link_libraries(vmpe PRIVATE winhvplatform)

# --- MicroVM hardening demo (ring-3 + real SYSCALL/#UD + #GP interception) --
add_executable(vmguard
    src/microvm/vmm/MicroVmMonitor.cpp
    src/microvm/VmGuard.cpp
)
if(MSVC)
    target_compile_options(vmguard PRIVATE /W4 /utf-8)
else()
    target_compile_options(vmguard PRIVATE -Wall -Wextra)
    target_link_options(vmguard PRIVATE -static-libgcc -static-libstdc++)
endif()
target_link_libraries(vmguard PRIVATE winhvplatform)

# --- MicroVM virtual FS + behavior report (synthetic NtKernel) --------------
add_executable(vmfs
    src/microvm/vmm/MicroVmMonitor.cpp
    src/microvm/host/PeLoader.cpp
    src/microvm/host/VirtualFs.cpp
    src/microvm/host/VirtualRegistry.cpp
    src/microvm/host/ReportBuilder.cpp
    src/microvm/host/NtKernel.cpp
    src/microvm/VmFs.cpp
)
if(MSVC)
    target_compile_options(vmfs PRIVATE /W4 /utf-8)
else()
    target_compile_options(vmfs PRIVATE -Wall -Wextra)
    target_link_options(vmfs PRIVATE -static-libgcc -static-libstdc++)
endif()
target_link_libraries(vmfs PRIVATE winhvplatform)

# --- MicroVM virtual registry + fake system profile -------------------------
add_executable(vmreg
    src/microvm/vmm/MicroVmMonitor.cpp
    src/microvm/host/PeLoader.cpp
    src/microvm/host/VirtualFs.cpp
    src/microvm/host/VirtualRegistry.cpp
    src/microvm/host/ReportBuilder.cpp
    src/microvm/host/NtKernel.cpp
    src/microvm/VmReg.cpp
)
if(MSVC)
    target_compile_options(vmreg PRIVATE /W4 /utf-8)
else()
    target_compile_options(vmreg PRIVATE -Wall -Wextra)
    target_link_options(vmreg PRIVATE -static-libgcc -static-libstdc++)
endif()
target_link_libraries(vmreg PRIVATE winhvplatform)

# --- MicroVM PEB/TEB foundation (toward real CRT binaries) ------------------
add_executable(vmpeb
    src/microvm/vmm/MicroVmMonitor.cpp
    src/microvm/host/ProcessEnv.cpp
    src/microvm/VmPeb.cpp
)
if(MSVC)
    target_compile_options(vmpeb PRIVATE /W4 /utf-8)
else()
    target_compile_options(vmpeb PRIVATE -Wall -Wextra)
    target_link_options(vmpeb PRIVATE -static-libgcc -static-libstdc++)
endif()
target_link_libraries(vmpeb PRIVATE winhvplatform)

# --- MicroVM Ldr (module list) + heap (more CRT prerequisites) --------------
add_executable(vmheap
    src/microvm/vmm/MicroVmMonitor.cpp
    src/microvm/host/ProcessEnv.cpp
    src/microvm/host/VirtualFs.cpp
    src/microvm/host/VirtualRegistry.cpp
    src/microvm/host/ReportBuilder.cpp
    src/microvm/host/NtKernel.cpp
    src/microvm/VmHeap.cpp
)
if(MSVC)
    target_compile_options(vmheap PRIVATE /W4 /utf-8)
else()
    target_compile_options(vmheap PRIVATE -Wall -Wextra)
    target_link_options(vmheap PRIVATE -static-libgcc -static-libstdc++)
endif()
target_link_libraries(vmheap PRIVATE winhvplatform)

# --- MicroVM CRT startup surface + TLS -------------------------------------
add_executable(vmstartup
    src/microvm/vmm/MicroVmMonitor.cpp
    src/microvm/host/ProcessEnv.cpp
    src/microvm/host/VirtualFs.cpp
    src/microvm/host/VirtualRegistry.cpp
    src/microvm/host/ReportBuilder.cpp
    src/microvm/host/NtKernel.cpp
    src/microvm/VmStartup.cpp
)
if(MSVC)
    target_compile_options(vmstartup PRIVATE /W4 /utf-8)
else()
    target_compile_options(vmstartup PRIVATE -Wall -Wextra)
    target_link_options(vmstartup PRIVATE -static-libgcc -static-libstdc++)
endif()
target_link_libraries(vmstartup PRIVATE winhvplatform)

# --- MicroVM Model B: load the REAL ntdll into a mini NT kernel --------------
add_executable(vmntdll
    src/microvm/vmm/MicroVmMonitor.cpp
    src/microvm/host/PeLoader.cpp
    src/microvm/host/ReportBuilder.cpp
    src/microvm/host/VirtualFs.cpp
    src/microvm/host/MiniNt.cpp
    src/microvm/host/MiniNtObjects.cpp
    src/microvm/host/MiniNtThreads.cpp
    src/microvm/host/MiniNtSynchronization.cpp
    src/microvm/VmRealNtdll.cpp
)
if(MSVC)
    target_compile_options(vmntdll PRIVATE /W4 /utf-8)
else()
    target_compile_options(vmntdll PRIVATE -Wall -Wextra)
    target_link_options(vmntdll PRIVATE -static-libgcc -static-libstdc++)
endif()
target_link_libraries(vmntdll PRIVATE winhvplatform)

# --- MicroVM Model B: real ntdll file subsystem ----------------------------
add_executable(vmntfile
    src/microvm/vmm/MicroVmMonitor.cpp
    src/microvm/host/PeLoader.cpp
    src/microvm/host/ReportBuilder.cpp
    src/microvm/host/VirtualFs.cpp
    src/microvm/host/MiniNt.cpp
    src/microvm/host/MiniNtObjects.cpp
    src/microvm/host/MiniNtThreads.cpp
    src/microvm/host/MiniNtSynchronization.cpp
    src/microvm/VmNtFile.cpp
)
if(MSVC)
    target_compile_options(vmntfile PRIVATE /W4 /utf-8)
else()
    target_compile_options(vmntfile PRIVATE -Wall -Wextra)
    target_link_options(vmntfile PRIVATE -static-libgcc -static-libstdc++)
endif()
target_link_libraries(vmntfile PRIVATE winhvplatform)

# --- MicroVM Model B: NT sections (loader core) ----------------------------
add_executable(vmntsect
    src/microvm/vmm/MicroVmMonitor.cpp
    src/microvm/host/PeLoader.cpp
    src/microvm/host/ReportBuilder.cpp
    src/microvm/host/VirtualFs.cpp
    src/microvm/host/MiniNt.cpp
    src/microvm/host/MiniNtObjects.cpp
    src/microvm/host/MiniNtThreads.cpp
    src/microvm/host/MiniNtSynchronization.cpp
    src/microvm/VmNtSection.cpp
)
if(MSVC)
    target_compile_options(vmntsect PRIVATE /W4 /utf-8)
else()
    target_compile_options(vmntsect PRIVATE -Wall -Wextra)
    target_link_options(vmntsect PRIVATE -static-libgcc -static-libstdc++)
endif()
target_link_libraries(vmntsect PRIVATE winhvplatform)

# --- MicroVM Model B: image sections (SEC_IMAGE + PE layout + relocations) --
add_executable(vmntimage
    src/microvm/vmm/MicroVmMonitor.cpp
    src/microvm/host/PeLoader.cpp
    src/microvm/host/ReportBuilder.cpp
    src/microvm/host/VirtualFs.cpp
    src/microvm/host/MiniNt.cpp
    src/microvm/host/MiniNtObjects.cpp
    src/microvm/host/MiniNtThreads.cpp
    src/microvm/host/MiniNtSynchronization.cpp
    src/microvm/VmNtImage.cpp
)
if(MSVC)
    target_compile_options(vmntimage PRIVATE /W4 /utf-8)
else()
    target_compile_options(vmntimage PRIVATE -Wall -Wextra)
    target_link_options(vmntimage PRIVATE -static-libgcc -static-libstdc++)
endif()
target_link_libraries(vmntimage PRIVATE winhvplatform)
target_compile_definitions(vmntimage PRIVATE GUARDIAN_IMAGE_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/test/imgtest.dll")

# --- MicroVM Model B: loader queries + sync (NtQuery* / events) -------------
add_executable(vmntquery
    src/microvm/vmm/MicroVmMonitor.cpp
    src/microvm/host/PeLoader.cpp
    src/microvm/host/ReportBuilder.cpp
    src/microvm/host/VirtualFs.cpp
    src/microvm/host/MiniNt.cpp
    src/microvm/host/MiniNtObjects.cpp
    src/microvm/host/MiniNtThreads.cpp
    src/microvm/host/MiniNtSynchronization.cpp
    src/microvm/VmNtQuery.cpp
)
if(MSVC)
    target_compile_options(vmntquery PRIVATE /W4 /utf-8)
else()
    target_compile_options(vmntquery PRIVATE -Wall -Wextra)
    target_link_options(vmntquery PRIVATE -static-libgcc -static-libstdc++)
endif()
target_link_libraries(vmntquery PRIVATE winhvplatform)

# --- MicroVM Model B: NT registry (NtOpenKey/NtQueryValueKey/NtSetValueKey) -
add_executable(vmntreg
    src/microvm/vmm/MicroVmMonitor.cpp
    src/microvm/host/PeLoader.cpp
    src/microvm/host/ReportBuilder.cpp
    src/microvm/host/VirtualFs.cpp
    src/microvm/host/MiniNt.cpp
    src/microvm/host/MiniNtObjects.cpp
    src/microvm/host/MiniNtThreads.cpp
    src/microvm/host/MiniNtSynchronization.cpp
    src/microvm/VmNtReg.cpp
)
if(MSVC)
    target_compile_options(vmntreg PRIVATE /W4 /utf-8)
else()
    target_compile_options(vmntreg PRIVATE -Wall -Wextra)
    target_link_options(vmntreg PRIVATE -static-libgcc -static-libstdc++)
endif()
target_link_libraries(vmntreg PRIVATE winhvplatform)

# --- MicroVM Model B: THE BOSS -- real ntdll LdrInitializeThunk bring-up ----
add_executable(vmldr
    src/microvm/vmm/MicroVmMonitor.cpp
    src/microvm/host/PeLoader.cpp
    src/microvm/host/ProcessEnv.cpp
    src/microvm/host/ReportBuilder.cpp
    src/microvm/host/VirtualFs.cpp
    src/microvm/host/MiniNt.cpp
    src/microvm/host/MiniNtObjects.cpp
    src/microvm/host/MiniNtThreads.cpp
    src/microvm/host/MiniNtSynchronization.cpp
    src/microvm/VmLdr.cpp
)
if(MSVC)
    target_compile_options(vmldr PRIVATE /W4 /utf-8)
else()
    target_compile_options(vmldr PRIVATE -Wall -Wextra)
    target_link_options(vmldr PRIVATE -static-libgcc -static-libstdc++)
endif()
target_link_libraries(vmldr PRIVATE winhvplatform)

# --- MicroVM Wine path: OUR loader (map + link real DLLs ourselves) ---------
add_executable(vmwinload
    src/microvm/vmm/MicroVmMonitor.cpp
    src/microvm/host/WinLoader.cpp
    src/microvm/VmWinLoad.cpp
)
if(MSVC)
    target_compile_options(vmwinload PRIVATE /W4 /utf-8)
else()
    target_compile_options(vmwinload PRIVATE -Wall -Wextra)
    target_link_options(vmwinload PRIVATE -static-libgcc -static-libstdc++)
endif()
target_link_libraries(vmwinload PRIVATE winhvplatform)

# --- MicroVM Wine path: OUR loader on the REAL system DLL graph -------------
add_executable(vmwinsys
    src/microvm/vmm/MicroVmMonitor.cpp
    src/microvm/host/WinLoader.cpp
    src/microvm/VmWinSys.cpp
)
if(MSVC)
    target_compile_options(vmwinsys PRIVATE /W4 /utf-8)
else()
    target_compile_options(vmwinsys PRIVATE -Wall -Wextra)
    target_link_options(vmwinsys PRIVATE -static-libgcc -static-libstdc++)
endif()
target_link_libraries(vmwinsys PRIVATE winhvplatform)

# --- MicroVM Wine path: CALL a real kernel32 export via OUR loader ----------
add_executable(vmwincall
    src/microvm/vmm/MicroVmMonitor.cpp
    src/microvm/host/WinLoader.cpp
    src/microvm/VmWinCall.cpp
)
if(MSVC)
    target_compile_options(vmwincall PRIVATE /W4 /utf-8)
else()
    target_compile_options(vmwincall PRIVATE -Wall -Wextra)
    target_link_options(vmwincall PRIVATE -static-libgcc -static-libstdc++)
endif()
target_link_libraries(vmwincall PRIVATE winhvplatform)

# --- MicroVM Wine path: auto-call EVERY kernel32 export, classify each ------
add_executable(vmwinall
    src/microvm/vmm/MicroVmMonitor.cpp
    src/microvm/host/WinLoader.cpp
    src/microvm/host/ProcessEnv.cpp
    src/microvm/host/ReportBuilder.cpp
    src/microvm/host/VirtualFs.cpp
    src/microvm/host/MiniNt.cpp
    src/microvm/host/MiniNtObjects.cpp
    src/microvm/host/MiniNtThreads.cpp
    src/microvm/host/MiniNtSynchronization.cpp
    src/microvm/VmWinAll.cpp
)
if(MSVC)
    target_compile_options(vmwinall PRIVATE /W4 /utf-8)
else()
    target_compile_options(vmwinall PRIVATE -Wall -Wextra)
    target_link_options(vmwinall PRIVATE -static-libgcc -static-libstdc++)
endif()
target_link_libraries(vmwinall PRIVATE winhvplatform)

# --- MicroVM Wine path: bring up the process heap via real heap code --------
add_executable(vmwininit
    src/microvm/vmm/MicroVmMonitor.cpp
    src/microvm/host/WinLoader.cpp
    src/microvm/host/ProcessEnv.cpp
    src/microvm/host/ReportBuilder.cpp
    src/microvm/host/VirtualFs.cpp
    src/microvm/host/MiniNt.cpp
    src/microvm/host/MiniNtObjects.cpp
    src/microvm/host/MiniNtThreads.cpp
    src/microvm/host/MiniNtSynchronization.cpp
    src/microvm/VmWinInit.cpp
)
if(MSVC)
    target_compile_options(vmwininit PRIVATE /W4 /utf-8)
else()
    target_compile_options(vmwininit PRIVATE -Wall -Wextra)
    target_link_options(vmwininit PRIVATE -static-libgcc -static-libstdc++)
endif()
target_link_libraries(vmwininit PRIVATE winhvplatform)

# --- MicroVM anti-VM: spoof CPUID to a bare-metal Intel desktop -------------
add_executable(vmspoof
    src/microvm/vmm/MicroVmMonitor.cpp
    src/microvm/VmSpoof.cpp
)
if(MSVC)
    target_compile_options(vmspoof PRIVATE /W4 /utf-8)
else()
    target_compile_options(vmspoof PRIVATE -Wall -Wextra)
    target_link_options(vmspoof PRIVATE -static-libgcc -static-libstdc++)
endif()
target_link_libraries(vmspoof PRIVATE winhvplatform)

# --- MicroVM anti-VM: read the registry personality (the malware's view) -----
add_executable(vmspoofreg
    src/microvm/vmm/MicroVmMonitor.cpp
    src/microvm/host/PeLoader.cpp
    src/microvm/host/VirtualFs.cpp
    src/microvm/host/ReportBuilder.cpp
    src/microvm/host/MiniNt.cpp
    src/microvm/host/MiniNtObjects.cpp
    src/microvm/host/MiniNtThreads.cpp
    src/microvm/host/MiniNtSynchronization.cpp
    src/microvm/VmSpoofReg.cpp
)
if(MSVC)
    target_compile_options(vmspoofreg PRIVATE /W4 /utf-8)
else()
    target_compile_options(vmspoofreg PRIVATE -Wall -Wextra)
    target_link_options(vmspoofreg PRIVATE -static-libgcc -static-libstdc++)
endif()
target_link_libraries(vmspoofreg PRIVATE winhvplatform)

# --- MicroVM anti-VM: uptime + process list (timing/execution artifacts) ------
add_executable(vmspoofsys
    src/microvm/vmm/MicroVmMonitor.cpp
    src/microvm/host/PeLoader.cpp
    src/microvm/host/VirtualFs.cpp
    src/microvm/host/ReportBuilder.cpp
    src/microvm/host/MiniNt.cpp
    src/microvm/host/MiniNtObjects.cpp
    src/microvm/host/MiniNtThreads.cpp
    src/microvm/host/MiniNtSynchronization.cpp
    src/microvm/VmSpoofSys.cpp
)
if(MSVC)
    target_compile_options(vmspoofsys PRIVATE /W4 /utf-8)
else()
    target_compile_options(vmspoofsys PRIVATE -Wall -Wextra)
    target_link_options(vmspoofsys PRIVATE -static-libgcc -static-libstdc++)
endif()
target_link_libraries(vmspoofsys PRIVATE winhvplatform)

# --- MicroVM anti-VM: NIC MAC via the NDIS OID query path --------------------
add_executable(vmspoofnet
    src/microvm/vmm/MicroVmMonitor.cpp
    src/microvm/host/PeLoader.cpp
    src/microvm/host/VirtualFs.cpp
    src/microvm/host/ReportBuilder.cpp
    src/microvm/host/MiniNt.cpp
    src/microvm/host/MiniNtObjects.cpp
    src/microvm/host/MiniNtThreads.cpp
    src/microvm/host/MiniNtSynchronization.cpp
    src/microvm/VmSpoofNet.cpp
)
if(MSVC)
    target_compile_options(vmspoofnet PRIVATE /W4 /utf-8)
else()
    target_compile_options(vmspoofnet PRIVATE -Wall -Wextra)
    target_link_options(vmspoofnet PRIVATE -static-libgcc -static-libstdc++)
endif()
target_link_libraries(vmspoofnet PRIVATE winhvplatform)

# --- microsandbox.exe: consolidated NT-personality detonation tool ----------
add_executable(microsandbox
    src/microvm/vmm/MicroVmMonitor.cpp
    src/microvm/host/PeLoader.cpp
    src/microvm/host/VirtualFs.cpp
    src/microvm/host/VirtualRegistry.cpp
    src/microvm/host/ProcessEnv.cpp
    src/microvm/host/ReportBuilder.cpp
    src/microvm/host/NtKernel.cpp
    src/microvm/MicroSandbox.cpp
    src/microvm/MicroSandboxMain.cpp
)
target_compile_definitions(microsandbox PRIVATE UNICODE _UNICODE)
if(MSVC)
    target_compile_options(microsandbox PRIVATE /W4 /utf-8)
else()
    target_compile_options(microsandbox PRIVATE -Wall -Wextra)
    target_link_options(microsandbox PRIVATE -municode -static-libgcc -static-libstdc++)
endif()
target_link_libraries(microsandbox PRIVATE winhvplatform user32)

# --- GUI executable (Direct2D / DirectWrite, hardware-accelerated) ---------
add_executable(avui WIN32
    src/Util.cpp
    src/Sha256.cpp
    src/SignatureDatabase.cpp
    src/EngineFacade.cpp
    src/Sandbox.cpp
    src/UiApp.cpp
)
target_compile_definitions(avui PRIVATE UNICODE _UNICODE)
if(MSVC)
    target_compile_options(avui PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(avui PRIVATE -Wall -Wextra)
    target_link_options(avui PRIVATE -municode -mwindows -static-libgcc -static-libstdc++)
endif()
target_link_libraries(avui PRIVATE
    d2d1 dwrite        # GPU-accelerated 2D vector + text
    ole32 oleaut32     # COM + file dialog
    uuid               # CLSID/IID/FOLDERID GUIDs
    shell32 advapi32 bcrypt user32
)

# --- Copy the signature DB next to each executable after build -------------
add_custom_command(TARGET av POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/signatures.txt"
            "$<TARGET_FILE_DIR:av>/signatures.txt"
    COMMENT "Copying signatures.txt next to av")
add_custom_command(TARGET avui POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/signatures.txt"
            "$<TARGET_FILE_DIR:avui>/signatures.txt"
    COMMENT "Copying signatures.txt next to avui")

# --- Convenience install ---------------------------------------------------
install(TARGETS av RUNTIME DESTINATION bin)
install(FILES signatures.txt DESTINATION bin)

message(STATUS "GuardianAV configured. Build with:  cmake --build <build-dir>")

# Kernel memory regression tests. The guest test executes only the tiny programs
# generated in its source, through the existing WHP/MiniNt syscall path.
include(CTest)
if(BUILD_TESTING)
    add_executable(microvm_memory_tests test/MicroVmMemoryTests.cpp)
    add_executable(microvm_virtual_memory_tests test/MicroVmVirtualMemoryTests.cpp)
    add_executable(microvm_objects_tests test/MicroVmObjectsTests.cpp)
    add_executable(microvm_scheduler_tests test/MicroVmSchedulerTests.cpp)
    add_executable(microvm_synchronization_tests test/MicroVmSynchronizationTests.cpp)
    add_executable(microvm_heap_guest_tests
        src/microvm/vmm/MicroVmMonitor.cpp
        src/microvm/host/ReportBuilder.cpp
        src/microvm/host/VirtualFs.cpp
        src/microvm/host/MiniNt.cpp
    src/microvm/host/MiniNtObjects.cpp
    src/microvm/host/MiniNtThreads.cpp
    src/microvm/host/MiniNtSynchronization.cpp
        test/MicroVmHeapGuest.cpp)
    target_link_libraries(microvm_heap_guest_tests PRIVATE winhvplatform)
    add_executable(microvm_virtual_memory_guest_tests
        src/microvm/vmm/MicroVmMonitor.cpp
        src/microvm/host/ReportBuilder.cpp
        src/microvm/host/VirtualFs.cpp
        src/microvm/host/MiniNt.cpp
    src/microvm/host/MiniNtObjects.cpp
    src/microvm/host/MiniNtThreads.cpp
    src/microvm/host/MiniNtSynchronization.cpp
        test/MicroVmVirtualMemoryGuest.cpp)
    target_link_libraries(microvm_virtual_memory_guest_tests PRIVATE winhvplatform)
    add_executable(microvm_objects_guest_tests
        src/microvm/vmm/MicroVmMonitor.cpp
        src/microvm/host/ReportBuilder.cpp
        src/microvm/host/VirtualFs.cpp
        src/microvm/host/MiniNt.cpp
        src/microvm/host/MiniNtObjects.cpp
    src/microvm/host/MiniNtThreads.cpp
    src/microvm/host/MiniNtSynchronization.cpp
        test/MicroVmObjectsGuest.cpp)
    target_link_libraries(microvm_objects_guest_tests PRIVATE winhvplatform)
    add_executable(microvm_threads_guest_tests
        src/microvm/vmm/MicroVmMonitor.cpp
        src/microvm/host/ReportBuilder.cpp
        src/microvm/host/VirtualFs.cpp
        src/microvm/host/MiniNt.cpp
        src/microvm/host/MiniNtObjects.cpp
        src/microvm/host/MiniNtThreads.cpp
        src/microvm/host/MiniNtSynchronization.cpp
        test/MicroVmThreadsGuest.cpp)
    target_link_libraries(microvm_threads_guest_tests PRIVATE winhvplatform)
    add_executable(microvm_synchronization_guest_tests
        src/microvm/vmm/MicroVmMonitor.cpp
        src/microvm/host/ReportBuilder.cpp
        src/microvm/host/VirtualFs.cpp
        src/microvm/host/MiniNt.cpp
        src/microvm/host/MiniNtObjects.cpp
        src/microvm/host/MiniNtThreads.cpp
        src/microvm/host/MiniNtSynchronization.cpp
        test/MicroVmSynchronizationGuest.cpp)
    target_link_libraries(microvm_synchronization_guest_tests PRIVATE winhvplatform)
    foreach(memory_test IN ITEMS microvm_memory_tests microvm_heap_guest_tests microvm_virtual_memory_tests microvm_virtual_memory_guest_tests microvm_objects_tests microvm_objects_guest_tests microvm_scheduler_tests microvm_threads_guest_tests microvm_synchronization_tests microvm_synchronization_guest_tests)
        if(MSVC)
            target_compile_options(${memory_test} PRIVATE /W4 /utf-8)
        else()
            target_compile_options(${memory_test} PRIVATE -Wall -Wextra)
            target_link_options(${memory_test} PRIVATE -static-libgcc -static-libstdc++)
        endif()
    endforeach()
    add_test(NAME microvm_memory COMMAND microvm_memory_tests)
    add_test(NAME microvm_heap_guest COMMAND microvm_heap_guest_tests)
    add_test(NAME microvm_virtual_memory COMMAND microvm_virtual_memory_tests)
    add_test(NAME microvm_virtual_memory_guest COMMAND microvm_virtual_memory_guest_tests)
    add_test(NAME microvm_objects COMMAND microvm_objects_tests)
    add_test(NAME microvm_objects_guest COMMAND microvm_objects_guest_tests)
    add_test(NAME microvm_scheduler COMMAND microvm_scheduler_tests)
    add_test(NAME microvm_threads_guest COMMAND microvm_threads_guest_tests)
    add_test(NAME microvm_synchronization COMMAND microvm_synchronization_tests)
    add_test(NAME microvm_synchronization_guest COMMAND microvm_synchronization_guest_tests)
    set_tests_properties(microvm_memory PROPERTIES TIMEOUT 30)
    set_tests_properties(microvm_heap_guest PROPERTIES TIMEOUT 20 RUN_SERIAL TRUE SKIP_RETURN_CODE 77)
    set_tests_properties(microvm_virtual_memory PROPERTIES TIMEOUT 30)
    set_tests_properties(microvm_virtual_memory_guest PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE SKIP_RETURN_CODE 77)
    set_tests_properties(microvm_objects PROPERTIES TIMEOUT 30)
    set_tests_properties(microvm_objects_guest PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE SKIP_RETURN_CODE 77)
    set_tests_properties(microvm_scheduler PROPERTIES TIMEOUT 30)
    set_tests_properties(microvm_synchronization PROPERTIES TIMEOUT 30)
    set_tests_properties(microvm_synchronization_guest PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE SKIP_RETURN_CODE 77)
    set_tests_properties(microvm_threads_guest PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE SKIP_RETURN_CODE 77)
endif()

# Phase 0: explicit host/VMM/reference groups and the freestanding ABI boundary.
include(cmake/GuardianPlatform.cmake)
option(GUARDIAN_GUI_COMPATIBILITY_ONLY "Build GUI compatibility/core tools without scanner dependencies or scanner frontends" OFF)
if(GUARDIAN_GUI_COMPATIBILITY_ONLY)
    set_target_properties(av avui PROPERTIES EXCLUDE_FROM_ALL TRUE)
else()
    include(cmake/GuardianScanner.cmake)
endif()
include(cmake/GuardianPeGui.cmake)
include(cmake/GuardianCompatibility.cmake)
include(cmake/GuardianExternalPe.cmake)
include(cmake/GuardianNativeNt.cmake)
include(cmake/GuardianWin32.cmake)
include(cmake/GuardianCrt.cmake)
include(cmake/GuardianSecurity.cmake)
include(cmake/GuardianDesktop.cmake)
include(cmake/GuardianComponents.cmake)
include(cmake/GuardianInternet.cmake)
