add_executable(vadd)
target_sources(vadd PRIVATE vadd-main.cpp vadd.cpp)
target_link_libraries(vadd PRIVATE task)
add_test(NAME vadd COMMAND vadd)