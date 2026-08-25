add_executable(wumpo
    main.cpp
)

target_link_libraries(wumpo
    PRIVATE wumpo::core wumpo::platform_desktop wumpo::warnings)

target_include_directories(wumpo PRIVATE "${CMAKE_SOURCE_DIR}/src")

set_target_properties(wumpo PROPERTIES FOLDER "wumpo")
