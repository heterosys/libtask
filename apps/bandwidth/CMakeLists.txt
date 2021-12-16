add_executable(bandwidth)
target_sources(bandwidth PRIVATE bandwidth-main.cpp bandwidth.cpp)
target_link_libraries(bandwidth PUBLIC task)
add_test(NAME bandwidth COMMAND bandwidth)