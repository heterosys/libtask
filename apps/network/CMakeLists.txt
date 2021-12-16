add_executable(network)
target_sources(network PRIVATE network-main.cpp network.cpp)
target_link_libraries(network PRIVATE task)
add_test(NAME network COMMAND network)