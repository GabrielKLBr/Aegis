# Phase 17: WinUI is explicit, so native/kernel builds do not restore NuGet packages.
if(WIN32 AND TARGET guardian_engine_client)
    file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/ui17-json-include.txt" CONTENT "$<JOIN:$<TARGET_PROPERTY:scanner_json,INTERFACE_INCLUDE_DIRECTORIES>,;>")
    add_custom_target(guardian-winui
        COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File "${PROJECT_SOURCE_DIR}/tools/build_winui.ps1"
            -Configuration "$<CONFIG>" -BackendBuild "${CMAKE_BINARY_DIR}" -SkipBackend
        DEPENDS guardian_engine_client WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}" VERBATIM)
    file(GLOB ui_languages CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/config/languages/*.json")
    add_custom_command(OUTPUT "${CMAKE_BINARY_DIR}/generated/ui17-languages.cpp"
        COMMAND "${Python3_EXECUTABLE}" -B "${PROJECT_SOURCE_DIR}/tools/embed_languages.py" "${PROJECT_SOURCE_DIR}/config/languages" "${CMAKE_BINARY_DIR}/generated/ui17-languages.cpp"
        DEPENDS ${ui_languages} "${PROJECT_SOURCE_DIR}/tools/embed_languages.py" VERBATIM)
    if(BUILD_TESTING)
        add_executable(winui_backend_tests test/WinUiBackendTests.cpp src/winui/Backend.cpp src/winui/Localization.cpp "${CMAKE_BINARY_DIR}/generated/ui17-languages.cpp")
        target_include_directories(winui_backend_tests PRIVATE src src/winui/include)
        target_link_libraries(winui_backend_tests PRIVATE guardian_engine_client shell32 ole32 advapi32)
        target_compile_options(winui_backend_tests PRIVATE /W4 /WX /utf-8 /permissive-)
    endif()
endif()
