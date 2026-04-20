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
