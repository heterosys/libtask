add_executable(nested-vadd)
target_sources(nested-vadd PRIVATE vadd-main.cpp vadd.cpp)
target_link_libraries(nested-vadd PRIVATE task)
add_test(NAME nested-vadd COMMAND nested-vadd)