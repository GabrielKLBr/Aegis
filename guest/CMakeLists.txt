# Contracts remain independently auditable; phase 1 adds the own guest image.
if(NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "The guest contract target currently requires MSVC x64.")
endif()

# Old caches can retain /RTC1 from before CMP0184. Remove it only in this
# directory; host targets keep their runtime checks and stack protection.
foreach(guest_config IN ITEMS DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    string(REGEX REPLACE "[/\-]RTC[^ ]*" "" CMAKE_CXX_FLAGS_${guest_config} "${CMAKE_CXX_FLAGS_${guest_config}}")
endforeach()
string(REGEX REPLACE "[/\-]EH[a-zA-Z\-]+" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
add_library(guardian_guest_contract OBJECT ContractCheck.cpp)
target_link_libraries(guardian_guest_contract PRIVATE guardian_abi)
set_target_properties(guardian_guest_contract PROPERTIES
    CXX_STANDARD 20 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO
    MSVC_RUNTIME_CHECKS "" MSVC_RUNTIME_LIBRARY ""
    VS_JUST_MY_CODE_DEBUGGING FALSE INTERPROCEDURAL_OPTIMIZATION FALSE)
target_compile_options(guardian_guest_contract PRIVATE /W4 /WX /utf-8 /permissive-
    /kernel /EHs-c- /GR- /Zl /GS- /JMC-)

add_custom_target(guardian_guest_audit
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/check_guest_object.py"
            --object $<TARGET_OBJECTS:guardian_guest_contract>
    DEPENDS guardian_guest_contract COMMAND_EXPAND_LISTS VERBATIM)
add_custom_target(guardian_guest DEPENDS guardian_guest_audit)

set(CMAKE_CXX_STANDARD_LIBRARIES "")
add_executable(guardian_kernel Kernel.cpp Memory.cpp)
target_link_libraries(guardian_kernel PRIVATE guardian_abi)
set_target_properties(guardian_kernel PROPERTIES
    OUTPUT_NAME guardian-kernel SUFFIX .gk
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/guest/$<CONFIG>"
    CXX_STANDARD 20 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO
    MSVC_RUNTIME_CHECKS "" MSVC_RUNTIME_LIBRARY ""
    VS_JUST_MY_CODE_DEBUGGING FALSE INTERPROCEDURAL_OPTIMIZATION FALSE)
target_compile_options(guardian_kernel PRIVATE /W4 /WX /utf-8 /permissive-
    /kernel /EHs-c- /GR- /Zl /GS- /JMC-)
target_link_options(guardian_kernel PRIVATE /NODEFAULTLIB /SUBSYSTEM:NATIVE
    /ENTRY:GuardianKernelEntry /BASE:0x200000 /FIXED /DYNAMICBASE:NO
    /INCREMENTAL:NO /MANIFEST:NO /PDBALTPATH:guardian-kernel.pdb)
add_dependencies(guardian_guest guardian_kernel)

enable_language(ASM_MASM)
function(guardian_cpu_image target output)
    add_executable(${target} CpuKernel.cpp Cpu.asm Memory.cpp TelemetryKernel.cpp)
    target_link_libraries(${target} PRIVATE guardian_abi)
    set_target_properties(${target} PROPERTIES OUTPUT_NAME "${output}" SUFFIX .gk
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/guest/$<CONFIG>"
        MSVC_RUNTIME_CHECKS "" MSVC_RUNTIME_LIBRARY "" VS_JUST_MY_CODE_DEBUGGING FALSE INTERPROCEDURAL_OPTIMIZATION FALSE)
    target_compile_options(${target} PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:/W4;/WX;/utf-8;/permissive-;/kernel;/EHs-c-;/GR-;/Zl;/GS-;/JMC->")
    target_link_options(${target} PRIVATE /NODEFAULTLIB /SUBSYSTEM:NATIVE /ENTRY:GuardianCpuEntry /BASE:0x200000
        /FIXED /DYNAMICBASE:NO /INCREMENTAL:NO /MANIFEST:NO "/PDBALTPATH:${output}.pdb")
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/normalize_kernel_image.py" $<TARGET_FILE:${target}> VERBATIM)
endfunction()
guardian_cpu_image(guardian_kernel_cpu guardian-kernel-cpu)
add_dependencies(guardian_guest guardian_kernel_cpu)
guardian_cpu_image(guardian_kernel_memory guardian-kernel-memory)
target_sources(guardian_kernel_memory PRIVATE MemoryManager.cpp MemoryKernel.cpp MemoryUser.asm)
target_compile_definitions(guardian_kernel_memory PRIVATE GUARDIAN_MEMORY_KERNEL=1)
add_dependencies(guardian_guest guardian_kernel_memory)
guardian_cpu_image(guardian_kernel_threads guardian-kernel-threads)
target_sources(guardian_kernel_threads PRIVATE MemoryManager.cpp Scheduler.cpp SchedulerChecks.cpp ThreadKernel.cpp ThreadUser.asm)
target_compile_definitions(guardian_kernel_threads PRIVATE GUARDIAN_THREAD_KERNEL=1)
add_dependencies(guardian_guest guardian_kernel_threads)
guardian_cpu_image(guardian_kernel_processes guardian-kernel-processes)
target_sources(guardian_kernel_processes PRIVATE MemoryManager.cpp Scheduler.cpp ProcessManager.cpp ProcessChecks.cpp ProcessKernel.cpp ProcessUser.asm ThreadUser.asm StackProbe.asm)
target_compile_definitions(guardian_kernel_processes PRIVATE GUARDIAN_PROCESS_KERNEL=1)
add_dependencies(guardian_guest guardian_kernel_processes)
guardian_cpu_image(guardian_kernel_io guardian-kernel-io)
target_sources(guardian_kernel_io PRIVATE MemoryManager.cpp Scheduler.cpp ProcessManager.cpp ProcessChecks.cpp ProcessKernel.cpp ProcessUser.asm ThreadUser.asm StackProbe.asm IoManager.cpp IoChecks.cpp IoKernel.cpp)
target_compile_definitions(guardian_kernel_io PRIVATE GUARDIAN_PROCESS_KERNEL=1 GUARDIAN_IO_KERNEL=1)
add_dependencies(guardian_guest guardian_kernel_io)
guardian_cpu_image(guardian_kernel_storage guardian-kernel-storage)
target_sources(guardian_kernel_storage PRIVATE MemoryManager.cpp Scheduler.cpp ProcessManager.cpp ProcessChecks.cpp ProcessKernel.cpp ProcessUser.asm ThreadUser.asm StackProbe.asm IoManager.cpp IoChecks.cpp IoKernel.cpp Volume.cpp StorageChecks.cpp StorageKernel.cpp)
target_compile_definitions(guardian_kernel_storage PRIVATE GUARDIAN_PROCESS_KERNEL=1 GUARDIAN_IO_KERNEL=1 GUARDIAN_STORAGE_KERNEL=1)
add_dependencies(guardian_guest guardian_kernel_storage)

# Owned console/DLL inputs for phase 8; no Windows binaries or external samples.
add_library(guardian_pe_fixture_dll SHARED fixtures/PeDll.cpp fixtures/PeDll.def)
add_executable(guardian_pe_fixture_app fixtures/PeConsole.cpp)
target_link_libraries(guardian_pe_fixture_app PRIVATE guardian_pe_fixture_dll)
foreach(pe_target IN ITEMS guardian_pe_fixture_dll guardian_pe_fixture_app)
    set_target_properties(${pe_target} PROPERTIES MSVC_RUNTIME_CHECKS "" MSVC_RUNTIME_LIBRARY ""
        VS_JUST_MY_CODE_DEBUGGING FALSE INTERPROCEDURAL_OPTIMIZATION FALSE
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/guest/fixtures/$<CONFIG>")
    target_compile_options(${pe_target} PRIVATE /W4 /WX /utf-8 /permissive- /EHs-c- /GR- /Zl /GS- /JMC-)
    target_link_options(${pe_target} PRIVATE /NODEFAULTLIB /INCREMENTAL:NO /MANIFEST:NO /DYNAMICBASE /FIXED:NO /INCLUDE:_tls_used /DEBUG:NONE)
endforeach()
set_target_properties(guardian_pe_fixture_dll PROPERTIES OUTPUT_NAME guardian-pe-runtime)
set_target_properties(guardian_pe_fixture_app PROPERTIES OUTPUT_NAME guardian-pe-console)
target_link_options(guardian_pe_fixture_dll PRIVATE /ENTRY:DllEntry)
target_link_options(guardian_pe_fixture_app PRIVATE /SUBSYSTEM:CONSOLE /ENTRY:GuardianEntry)
set(pe_fixture_header "${PROJECT_BINARY_DIR}/generated/$<CONFIG>/PeFixtures.h")
add_custom_command(OUTPUT "${pe_fixture_header}"
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/embed_pe_fixtures.py" "${pe_fixture_header}"
        $<TARGET_FILE:guardian_pe_fixture_app> $<TARGET_FILE:guardian_pe_fixture_dll>
    DEPENDS guardian_pe_fixture_app guardian_pe_fixture_dll "${PROJECT_SOURCE_DIR}/tools/embed_pe_fixtures.py" VERBATIM)
add_custom_target(guardian_pe_fixtures DEPENDS "${pe_fixture_header}")
guardian_cpu_image(guardian_kernel_loader guardian-kernel-loader)
target_sources(guardian_kernel_loader PRIVATE MemoryManager.cpp Scheduler.cpp ProcessManager.cpp ProcessChecks.cpp ProcessKernel.cpp ProcessUser.asm ThreadUser.asm StackProbe.asm IoManager.cpp IoChecks.cpp IoKernel.cpp Volume.cpp StorageChecks.cpp StorageKernel.cpp PeLoader.cpp PeKernel.cpp)
target_compile_definitions(guardian_kernel_loader PRIVATE GUARDIAN_PROCESS_KERNEL=1 GUARDIAN_IO_KERNEL=1 GUARDIAN_STORAGE_KERNEL=1 GUARDIAN_PE_KERNEL=1)
target_include_directories(guardian_kernel_loader PRIVATE "${PROJECT_BINARY_DIR}/generated/$<CONFIG>")
add_dependencies(guardian_kernel_loader guardian_pe_fixtures)
add_dependencies(guardian_guest guardian_kernel_loader)

add_library(guardian_runtime_fixture_dll SHARED fixtures/PeRuntimeDll.cpp fixtures/PeRuntime.cpp fixtures/PeRuntimeDll.def fixtures/PeRuntimeCall.asm Memory.cpp StackProbe.asm)
target_sources(guardian_runtime_fixture_dll PRIVATE fixtures/PeSystemRuntime.cpp)
add_executable(guardian_runtime_fixture_app fixtures/PeRuntimeConsole.cpp fixtures/PeRuntimeCall.asm Memory.cpp StackProbe.asm)
target_link_libraries(guardian_runtime_fixture_dll PRIVATE guardian_abi)
target_link_libraries(guardian_runtime_fixture_app PRIVATE guardian_runtime_fixture_dll guardian_abi)
foreach(rt_target IN ITEMS guardian_runtime_fixture_dll guardian_runtime_fixture_app)
    set_target_properties(${rt_target} PROPERTIES MSVC_RUNTIME_CHECKS "" MSVC_RUNTIME_LIBRARY ""
        VS_JUST_MY_CODE_DEBUGGING FALSE INTERPROCEDURAL_OPTIMIZATION FALSE
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/guest/fixtures/$<CONFIG>")
    target_compile_options(${rt_target} PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:/W4;/WX;/utf-8;/permissive-;/EHs-c-;/GR-;/Zl;/GS-;/JMC->")
    target_link_options(${rt_target} PRIVATE /NODEFAULTLIB /INCREMENTAL:NO /MANIFEST:NO /DYNAMICBASE /FIXED:NO /INCLUDE:_tls_used /DEBUG:NONE)
endforeach()
set_target_properties(guardian_runtime_fixture_dll PROPERTIES OUTPUT_NAME guardian-runtime-api)
set_target_properties(guardian_runtime_fixture_app PROPERTIES OUTPUT_NAME guardian-runtime-console)
target_link_options(guardian_runtime_fixture_dll PRIVATE /ENTRY:DllEntry)
target_link_options(guardian_runtime_fixture_app PRIVATE /SUBSYSTEM:CONSOLE /ENTRY:GuardianEntry)
set(rt_header "${PROJECT_BINARY_DIR}/generated/$<CONFIG>/PeRuntimeFixtures.h")
add_custom_command(OUTPUT "${rt_header}"
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/embed_runtime_fixtures.py" "${rt_header}"
        $<TARGET_FILE:guardian_runtime_fixture_app> $<TARGET_FILE:guardian_runtime_fixture_dll>
    DEPENDS guardian_runtime_fixture_app guardian_runtime_fixture_dll "${PROJECT_SOURCE_DIR}/tools/embed_runtime_fixtures.py" VERBATIM)
add_custom_target(guardian_runtime_fixtures DEPENDS "${rt_header}")
guardian_cpu_image(guardian_kernel_runtime guardian-kernel-runtime)
target_sources(guardian_kernel_runtime PRIVATE MemoryManager.cpp Scheduler.cpp ProcessManager.cpp ProcessChecks.cpp ProcessKernel.cpp ProcessUser.asm ThreadUser.asm StackProbe.asm IoManager.cpp IoChecks.cpp IoKernel.cpp Volume.cpp StorageChecks.cpp StorageKernel.cpp PeLoader.cpp PeKernel.cpp PeServices.cpp)
target_compile_definitions(guardian_kernel_runtime PRIVATE GUARDIAN_PROCESS_KERNEL=1 GUARDIAN_IO_KERNEL=1 GUARDIAN_STORAGE_KERNEL=1 GUARDIAN_PE_KERNEL=1 GUARDIAN_RUNTIME_KERNEL=1)
target_include_directories(guardian_kernel_runtime PRIVATE "${PROJECT_BINARY_DIR}/generated/$<CONFIG>")
add_dependencies(guardian_kernel_runtime guardian_pe_fixtures guardian_runtime_fixtures)
add_dependencies(guardian_guest guardian_kernel_runtime)

foreach(life_kind IN ITEMS app child)
    if(life_kind STREQUAL "app")
        set(life_source fixtures/PeLifetimeConsole.cpp)
        set(life_output guardian-lifetime-console)
    else()
        set(life_source fixtures/PeLifetimeChild.cpp)
        set(life_output guardian-lifetime-child)
    endif()
    add_executable(guardian_lifetime_${life_kind} ${life_source} fixtures/PeRuntimeCall.asm Memory.cpp StackProbe.asm)
    target_link_libraries(guardian_lifetime_${life_kind} PRIVATE guardian_runtime_fixture_dll guardian_abi)
    set_target_properties(guardian_lifetime_${life_kind} PROPERTIES OUTPUT_NAME ${life_output} MSVC_RUNTIME_CHECKS "" MSVC_RUNTIME_LIBRARY ""
        VS_JUST_MY_CODE_DEBUGGING FALSE INTERPROCEDURAL_OPTIMIZATION FALSE RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/guest/fixtures/$<CONFIG>")
    target_compile_options(guardian_lifetime_${life_kind} PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:/W4;/WX;/utf-8;/permissive-;/EHs-c-;/GR-;/Zl;/GS-;/JMC->")
    target_link_options(guardian_lifetime_${life_kind} PRIVATE /NODEFAULTLIB /INCREMENTAL:NO /MANIFEST:NO /DYNAMICBASE /FIXED:NO /INCLUDE:_tls_used /DEBUG:NONE /SUBSYSTEM:CONSOLE /ENTRY:GuardianEntry)
endforeach()
set(life_header "${PROJECT_BINARY_DIR}/generated/$<CONFIG>/PeLifetimeFixtures.h")
add_custom_command(OUTPUT "${life_header}"
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/embed_lifetime_fixtures.py" "${life_header}" $<TARGET_FILE:guardian_lifetime_app> $<TARGET_FILE:guardian_lifetime_child>
    DEPENDS guardian_lifetime_app guardian_lifetime_child "${PROJECT_SOURCE_DIR}/tools/embed_lifetime_fixtures.py" VERBATIM)
add_custom_target(guardian_lifetime_fixtures DEPENDS "${life_header}")
guardian_cpu_image(guardian_kernel_lifetime guardian-kernel-lifetime)
target_sources(guardian_kernel_lifetime PRIVATE Unwind.cpp MemoryManager.cpp Scheduler.cpp ProcessManager.cpp ProcessChecks.cpp ProcessKernel.cpp ProcessUser.asm ThreadUser.asm StackProbe.asm IoManager.cpp IoChecks.cpp IoKernel.cpp Volume.cpp StorageChecks.cpp StorageKernel.cpp PeLoader.cpp PeKernel.cpp PeServices.cpp)
target_compile_definitions(guardian_kernel_lifetime PRIVATE GUARDIAN_PROCESS_KERNEL=1 GUARDIAN_IO_KERNEL=1 GUARDIAN_STORAGE_KERNEL=1 GUARDIAN_PE_KERNEL=1 GUARDIAN_RUNTIME_KERNEL=1 GUARDIAN_LIFETIME_KERNEL=1)
target_include_directories(guardian_kernel_lifetime PRIVATE "${PROJECT_BINARY_DIR}/generated/$<CONFIG>")
add_dependencies(guardian_kernel_lifetime guardian_pe_fixtures guardian_runtime_fixtures guardian_lifetime_fixtures)
add_dependencies(guardian_guest guardian_kernel_lifetime)
if(BUILD_TESTING)
    guardian_cpu_image(guardian_kernel_cpu_fatal guardian-kernel-cpu-fatal)
    target_compile_definitions(guardian_kernel_cpu_fatal PRIVATE GUARDIAN_CPU_TEST_FATAL=1)
    guardian_cpu_image(guardian_kernel_cpu_double_fault guardian-kernel-cpu-double-fault)
    target_compile_definitions(guardian_kernel_cpu_double_fault PRIVATE GUARDIAN_CPU_TEST_FATAL=2)
endif()

# Final phase-8 workload: standard x64 language handlers plus dynamic DLLs.
target_sources(guardian_runtime_fixture_dll PRIVATE fixtures/PeExceptionRuntime.cpp fixtures/PeLibraryRuntime.cpp)
add_executable(guardian_complete_app fixtures/PeCompleteConsole.cpp fixtures/PeExceptionProbe.asm fixtures/PeRuntimeCall.asm Memory.cpp StackProbe.asm)
add_executable(guardian_complete_child fixtures/PeCompleteChild.cpp fixtures/PeExceptionProbe.asm fixtures/PeRuntimeCall.asm Memory.cpp StackProbe.asm)
add_library(guardian_dynamic_dll SHARED fixtures/PeDynamicDll.cpp fixtures/PeDynamicDll.def)
add_library(guardian_failing_dll SHARED fixtures/PeFailingDll.cpp fixtures/PeFailingDll.def)
foreach(complete_target IN ITEMS guardian_complete_app guardian_complete_child guardian_dynamic_dll guardian_failing_dll)
    target_link_libraries(${complete_target} PRIVATE guardian_runtime_fixture_dll guardian_abi)
    set_target_properties(${complete_target} PROPERTIES MSVC_RUNTIME_CHECKS "" MSVC_RUNTIME_LIBRARY ""
        VS_JUST_MY_CODE_DEBUGGING FALSE INTERPROCEDURAL_OPTIMIZATION FALSE
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/guest/fixtures/$<CONFIG>")
    target_compile_options(${complete_target} PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:/W4;/WX;/utf-8;/permissive-;/EHs-c-;/GR-;/Zl;/GS-;/JMC->")
    target_link_options(${complete_target} PRIVATE /NODEFAULTLIB /INCREMENTAL:NO /MANIFEST:NO /DYNAMICBASE /FIXED:NO /INCLUDE:_tls_used /DEBUG:NONE)
endforeach()
set_target_properties(guardian_complete_app PROPERTIES OUTPUT_NAME guardian-complete-console)
set_target_properties(guardian_complete_child PROPERTIES OUTPUT_NAME guardian-complete-child)
set_target_properties(guardian_dynamic_dll PROPERTIES OUTPUT_NAME guardian-dynamic)
set_target_properties(guardian_failing_dll PROPERTIES OUTPUT_NAME guardian-failing)
target_link_options(guardian_complete_app PRIVATE /SUBSYSTEM:CONSOLE /ENTRY:GuardianEntry)
target_link_options(guardian_complete_child PRIVATE /SUBSYSTEM:CONSOLE /ENTRY:GuardianEntry)
target_link_options(guardian_dynamic_dll PRIVATE /ENTRY:DllEntry)
target_link_options(guardian_failing_dll PRIVATE /ENTRY:DllEntry)
set(complete_header "${PROJECT_BINARY_DIR}/generated/$<CONFIG>/PeCompleteFixtures.h")
add_custom_command(OUTPUT "${complete_header}"
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/embed_complete_fixtures.py" "${complete_header}"
        $<TARGET_FILE:guardian_complete_app> $<TARGET_FILE:guardian_complete_child>
        $<TARGET_FILE:guardian_dynamic_dll> $<TARGET_FILE:guardian_failing_dll>
    DEPENDS guardian_complete_app guardian_complete_child guardian_dynamic_dll guardian_failing_dll
            "${PROJECT_SOURCE_DIR}/tools/embed_complete_fixtures.py" VERBATIM)
add_custom_target(guardian_complete_fixtures DEPENDS "${complete_header}")
guardian_cpu_image(guardian_kernel_complete guardian-kernel-complete)
target_sources(guardian_kernel_complete PRIVATE Unwind.cpp ExceptionDispatch.cpp PeExceptions.cpp MemoryManager.cpp
    Scheduler.cpp ProcessManager.cpp ProcessChecks.cpp ProcessKernel.cpp ProcessUser.asm ThreadUser.asm StackProbe.asm
    IoManager.cpp IoChecks.cpp IoKernel.cpp Volume.cpp StorageChecks.cpp StorageKernel.cpp PeLoader.cpp PeKernel.cpp PeServices.cpp)
target_compile_definitions(guardian_kernel_complete PRIVATE GUARDIAN_PROCESS_KERNEL=1 GUARDIAN_IO_KERNEL=1 GUARDIAN_STORAGE_KERNEL=1
    GUARDIAN_PE_KERNEL=1 GUARDIAN_RUNTIME_KERNEL=1 GUARDIAN_LIFETIME_KERNEL=1 GUARDIAN_COMPLETE_KERNEL=1)
target_include_directories(guardian_kernel_complete PRIVATE "${PROJECT_BINARY_DIR}/generated/$<CONFIG>")
add_dependencies(guardian_kernel_complete guardian_pe_fixtures guardian_runtime_fixtures guardian_complete_fixtures)
add_dependencies(guardian_guest guardian_kernel_complete)

# The same owned case program is compiled against Win32 by the native oracle.
add_executable(guardian_conformance_app fixtures/PeConformance.cpp fixtures/PeExceptionProbe.asm fixtures/PeRuntimeCall.asm Memory.cpp StackProbe.asm)
target_link_libraries(guardian_conformance_app PRIVATE guardian_runtime_fixture_dll guardian_abi)
set_target_properties(guardian_conformance_app PROPERTIES OUTPUT_NAME guardian-conformance
    MSVC_RUNTIME_CHECKS "" MSVC_RUNTIME_LIBRARY "" VS_JUST_MY_CODE_DEBUGGING FALSE INTERPROCEDURAL_OPTIMIZATION FALSE
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/guest/fixtures/$<CONFIG>")
target_compile_options(guardian_conformance_app PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:/W4;/WX;/utf-8;/permissive-;/EHs-c-;/GR-;/Zl;/GS-;/JMC->")
target_link_options(guardian_conformance_app PRIVATE /NODEFAULTLIB /INCREMENTAL:NO /MANIFEST:NO /DYNAMICBASE /FIXED:NO /INCLUDE:_tls_used /DEBUG:NONE /SUBSYSTEM:CONSOLE /ENTRY:GuardianEntry)
set(conformance_header "${PROJECT_BINARY_DIR}/generated/$<CONFIG>/PeConformanceFixtures.h")
add_custom_command(OUTPUT "${conformance_header}"
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/embed_conformance_fixtures.py" "${conformance_header}" $<TARGET_FILE:guardian_conformance_app>
    DEPENDS guardian_conformance_app "${PROJECT_SOURCE_DIR}/tools/embed_conformance_fixtures.py" VERBATIM)
add_custom_target(guardian_conformance_fixtures DEPENDS "${conformance_header}")
guardian_cpu_image(guardian_kernel_conformance guardian-kernel-conformance)
get_target_property(conformance_sources guardian_kernel_complete SOURCES)
list(REMOVE_ITEM conformance_sources CpuKernel.cpp Cpu.asm Memory.cpp TelemetryKernel.cpp)
target_sources(guardian_kernel_conformance PRIVATE ${conformance_sources})
get_target_property(conformance_definitions guardian_kernel_complete COMPILE_DEFINITIONS)
target_compile_definitions(guardian_kernel_conformance PRIVATE ${conformance_definitions} GUARDIAN_CONFORMANCE_KERNEL=1)
target_include_directories(guardian_kernel_conformance PRIVATE "${PROJECT_BINARY_DIR}/generated/$<CONFIG>")
add_dependencies(guardian_kernel_conformance guardian_pe_fixtures guardian_runtime_fixtures guardian_complete_fixtures guardian_conformance_fixtures)
add_dependencies(guardian_guest guardian_kernel_conformance)

# Phase 12D: owned wait/reboot persistence application.
add_executable(guardian_session_app fixtures/PeSession.cpp fixtures/PeExceptionProbe.asm fixtures/PeRuntimeCall.asm Memory.cpp StackProbe.asm)
target_link_libraries(guardian_session_app PRIVATE guardian_runtime_fixture_dll guardian_abi)
set_target_properties(guardian_session_app PROPERTIES OUTPUT_NAME guardian-session
    MSVC_RUNTIME_CHECKS "" MSVC_RUNTIME_LIBRARY "" VS_JUST_MY_CODE_DEBUGGING FALSE INTERPROCEDURAL_OPTIMIZATION FALSE
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/guest/fixtures/$<CONFIG>")
target_compile_options(guardian_session_app PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:/W4;/WX;/utf-8;/permissive-;/EHs-c-;/GR-;/Zl;/GS-;/JMC->")
target_link_options(guardian_session_app PRIVATE /NODEFAULTLIB /INCREMENTAL:NO /MANIFEST:NO /DYNAMICBASE /FIXED:NO /INCLUDE:_tls_used /DEBUG:NONE /SUBSYSTEM:CONSOLE /ENTRY:GuardianEntry)
set(session_header "${PROJECT_BINARY_DIR}/generated/$<CONFIG>/PeSessionFixtures.h")
add_custom_command(OUTPUT "${session_header}"
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/embed_session_fixtures.py" "${session_header}" $<TARGET_FILE:guardian_session_app>
    DEPENDS guardian_session_app "${PROJECT_SOURCE_DIR}/tools/embed_session_fixtures.py" VERBATIM)
add_custom_target(guardian_session_fixtures DEPENDS "${session_header}")
guardian_cpu_image(guardian_kernel_session guardian-kernel-session)
get_target_property(session_sources guardian_kernel_complete SOURCES)
list(REMOVE_ITEM session_sources CpuKernel.cpp Cpu.asm Memory.cpp TelemetryKernel.cpp)
target_sources(guardian_kernel_session PRIVATE ${session_sources})
get_target_property(session_definitions guardian_kernel_complete COMPILE_DEFINITIONS)
target_compile_definitions(guardian_kernel_session PRIVATE ${session_definitions} GUARDIAN_SESSION_KERNEL=1)
target_include_directories(guardian_kernel_session PRIVATE "${PROJECT_BINARY_DIR}/generated/$<CONFIG>")
add_dependencies(guardian_kernel_session guardian_pe_fixtures guardian_runtime_fixtures guardian_complete_fixtures guardian_session_fixtures)
add_dependencies(guardian_guest guardian_kernel_session)

# Owned synthetic workspace and actual reader from its immutable guest catalog.
foreach(env_application IN ITEMS environment docs)
    if(env_application STREQUAL "environment")
        set(env_source fixtures/PeEnvironment.cpp)
    else()
        set(env_source fixtures/PeEnvironmentDocs.cpp)
    endif()
    add_executable(guardian_${env_application}_app ${env_source} fixtures/PeRuntimeCall.asm Memory.cpp StackProbe.asm)
    target_link_libraries(guardian_${env_application}_app PRIVATE guardian_runtime_fixture_dll guardian_abi)
    set_target_properties(guardian_${env_application}_app PROPERTIES OUTPUT_NAME guardian-${env_application}
        MSVC_RUNTIME_CHECKS "" MSVC_RUNTIME_LIBRARY "" VS_JUST_MY_CODE_DEBUGGING FALSE INTERPROCEDURAL_OPTIMIZATION FALSE
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/guest/fixtures/$<CONFIG>")
    target_compile_options(guardian_${env_application}_app PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:/W4;/WX;/utf-8;/permissive-;/EHs-c-;/GR-;/Zl;/GS-;/JMC->")
    target_link_options(guardian_${env_application}_app PRIVATE /NODEFAULTLIB /INCREMENTAL:NO /MANIFEST:NO /DYNAMICBASE /FIXED:NO /INCLUDE:_tls_used /DEBUG:NONE /SUBSYSTEM:CONSOLE /ENTRY:GuardianEntry)
endforeach()
set(environment_header "${PROJECT_BINARY_DIR}/generated/$<CONFIG>/PeEnvironmentFixtures.h")
add_custom_command(OUTPUT "${environment_header}"
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/embed_environment_fixtures.py" "${environment_header}"
        $<TARGET_FILE:guardian_environment_app> $<TARGET_FILE:guardian_docs_app>
    DEPENDS guardian_environment_app guardian_docs_app "${PROJECT_SOURCE_DIR}/tools/embed_environment_fixtures.py" VERBATIM)
add_custom_target(guardian_environment_fixtures DEPENDS "${environment_header}")
guardian_cpu_image(guardian_kernel_environment guardian-kernel-environment)
get_target_property(environment_sources guardian_kernel_complete SOURCES)
list(REMOVE_ITEM environment_sources CpuKernel.cpp Cpu.asm Memory.cpp TelemetryKernel.cpp)
target_sources(guardian_kernel_environment PRIVATE ${environment_sources})
get_target_property(environment_definitions guardian_kernel_complete COMPILE_DEFINITIONS)
target_compile_definitions(guardian_kernel_environment PRIVATE ${environment_definitions} GUARDIAN_ENVIRONMENT_KERNEL=1)
target_include_directories(guardian_kernel_environment PRIVATE "${PROJECT_BINARY_DIR}/generated/$<CONFIG>")
add_dependencies(guardian_kernel_environment guardian_pe_fixtures guardian_runtime_fixtures guardian_complete_fixtures guardian_environment_fixtures)
add_dependencies(guardian_guest guardian_kernel_environment)

guardian_cpu_image(guardian_kernel_network guardian-kernel-network)
target_sources(guardian_kernel_network PRIVATE NetworkStack.cpp NetworkDriver.cpp StackProbe.asm)
target_compile_definitions(guardian_kernel_network PRIVATE GUARDIAN_NETWORK_KERNEL=1)
add_dependencies(guardian_guest guardian_kernel_network)

# Owned CPL3 graphical applications use the explicit graphics device ABI.
guardian_cpu_image(guardian_kernel_graphics guardian-kernel-graphics)
target_sources(guardian_kernel_graphics PRIVATE Graphics.cpp GraphicsDriver.cpp StackProbe.asm)
target_compile_definitions(guardian_kernel_graphics PRIVATE GUARDIAN_GRAPHICS_KERNEL=1)
target_include_directories(guardian_kernel_graphics PRIVATE "${PROJECT_SOURCE_DIR}/guest")
add_dependencies(guardian_guest guardian_kernel_graphics)

include("${PROJECT_SOURCE_DIR}/cmake/GuardianDriverGuest.cmake")

# Phase 12B resource kernel: real SMP and guest-owned scalable filesystem.
add_executable(guardian_kernel_resources ResourceKernel.cpp ScalableVolume.cpp Memory.cpp StackProbe.asm)
target_link_libraries(guardian_kernel_resources PRIVATE guardian_abi)
set_target_properties(guardian_kernel_resources PROPERTIES OUTPUT_NAME guardian-kernel-resources SUFFIX .gk
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/guest/$<CONFIG>" MSVC_RUNTIME_CHECKS "" MSVC_RUNTIME_LIBRARY ""
    VS_JUST_MY_CODE_DEBUGGING FALSE INTERPROCEDURAL_OPTIMIZATION FALSE)
target_compile_options(guardian_kernel_resources PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:/W4;/WX;/utf-8;/permissive-;/kernel;/EHs-c-;/GR-;/Zl;/GS-;/JMC->")
target_link_options(guardian_kernel_resources PRIVATE /NODEFAULTLIB /SUBSYSTEM:NATIVE /ENTRY:GuardianResourceEntry /BASE:0x200000
    /FIXED /DYNAMICBASE:NO /INCREMENTAL:NO /MANIFEST:NO)
add_custom_command(TARGET guardian_kernel_resources POST_BUILD
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/normalize_kernel_image.py" $<TARGET_FILE:guardian_kernel_resources> VERBATIM)
add_dependencies(guardian_guest guardian_kernel_resources)

foreach(analysis_application IN ITEMS analysis analysis_child)
    if(analysis_application STREQUAL "analysis")
        set(analysis_source fixtures/PeAnalysis.cpp)
    else()
        set(analysis_source fixtures/PeAnalysisChild.cpp)
    endif()
    add_executable(guardian_${analysis_application}_app ${analysis_source} fixtures/PeRuntimeCall.asm Memory.cpp StackProbe.asm)
    target_link_libraries(guardian_${analysis_application}_app PRIVATE guardian_runtime_fixture_dll guardian_abi)
    set_target_properties(guardian_${analysis_application}_app PROPERTIES MSVC_RUNTIME_CHECKS "" MSVC_RUNTIME_LIBRARY "" VS_JUST_MY_CODE_DEBUGGING FALSE RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/guest/fixtures/$<CONFIG>")
    target_compile_options(guardian_${analysis_application}_app PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:/W4;/WX;/utf-8;/permissive-;/EHs-c-;/GR-;/Zl;/GS-;/JMC->")
    target_link_options(guardian_${analysis_application}_app PRIVATE /NODEFAULTLIB /INCREMENTAL:NO /MANIFEST:NO /DYNAMICBASE /FIXED:NO /INCLUDE:_tls_used /DEBUG:NONE /SUBSYSTEM:CONSOLE /ENTRY:GuardianEntry)
endforeach()
set_target_properties(guardian_analysis_app PROPERTIES OUTPUT_NAME guardian-analysis)
set_target_properties(guardian_analysis_child_app PROPERTIES OUTPUT_NAME guardian-analysis-child)
set(analysis_header "${PROJECT_BINARY_DIR}/generated/$<CONFIG>/PeAnalysisFixtures.h")
add_custom_command(OUTPUT "${analysis_header}" COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/embed_analysis_fixtures.py" "${analysis_header}" $<TARGET_FILE:guardian_analysis_app> $<TARGET_FILE:guardian_analysis_child_app> DEPENDS guardian_analysis_app guardian_analysis_child_app "${PROJECT_SOURCE_DIR}/tools/embed_analysis_fixtures.py" VERBATIM)
add_custom_target(guardian_analysis_fixtures DEPENDS "${analysis_header}")
guardian_cpu_image(guardian_kernel_analysis guardian-kernel-analysis)
get_target_property(analysis_sources guardian_kernel_complete SOURCES)
list(REMOVE_ITEM analysis_sources CpuKernel.cpp Cpu.asm Memory.cpp TelemetryKernel.cpp)
target_sources(guardian_kernel_analysis PRIVATE ${analysis_sources} NetworkStack.cpp NetworkDriver.cpp)
get_target_property(analysis_definitions guardian_kernel_complete COMPILE_DEFINITIONS)
target_compile_definitions(guardian_kernel_analysis PRIVATE ${analysis_definitions} GUARDIAN_ANALYSIS_KERNEL=1 GUARDIAN_NETWORK_KERNEL=1)
target_include_directories(guardian_kernel_analysis PRIVATE "${PROJECT_BINARY_DIR}/generated/$<CONFIG>")
add_dependencies(guardian_kernel_analysis guardian_pe_fixtures guardian_runtime_fixtures guardian_complete_fixtures guardian_analysis_fixtures)
add_dependencies(guardian_guest guardian_kernel_analysis)
