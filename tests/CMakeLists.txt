add_executable(restgoose_client
    client.cpp ${DIR_JSON}/dist/jsoncpp.cpp
)

target_link_libraries(restgoose_client
    PRIVATE
        restgoose
        pml_log
)

target_compile_features(restgoose_client
    PRIVATE
        cxx_std_17
)

add_executable(restgoose_proxy_client proxy_client.cpp ${DIR_JSON}/dist/jsoncpp.cpp)
target_link_libraries(restgoose_proxy_client PRIVATE restgoose pml_log)
target_compile_features(restgoose_proxy_client PRIVATE cxx_std_17)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
enable_testing()
add_test(NAME restgoose_proxy_regression
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/proxy_regression.py"
            "$<TARGET_FILE:restgoose_proxy_client>")
set_tests_properties(restgoose_proxy_regression PROPERTIES TIMEOUT 120)

add_executable(restgoose_websocket_proxy_client websocket_proxy_client.cpp ${DIR_JSON}/dist/jsoncpp.cpp)
target_link_libraries(restgoose_websocket_proxy_client PRIVATE restgoose pml_log)
target_compile_features(restgoose_websocket_proxy_client PRIVATE cxx_std_17)
add_test(NAME restgoose_websocket_proxy_regression
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/websocket_proxy_regression.py"
            "$<TARGET_FILE:restgoose_websocket_proxy_client>")
set_tests_properties(restgoose_websocket_proxy_regression PROPERTIES TIMEOUT 120)
