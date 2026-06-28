# Minimal assert-based unit tests driven by CTest.
# Each test is a standalone executable that returns 0 on success, non-zero on failure.

add_executable(test_config test_config.c)
target_link_libraries(test_config PRIVATE PkgConfig::JSONC)

add_test(NAME config COMMAND test_config)

add_executable(test_log test_log.c)
add_test(NAME log COMMAND test_log)
