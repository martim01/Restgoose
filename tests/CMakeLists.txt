add_executable(restgoose_proxy_test
    proxy.cpp
)

target_link_libraries(restgoose_proxy_test
    PRIVATE
        restgoose
)

target_compile_features(restgoose_proxy_test
    PRIVATE
        cxx_std_17
)
