cmake_minimum_required(VERSION 3.5 FATAL_ERROR)
project(restgoose LANGUAGES CXX C VERSION 3.4.0)

message(STATUS "----pml/restgoose ----")

set(CMAKE_INSTALL_PREFIX "/usr/local")


set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

message(STATUS "Restgoose Version: ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}")

execute_process(COMMAND ${CMAKE_COMMAND} -DNAMESPACE=restgoose -DMAJOR=${PROJECT_VERSION_MAJOR} -DMINOR=${PROJECT_VERSION_MINOR} -DPATCH=${PROJECT_VERSION_PATCH} -P ${CMAKE_CURRENT_SOURCE_DIR}/version.cmake)

SET(DIR_BASE $ENV{HOME} CACHE STRING "base location for libraries")
SET(DIR_JSON ${DIR_BASE}/jsoncpp CACHE STRING "location of jsoncpp")
SET(DIR_MONGOOSE ${DIR_BASE}/mongoose CACHE STRING "location of mongoose")
SET(DIR_LOG ${DIR_BASE}/log CACHE STRING "location of pml log")
SET(MONGOOSE_CHECKOUT "7.14" CACHE STRING "Mongoose release 7.14")

message(STATUS, ${CMAKE_SOURCE_DIR})

#check openssl installed
find_package(OpenSSL REQUIRED)

#Get JsonCpp
if(NOT DEFINED BUILD_JSONCPP)
	SET(BUILD_JSONCPP ON)
	find_path(RG_TEMP_DIR NAMES "amalgamate.py" PATHS ${DIR_JSON} NO_CACHE)
	if((NOT RG_TEMP_DIR) OR (NOT EXISTS ${RG_TEMP_DIR}))
		message("jsoncpp not found - clone from GitHub")
		execute_process(COMMAND git clone https://github.com/open-source-parsers/jsoncpp ${DIR_JSON})

		find_path(RG_TEMP_DIR2 NAMES "amalgamate.py" PATHS ${DIR_JSON} NO_CACHE)
		if((NOT RG_TEMP_DIR2) OR (NOT EXISTS ${RG_TEMP_DIR2}))
			message(FATAL_ERROR "Failed to clone jsoncpp")
		endif()
		unset(RG_TEMP_DIR2)
	else()
		message(STATUS "jsoncpp found - update to latest version")
		execute_process(COMMAND git -C ${DIR_JSON} pull --no-rebase)
	endif()
	unset(RG_TEMP_DIR)


	#Amalgamate JsonCpp
	find_path(RG_TEMP_DIR3 NAMES "jsoncpp.cpp" PATHS ${DIR_JSON}/dist NO_CACHE)
	if((NOT RG_TEMP_DIR3) OR (NOT EXISTS ${RG_TEMP_DIR3}))
		message("amalgamate jsoncpp")
		execute_process(COMMAND python3 amalgamate.py WORKING_DIRECTORY ${DIR_JSON})
	endif()
	unset(RG_TEMP_DIR3)

	find_path(RG_TEMP_DIR4 NAMES "jsoncpp.cpp" PATHS ${DIR_JSON}/dist NO_CACHE)
	if((NOT RG_TEMP_DIR4) OR (NOT EXISTS ${RG_TEMP_DIR4}))
		message(FATAL_ERROR "Failed to amalgamate jsoncpp")
	endif()
	unset(RG_TEMP_DIR4)
endif()

#Get Mongoose
find_path(RG_TEMP_DIR5 NAMES "mongoose.c" PATHS ${DIR_MONGOOSE} NO_CACHE)
if((NOT RG_TEMP_DIR5) OR (NOT EXISTS ${RG_TEMP_DIR5}))
	message(STATUS "mongoose not found - clone from GitHub")
	execute_process(COMMAND git clone https://github.com/cesanta/mongoose/ ${DIR_MONGOOSE})

	find_path(RG_TEMP_DIR6 NAMES "mongoose.c" PATHS ${DIR_MONGOOSE} NO_CACHE)
	if((NOT RG_TEMP_DIR6) OR (NOT EXISTS ${RG_TEMP_DIR6}))
		message(FATAL_ERROR "Failed to clone Mongoose")
	endif()
endif()

execute_process(COMMAND git -C ${DIR_MONGOOSE} fetch --all --tags)
execute_process(COMMAND git -C ${DIR_MONGOOSE} rev-list -n 1 ${MONGOOSE_CHECKOUT} OUTPUT_VARIABLE MONGOOSE_COMMIT OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND git -C ${DIR_MONGOOSE} rev-parse HEAD OUTPUT_VARIABLE MONGOOSE_HEAD OUTPUT_STRIP_TRAILING_WHITESPACE)
message(STATUS "mongoose found - want ${MONGOOSE_CHECKOUT} which is commit ${MONGOOSE_COMMIT} HEAD is currently ${MONGOOSE_HEAD}")
if(${MONGOOSE_HEAD} STREQUAL ${MONGOOSE_COMMIT})
	message(STATUS "mongoose found - already version wanted")
else()
	message(STATUS "mongoose found - ${MONGOOSE_HEAD} pull and checkout version wanted: ${MONGOOSE_CHECKOUT} = ${MONGOOSE_COMMIT}")
	message(STATUS "git -C ${DIR_MONGOOSE} pull origin master")
	execute_process(COMMAND git -C ${DIR_MONGOOSE} pull origin master)
	message(STATUS "git -C ${DIR_MONGOOSE} checkout ${MONGOOSE_COMMIT}")
	execute_process(COMMAND git -C ${DIR_MONGOOSE} checkout ${MONGOOSE_COMMIT})
endif()

unset(RG_TEMP_DIR5)

unset(RG_TEMP_DIR6)

#Get log
if(NOT DEFINED BUILD_LOG)
	SET(BUILD_LOG ON)
	find_path(RG_TEMP_DIR7 NAMES "CMakeLists.txt" PATHS ${DIR_LOG} NO_CACHE)
	if((NOT RG_TEMP_DIR7) OR (NOT EXISTS ${RG_TEMP_DIR7}))
		message(STATUS "log not found - clone from GitHub")
		execute_process(COMMAND git clone https://github.com/martim01/log/ ${DIR_LOG})

		find_path(RG_TEMP_DIR8 NAMES "CMakeLists.txt" PATHS ${DIR_LOG} NO_CACHE)
		if((NOT RG_TEMP_DIR8) OR (NOT EXISTS ${RG_TEMP_DIR8}))
			message(FATAL_ERROR "Failed to clone pml::Log")
		endif()
	else()
		message(STATUS "pml::log found - update to latest version")
		execute_process(COMMAND git -C ${DIR_LOG} pull --no-rebase)
	endif()
	unset(RG_TEMP_DIR7)
	unset(RG_TEMP_DIR8)

	add_subdirectory(${DIR_LOG} ${CMAKE_SOURCE_DIR}/build/log)
else()
	message(STATUS "Restgoose - pml::log being build elsewhere")
endif()

#Now restgoose
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
link_directories(${CMAKE_SOURCE_DIR}/lib)
include_directories(${DIR_MONGOOSE})
include_directories(${DIR_JSON}/dist)
include_directories(${DIR_LOG}/include)
include_directories("include")


SET(SOURCES "src/RestGoose.cpp" "src/mongooseserver.cpp" "src/utils.cpp" "src/websocketclient.cpp" "src/websocketclientimpl.cpp"
 "src/httpclientimpl.cpp" "src/httpclient.cpp" "src/response.cpp" "src/threadpool.cpp" ${CMAKE_BINARY_DIR}/src/restgoose_version.cpp
 "${DIR_MONGOOSE}/mongoose.c" "${DIR_JSON}/dist/jsoncpp.cpp")

add_library(restgoose SHARED ${SOURCES})
target_include_directories(restgoose PUBLIC ${CMAKE_BINARY_DIR}/include)
set_target_properties(restgoose PROPERTIES DEBUG_POSTFIX "d")

list(APPEND flags "-fPIC" "-Wall" "-fpermissive"  "-std=c++17")

if(CMAKE_BUILD_TYPE MATCHES Debug)
   message(STATUS "Debug build")
   list(APPEND flags "-g")
   target_compile_definitions(restgoose PUBLIC DEBUG DLL_EXPORTS _MSL_STDINT_H MG_ENABLE_OPENSSL)
else()
   message(STATUS "Release build")
   list(APPEND flags "-O3" "-s")
   target_compile_definitions(restgoose PUBLIC NDEBUG DLL_EXPORTS _MSL_STDINT_H MG_ENABLE_OPENSSL)

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
