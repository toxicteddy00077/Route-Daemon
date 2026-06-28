# Minimal assert-based unit tests driven by CTest.
# Each test is a standalone executable that returns 0 on success, non-zero on failure.

add_executable(test_config test_config.c ${CMAKE_SOURCE_DIR}/src/log.c ${CMAKE_SOURCE_DIR}/src/config.c)
target_include_directories(test_config PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(test_config PRIVATE PkgConfig::JSONC)

add_test(NAME config COMMAND test_config)

add_executable(test_log test_log.c ${CMAKE_SOURCE_DIR}/src/log.c)
target_include_directories(test_log PRIVATE ${CMAKE_SOURCE_DIR}/include)

add_test(NAME log COMMAND test_log)
