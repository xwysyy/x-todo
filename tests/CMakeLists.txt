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

add_xtodo_test(xtodo_model_property_tests
    todo_model_property_tests.cpp
    ../src/TodoModel.cpp
)

add_xtodo_test(xtodo_store_format_tests
    store_format_tests.cpp
    ../src/StoreFormat.cpp
    ../src/CalendarModel.cpp
    ../src/TodoModel.cpp
)
target_include_directories(xtodo_store_format_tests PRIVATE ../third_party)

add_xtodo_test(xtodo_store_format_hardening_tests
    store_format_hardening_tests.cpp
    ../src/StoreFormat.cpp
    ../src/CalendarModel.cpp
    ../src/TodoModel.cpp
)
target_include_directories(xtodo_store_format_hardening_tests PRIVATE ../third_party)

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

add_xtodo_test(xtodo_launch_command_tests
    launch_command_tests.cpp
    ../src/LaunchCommand.cpp
    ../src/TaskSchedulerReminder.cpp
    ../src/CalendarDate.cpp
)
if(WIN32)
    target_link_libraries(xtodo_launch_command_tests PRIVATE taskschd comsuppw ole32)
endif()

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

add_xtodo_test(xtodo_calendar_date_tests
    calendar_date_tests.cpp
    ../src/CalendarDate.cpp
)

add_xtodo_test(xtodo_calendar_date_property_tests
    calendar_date_property_tests.cpp
    ../src/CalendarDate.cpp
)

add_xtodo_test(xtodo_calendar_model_property_tests
    calendar_model_property_tests.cpp
    ../src/CalendarModel.cpp
)

add_xtodo_test(xtodo_calendar_layout_tests
    calendar_layout_tests.cpp
    ../src/CalendarLayout.cpp
    ../src/CalendarModel.cpp
)

add_xtodo_test(xtodo_calendar_layout_property_tests
    calendar_layout_property_tests.cpp
    ../src/CalendarLayout.cpp
    ../src/CalendarModel.cpp
)

add_xtodo_test(xtodo_reminder_service_tests
    reminder_service_tests.cpp
    ../src/ReminderService.cpp
    ../src/CalendarDate.cpp
    ../src/CalendarModel.cpp
)

add_xtodo_test(xtodo_reminder_format_tests
    reminder_format_tests.cpp
    ../src/StoreFormat.cpp
    ../src/CalendarModel.cpp
    ../src/TodoModel.cpp
)
target_include_directories(xtodo_reminder_format_tests PRIVATE ../third_party)

add_xtodo_test(xtodo_reminder_text_tests
    reminder_text_tests.cpp
    ../src/ReminderText.cpp
    ../src/I18n.cpp
)

add_xtodo_test(xtodo_reminder_popup_policy_tests
    reminder_popup_policy_tests.cpp
)

add_xtodo_test(xtodo_reminder_visual_policy_tests
    reminder_visual_policy_tests.cpp
)

add_xtodo_test(xtodo_reminder_scheduler_tests
    reminder_scheduler_tests.cpp
    ../src/TaskSchedulerReminder.cpp
    ../src/CalendarDate.cpp
)
if(WIN32)
    target_link_libraries(xtodo_reminder_scheduler_tests PRIVATE taskschd comsuppw ole32)
endif()

add_xtodo_test(xtodo_reminder_scheduler_policy_tests
    reminder_scheduler_policy_tests.cpp
)

add_xtodo_test(xtodo_reminder_timer_policy_tests
    reminder_timer_policy_tests.cpp
)

add_xtodo_test(xtodo_reminder_settings_policy_tests
    reminder_settings_policy_tests.cpp
)

add_xtodo_test(xtodo_reminder_dispatch_policy_tests
    reminder_dispatch_policy_tests.cpp
)

add_xtodo_test(xtodo_windows_notifier_tests
    windows_notifier_tests.cpp
    ../src/WindowsNotifier.cpp
    ../src/CalendarDate.cpp
)
if(WIN32)
    target_link_libraries(xtodo_windows_notifier_tests PRIVATE shell32 ole32 runtimeobject propsys)
endif()
