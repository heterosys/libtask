add_executable(cannon)
target_sources(cannon PRIVATE cannon-main.cpp cannon.cpp)
target_link_libraries(cannon PRIVATE task)
add_test(NAME cannon COMMAND cannon)