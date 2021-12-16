add_executable(shared-vadd)
target_sources(shared-vadd PRIVATE vadd-main.cpp vadd.cpp)
target_link_libraries(shared-vadd PRIVATE task)
add_test(NAME shared-vadd COMMAND shared-vadd)