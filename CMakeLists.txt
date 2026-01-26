cmake_minimum_required(VERSION 3.20 FATAL_ERROR)
include(addexternal.cmake)
project(restgoose LANGUAGES CXX C VERSION 3.6.0)


message(STATUS "----pml/restgoose ----")

set(CMAKE_INSTALL_PREFIX "/usr/local")

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

message(STATUS "Restgoose Version: ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}")

execute_process(COMMAND ${CMAKE_COMMAND} -DNAMESPACE=${PROJECT_NAME} -DMAJOR=${PROJECT_VERSION_MAJOR} -DMINOR=${PROJECT_VERSION_MINOR} -DPATCH=${PROJECT_VERSION_PATCH} -DOUTPUT=${CMAKE_BINARY_DIR} -P ${CMAKE_SOURCE_DIR}/version.cmake)

SET(DIR_BASE ${CMAKE_SOURCE_DIR} CACHE STRING "base location for external libraries' source")
SET(DIR_JSON ${DIR_BASE}/external/jsoncpp CACHE STRING "location of jsoncpp")
SET(DIR_MONGOOSE ${DIR_BASE}/external/mongoose CACHE STRING "location of mongoose")
SET(DIR_LOG ${DIR_BASE}/external/log CACHE STRING "location of pml log")
SET(MONGOOSE_CHECKOUT "7.18" CACHE STRING "Mongoose release 7.18")

message(STATUS "Source Directory: ${CMAKE_SOURCE_DIR}")

#check openssl installed
message(STATUS "Check OpenSSL")
find_package(OpenSSL REQUIRED)

message(STATUS "Get external repositories")
add_external_library(jsoncpp ${DIR_JSON} "https://github.com/open-source-parsers/jsoncpp" "" FALSE)
if(EXISTS ${DIR_JSON}/amalgamate.py)
	message(STATUS "Generating jsoncpp amalgamated source files")
	execute_process(COMMAND python3 amalgamate.py WORKING_DIRECTORY ${DIR_JSON})
endif()

add_external_library(log ${DIR_LOG} "https://github.com/martim01/log.git" "" FALSE)
add_external_library(mongoose ${DIR_MONGOOSE} "https://github.com/cesanta/mongoose.git" ${MONGOOSE_CHECKOUT} FALSE)


#Now restgoose
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
link_directories(${CMAKE_SOURCE_DIR}/lib)
include_directories(${DIR_MONGOOSE})
include_directories(${DIR_JSON}/include)
include_directories(${DIR_LOG}/include)
include_directories("include")


SET(SOURCES "src/RestGoose.cpp" "src/mongooseserver.cpp" "src/utils.cpp" "src/websocketclient.cpp" "src/websocketclientimpl.cpp"
 "src/httpclientimpl.cpp" "src/httpclient.cpp" "src/response.cpp" "src/threadpool.cpp" ${CMAKE_BINARY_DIR}/src/restgoose_version.cpp
 "${DIR_MONGOOSE}/mongoose.c")

add_library(restgoose SHARED ${SOURCES})
target_include_directories(restgoose PUBLIC ${CMAKE_BINARY_DIR}/include)
set_target_properties(restgoose PROPERTIES DEBUG_POSTFIX "d")

set(CMAKE_CXX_STANDARD 17)

list(APPEND flags "-fPIC" "-Wall" "-fpermissive")

if(CMAKE_BUILD_TYPE MATCHES Debug)
   message(STATUS "Debug build")
   list(APPEND flags "-g")
   target_compile_definitions(restgoose PUBLIC DEBUG DLL_EXPORTS _MSL_STDINT_H MG_TLS=MG_TLS_OPENSSL)
else()
   message(STATUS "Release build")
   list(APPEND flags "-O3" "-s")
   target_compile_definitions(restgoose PUBLIC NDEBUG DLL_EXPORTS _MSL_STDINT_H MG_TLS=MG_TLS_OPENSSL)

endif()

target_compile_options(restgoose PRIVATE ${flags})
target_link_libraries(restgoose ssl)
target_link_libraries(restgoose optimized pml_log)

set_target_properties(restgoose PROPERTIES DEBUG_POSTFIX "d")
set_target_properties(restgoose PROPERTIES VERSION ${PROJECT_VERSION} SOVERSION ${PROJECT_VERSION_MAJOR})
set_target_properties(restgoose PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/lib/)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux") 
    install(TARGETS restgoose LIBRARY DESTINATION lib)
endif()
