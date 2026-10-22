add_library(guardian_driver_api SHARED fixtures/DriverKernelApi.cpp fixtures/DriverKernelApi.def)
add_executable(guardian_driver_probe fixtures/HardwareProbeDriver.cpp)
add_executable(guardian_driver_failing fixtures/HardwareProbeDriver.cpp)
target_compile_definitions(guardian_driver_failing PRIVATE GUARDIAN_DRIVER_FAIL_ENTRY=1)
foreach(driver_target IN ITEMS guardian_driver_api guardian_driver_probe guardian_driver_failing)
    target_link_libraries(${driver_target} PRIVATE guardian_abi)
    set_target_properties(${driver_target} PROPERTIES
        MSVC_RUNTIME_CHECKS "" MSVC_RUNTIME_LIBRARY "" VS_JUST_MY_CODE_DEBUGGING FALSE
        INTERPROCEDURAL_OPTIMIZATION FALSE
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/guest/$<CONFIG>/drivers")
    target_compile_options(${driver_target} PRIVATE /W4 /WX /utf-8 /permissive- /kernel /EHs-c- /GR- /Zl /GS- /JMC-)
    target_link_options(${driver_target} PRIVATE /NODEFAULTLIB /INCREMENTAL:NO /MANIFEST:NO /DYNAMICBASE /FIXED:NO /SUBSYSTEM:NATIVE /DEBUG:NONE)
endforeach()
set_target_properties(guardian_driver_api PROPERTIES OUTPUT_NAME ntoskrnl SUFFIX .exe)
target_link_options(guardian_driver_api PRIVATE /ENTRY:ApiEntry /BASE:0x1C0000)
foreach(driver_target IN ITEMS guardian_driver_probe guardian_driver_failing)
    target_link_libraries(${driver_target} PRIVATE guardian_driver_api)
    set_target_properties(${driver_target} PROPERTIES SUFFIX .sys)
    target_link_options(${driver_target} PRIVATE /ENTRY:DriverEntry /DRIVER /BASE:0x1A0000)
endforeach()
set_target_properties(guardian_driver_probe PROPERTIES OUTPUT_NAME guardian-hardware-probe)
set_target_properties(guardian_driver_failing PROPERTIES OUTPUT_NAME guardian-hardware-probe-failing)
add_custom_command(TARGET guardian_driver_probe POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/guardian-hardware-probe.inf"
        "$<TARGET_FILE_DIR:guardian_driver_probe>/guardian-hardware-probe.inf" VERBATIM)

add_executable(guardian_kernel_hardware Kernel.cpp Memory.cpp TelemetryKernel.cpp DriverKernel.cpp PeLoader.cpp StackProbe.asm)
target_link_libraries(guardian_kernel_hardware PRIVATE guardian_abi)
target_compile_definitions(guardian_kernel_hardware PRIVATE GUARDIAN_DRIVER_KERNEL=1)
set_target_properties(guardian_kernel_hardware PROPERTIES OUTPUT_NAME guardian-kernel-hardware SUFFIX .gk
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/guest/$<CONFIG>" MSVC_RUNTIME_CHECKS "" MSVC_RUNTIME_LIBRARY ""
    VS_JUST_MY_CODE_DEBUGGING FALSE INTERPROCEDURAL_OPTIMIZATION FALSE)
target_compile_options(guardian_kernel_hardware PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:/W4;/WX;/utf-8;/permissive-;/kernel;/EHs-c-;/GR-;/Zl;/GS-;/JMC->")
target_link_options(guardian_kernel_hardware PRIVATE /NODEFAULTLIB /SUBSYSTEM:NATIVE /ENTRY:GuardianKernelEntry /BASE:0x200000
    /FIXED /DYNAMICBASE:NO /INCREMENTAL:NO /MANIFEST:NO /PDBALTPATH:guardian-kernel-hardware.pdb)
add_custom_command(TARGET guardian_kernel_hardware POST_BUILD
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/normalize_kernel_image.py" $<TARGET_FILE:guardian_kernel_hardware> VERBATIM)
add_dependencies(guardian_guest guardian_kernel_hardware guardian_driver_probe guardian_driver_failing)
