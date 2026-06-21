cmake_minimum_required(VERSION 3.20)
cmake_policy(SET CMP0091 NEW) # 让 CMAKE_MSVC_RUNTIME_LIBRARY 生效（静态 CRT）
project(x-todo LANGUAGES CXX)

option(XTODO_BUILD_APP "Build the Windows desktop app" ON)
option(XTODO_BUILD_TESTS "Build unit tests" OFF)

if(NOT WIN32)
    set(XTODO_BUILD_APP OFF CACHE BOOL "Build the Windows desktop app" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 静态链接 MSVC 运行库：目标机无需安装 VC++ 运行时，单 exe 直接跑
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(XTODO_BUILD_APP)
    enable_language(RC)

    add_executable(x-todo WIN32
        src/main.cpp
        src/MainWindow.cpp
        src/MainWindowView.cpp
        src/CalendarLayout.cpp
        src/CalendarModel.cpp
        src/EditIntent.cpp
        src/GeometryPolicy.cpp
        src/MenuModel.cpp
        src/ViewLayout.cpp
        src/WindowHitTest.cpp
        src/TodoModel.cpp
        src/Store.cpp
        src/StoreFormat.cpp
        src/Autostart.cpp
        src/I18n.cpp
        src/Theme.cpp
        src/ThemeCatalog.cpp
        src/ThemeLoader.cpp
        src/ThemeManagerWindow.cpp
        resources/resource.rc
    )

    target_compile_definitions(x-todo PRIVATE
        UNICODE _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )

    # vendored 第三方头文件（nlohmann/json 单头，v3.11.3，MIT）
    target_include_directories(x-todo PRIVATE third_party)

    target_link_libraries(x-todo PRIVATE
        d2d1
        dwrite
        user32
        gdi32
        shell32
        ole32
        comctl32
        dwmapi
        advapi32
        shlwapi
    )

    if(MSVC)
        # /utf-8：源码内的中文字符串字面量按 UTF-8 解析，避免乱码
        target_compile_options(x-todo PRIVATE /utf-8 /W3)
        # /MANIFEST:NO：改用 resource.rc 里嵌入的 app.manifest（DPI 感知）
        # /OPT:REF /OPT:ICF：剔除未引用代码、合并相同函数，减小体积
        target_link_options(x-todo PRIVATE /MANIFEST:NO /OPT:REF /OPT:ICF)
    endif()
endif()

if(XTODO_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
