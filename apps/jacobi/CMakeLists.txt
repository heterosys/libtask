add_executable(jacobi)
target_sources(jacobi PRIVATE jacobi-main.cpp jacobi.cpp)
target_link_libraries(jacobi PRIVATE task)
add_test(NAME jacobi COMMAND jacobi)