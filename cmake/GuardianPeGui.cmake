# A separate, bounded experiment: external GUI PE + guest-owned USER32 subset.
# Existing kernels and their regression contracts remain independent.
option(GUARDIAN_BUILD_PE_GUI "Build the experimental external PE GUI contract" ${GUARDIAN_GUI_COMPATIBILITY_ONLY})
if(NOT GUARDIAN_BUILD_PE_GUI OR NOT TARGET guardian_kernel_runtime)
    return()
endif()
add_library(guardian_gui_user32 SHARED guest/fixtures/PeGuiUser32.cpp
    guest/fixtures/PeGuiUser32.def guest/fixtures/PeGuiCall.asm guest/fixtures/PeRuntimeCall.asm guest/Memory.cpp)
add_executable(guardian_gui_application guest/fixtures/PeGuiApplication.cpp guest/Memory.cpp)
target_sources(guardian_gui_application PRIVATE guest/fixtures/PeGuiCall.asm)
target_compile_definitions(guardian_gui_application PRIVATE GUARDIAN_GUI_GUEST_TEST=1)
target_link_libraries(guardian_gui_user32 PRIVATE guardian_abi)
target_link_libraries(guardian_gui_application PRIVATE guardian_abi guardian_gui_user32)
foreach(target IN ITEMS guardian_gui_user32 guardian_gui_application)
    set_target_properties(${target} PROPERTIES MSVC_RUNTIME_CHECKS "" MSVC_RUNTIME_LIBRARY ""
        VS_JUST_MY_CODE_DEBUGGING FALSE INTERPROCEDURAL_OPTIMIZATION FALSE
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/guest/fixtures/$<CONFIG>")
    set_target_properties(${target} PROPERTIES ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/guest/imports/$<CONFIG>")
    target_compile_options(${target} PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:/W4;/WX;/utf-8;/permissive-;/EHs-c-;/GR-;/Zl;/GS-;/JMC->")
    target_link_options(${target} PRIVATE /NODEFAULTLIB /INCREMENTAL:NO /MANIFEST:NO /DYNAMICBASE /FIXED:NO /DEBUG:NONE)
endforeach()
set_target_properties(guardian_gui_user32 PROPERTIES OUTPUT_NAME user32)
set_target_properties(guardian_gui_application PROPERTIES OUTPUT_NAME guardian-gui-application)
target_link_options(guardian_gui_user32 PRIVATE /ENTRY:DllEntry)
target_link_options(guardian_gui_application PRIVATE /ENTRY:GuiEntry /SUBSYSTEM:WINDOWS)
set(gui_header "${PROJECT_BINARY_DIR}/generated/$<CONFIG>/PeGuiFixtures.h")
add_custom_command(OUTPUT "${gui_header}"
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/embed_gui_runtime.py" "${gui_header}" $<TARGET_FILE:guardian_gui_user32>
    DEPENDS guardian_gui_user32 "${PROJECT_SOURCE_DIR}/tools/embed_gui_runtime.py" VERBATIM)
add_custom_target(guardian_gui_runtime DEPENDS "${gui_header}")
# guardian_cpu_image was defined in the guest directory; use explicit paths here.
add_executable(guardian_kernel_pe_gui guest/CpuKernel.cpp guest/Cpu.asm guest/Memory.cpp guest/TelemetryKernel.cpp)
get_target_property(gui_kernel_sources guardian_kernel_runtime SOURCES)
list(REMOVE_ITEM gui_kernel_sources CpuKernel.cpp Cpu.asm Memory.cpp TelemetryKernel.cpp)
list(REMOVE_ITEM gui_kernel_sources IoManager.cpp IoChecks.cpp IoKernel.cpp Volume.cpp StorageChecks.cpp StorageKernel.cpp)
foreach(source IN LISTS gui_kernel_sources)
    target_sources(guardian_kernel_pe_gui PRIVATE "guest/${source}")
endforeach()
target_sources(guardian_kernel_pe_gui PRIVATE guest/Graphics.cpp guest/GraphicsDriver.cpp)
target_link_libraries(guardian_kernel_pe_gui PRIVATE guardian_abi)
target_include_directories(guardian_kernel_pe_gui PRIVATE "${PROJECT_SOURCE_DIR}/guest" "${PROJECT_BINARY_DIR}/generated/$<CONFIG>")
target_compile_definitions(guardian_kernel_pe_gui PRIVATE GUARDIAN_PROCESS_KERNEL=1
    GUARDIAN_PE_KERNEL=1 GUARDIAN_RUNTIME_KERNEL=1 GUARDIAN_GRAPHICS_KERNEL=1 GUARDIAN_PE_GUI_KERNEL=1)
set_target_properties(guardian_kernel_pe_gui PROPERTIES OUTPUT_NAME guardian-kernel-pe-gui SUFFIX .gk
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/guest/$<CONFIG>"
    MSVC_RUNTIME_CHECKS "" MSVC_RUNTIME_LIBRARY "" VS_JUST_MY_CODE_DEBUGGING FALSE INTERPROCEDURAL_OPTIMIZATION FALSE)
target_compile_options(guardian_kernel_pe_gui PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:/W4;/WX;/utf-8;/permissive-;/kernel;/EHs-c-;/GR-;/Zl;/GS-;/JMC->")
target_link_options(guardian_kernel_pe_gui PRIVATE /NODEFAULTLIB /SUBSYSTEM:NATIVE /ENTRY:GuardianCpuEntry /BASE:0x200000
    /FIXED /DYNAMICBASE:NO /INCREMENTAL:NO /MANIFEST:NO /PDBALTPATH:guardian-kernel-pe-gui.pdb)
add_custom_command(TARGET guardian_kernel_pe_gui POST_BUILD COMMAND "${Python3_EXECUTABLE}"
    "${PROJECT_SOURCE_DIR}/tools/normalize_kernel_image.py" $<TARGET_FILE:guardian_kernel_pe_gui> VERBATIM)
add_dependencies(guardian_kernel_pe_gui guardian_pe_fixtures guardian_runtime_fixtures guardian_gui_runtime)
add_executable(guardian-gui-pe src/microvm/gui/PeMain.cpp src/microvm/gui/Viewer.cpp)
target_link_libraries(guardian-gui-pe PRIVATE guardian_boot_core user32 gdi32)
add_dependencies(guardian-gui-pe guardian_kernel_pe_gui guardian_gui_application)
target_compile_definitions(guardian-gui-pe PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(guardian-gui-pe PRIVATE /W4 /WX /utf-8 /permissive-)
if(BUILD_TESTING)
    add_executable(pe_gui_native_oracle guest/fixtures/PeGuiApplication.cpp guest/Memory.cpp)
    target_link_libraries(pe_gui_native_oracle PRIVATE user32)
    target_compile_definitions(pe_gui_native_oracle PRIVATE GUARDIAN_GUI_NATIVE_ORACLE=1)
    target_compile_options(pe_gui_native_oracle PRIVATE /W4 /WX /utf-8 /permissive- /GS- /Zl /JMC-)
    target_link_options(pe_gui_native_oracle PRIVATE /ENTRY:GuiEntry /SUBSYSTEM:WINDOWS /NODEFAULTLIB /INCREMENTAL:NO)
    set_target_properties(pe_gui_native_oracle PROPERTIES MSVC_RUNTIME_CHECKS "" MSVC_RUNTIME_LIBRARY "" VS_JUST_MY_CODE_DEBUGGING FALSE)
    add_test(NAME pe_gui_native COMMAND pe_gui_native_oracle)
    set_tests_properties(pe_gui_native PROPERTIES TIMEOUT 15 RUN_SERIAL TRUE)
    add_test(NAME pe_gui_inventory COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/test/PeGuiInventoryTests.py"
        $<TARGET_FILE:guardian_gui_application> $<TARGET_FILE:guardian_gui_user32>)
    set_tests_properties(pe_gui_inventory PROPERTIES TIMEOUT 15)
    add_executable(pe_gui_boot_tests test/PeGuiBootTests.cpp)
    target_link_libraries(pe_gui_boot_tests PRIVATE guardian_boot_core)
    target_compile_definitions(pe_gui_boot_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(pe_gui_boot_tests PRIVATE /W4 /WX /utf-8 /permissive-)
    add_dependencies(pe_gui_boot_tests guardian_kernel_pe_gui guardian_gui_application)
    add_test(NAME pe_gui_boot COMMAND pe_gui_boot_tests $<TARGET_FILE:guardian_kernel_pe_gui>
        $<TARGET_FILE:guardian_gui_application> "${PROJECT_BINARY_DIR}/pe-gui-$<CONFIG>")
    set_tests_properties(pe_gui_boot PROPERTIES TIMEOUT 90 SKIP_RETURN_CODE 77 RUN_SERIAL TRUE)
endif()
