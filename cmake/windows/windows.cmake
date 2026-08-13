set(PLATFORM_SOURCES 3rdparty/WinCommander.cpp src/sys/windows/guihelper.cpp src/sys/windows/MiniDump.cpp src/sys/windows/eventHandler.cpp src/sys/windows/WinVersion.cpp src/sys/windows/AutoRun.cpp src/sys/windows/UrlScheme.cpp)
set(PLATFORM_LIBRARIES wininet wsock32 ws2_32 user32 rasapi32 iphlpapi ntdll wbemuuid psapi shell32)

include(cmake/windows/generate_product_version.cmake)
set(THRONED_RESOURCE_VERSION "$ENV{INPUT_VERSION}")
string(REGEX MATCH "^([0-9]+)\\.([0-9]+)\\.([0-9]+)" THRONED_RESOURCE_VERSION_MATCH "${THRONED_RESOURCE_VERSION}")
if (THRONED_RESOURCE_VERSION_MATCH)
    set(THRONED_VERSION_MAJOR "${CMAKE_MATCH_1}")
    set(THRONED_VERSION_MINOR "${CMAKE_MATCH_2}")
    set(THRONED_VERSION_PATCH "${CMAKE_MATCH_3}")
else ()
    set(THRONED_VERSION_MAJOR 0)
    set(THRONED_VERSION_MINOR 0)
    set(THRONED_VERSION_PATCH 0)
endif ()
generate_product_version(
        QV2RAY_RC
        ICON "${CMAKE_SOURCE_DIR}/res/Throne.ico"
        NAME "Throned"
        BUNDLE "Throned"
        COMPANY_NAME "Throned"
        COMPANY_COPYRIGHT "Throned"
        FILE_DESCRIPTION "Throned"
        VERSION_MAJOR ${THRONED_VERSION_MAJOR}
        VERSION_MINOR ${THRONED_VERSION_MINOR}
        VERSION_PATCH ${THRONED_VERSION_PATCH}
)
add_definitions(-DUNICODE -D_UNICODE -DNOMINMAX)
set(GUI_TYPE WIN32)
if (MSVC)
    add_compile_options("/utf-8")
    add_definitions(-D_WIN32_WINNT=0x600 -D_SCL_SECURE_NO_WARNINGS -D_CRT_SECURE_NO_WARNINGS)
endif ()
