function(add_xtodo_test name)
    add_executable(${name} ${ARGN})
    target_include_directories(${name} PRIVATE ../src .)
    target_compile_features(${name} PRIVATE cxx_std_17)

    if(MSVC)
        target_compile_options(${name} PRIVATE /utf-8 /W4 /permissive-)
    else()
        target_compile_options(${name} PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    add_test(NAME ${name} COMMAND ${name})
endfunction()

add_xtodo_test(xtodo_model_core_tests
    todo_model_core_tests.cpp
    ../src/TodoModel.cpp
)

add_xtodo_test(xtodo_model_tree_tests
    todo_model_tree_tests.cpp
    ../src/TodoModel.cpp
)

add_xtodo_test(xtodo_model_list_tests
    todo_model_list_tests.cpp
    ../src/TodoModel.cpp
)

add_xtodo_test(xtodo_model_regression_tests
    todo_model_regression_tests.cpp
    ../src/TodoModel.cpp
)

add_xtodo_test(xtodo_store_format_tests
    store_format_tests.cpp
    ../src/StoreFormat.cpp
    ../src/CalendarModel.cpp
    ../src/TodoModel.cpp
)

add_xtodo_test(xtodo_theme_tests
    theme_tests.cpp
    ../src/Theme.cpp
    ../src/ThemeCatalog.cpp
)
if(WIN32)
    target_link_libraries(xtodo_theme_tests PRIVATE advapi32 user32)
endif()

add_xtodo_test(xtodo_i18n_tests
    i18n_tests.cpp
    ../src/I18n.cpp
)

add_xtodo_test(xtodo_gui_contract_tests
    gui_contract_tests.cpp
    ../src/EditIntent.cpp
    ../src/CalendarLayout.cpp
    ../src/CalendarModel.cpp
    ../src/GeometryPolicy.cpp
    ../src/MenuModel.cpp
    ../src/ViewLayout.cpp
    ../src/WindowHitTest.cpp
    ../src/I18n.cpp
)
