cmake_minimum_required(VERSION 3.5 FATAL_ERROR)
project(restgoose LANGUAGES CXX C)

SET(DIR_BASE $ENV{HOME} CACHE STRING "base location for libraries")
SET(DIR_JSON ${DIR_BASE}/jsoncpp CACHE STRING "location of jsoncpp")
SET(DIR_MONGOOSE ${DIR_BASE}/mongoose CACHE STRING "location of mongoose")
SET(DIR_LOG ${DIR_BASE}/log CACHE STRING "location of pml log")

#Get JsonCpp
find_path(TEMP_DIR NAMES "amalgamate.py" PATHS ${DIR_JSON})
if((NOT TEMP_DIR) OR (NOT EXISTS ${TEMP_DIR}))
	message("jsoncpp not found - clone from GitHub")
	execute_process(COMMAND git clone https://github.com/open-source-parsers/jsoncpp ${DIR_JSON})
endif()
unset(TEMP_DIR)

find_path(TEMP_DIR2 NAMES "amalgamate.py" PATHS ${DIR_JSON})
if((NOT TEMP_DIR2) OR (NOT EXISTS ${TEMP_DIR2}))
	message(FATAL_ERROR "Failed to clone jsoncpp")
endif()
unset(TEMP_DIR2)

#Amalgamate JsonCpp
find_path(TEMP_DIR3 NAMES "jsoncpp.cpp" PATHS ${DIR_JSON}/dist)
if((NOT TEMP_DIR3) OR (NOT EXISTS ${TEMP_DIR3}))
	message("amalgamate jsoncpp")
	execute_process(COMMAND python amalgamate.py WORKING_DIRECTORY ${DIR_JSON})
endif()
unset(TEMP_DIR3)

find_path(TEMP_DIR4 NAMES "jsoncpp.cpp" PATHS ${DIR_JSON}/dist)
if((NOT TEMP_DIR4) OR (NOT EXISTS ${TEMP_DIR4}))
	message(FATAL_ERROR "Failed to amalgamate jsoncpp")
endif()
unset(TEMP_DIR4)

#Get Mongoose
find_path(TEMP_DIR5 NAMES "mongoose.c" PATHS ${DIR_MONGOOSE})
if((NOT TEMP_DIR5) OR (NOT EXISTS ${TEMP_DIR5}))
	message("mongoose not found - clone from GitHub")
	execute_process(COMMAND git clone https://github.com/cesanta/mongoose/ ${DIR_MONGOOSE})
endif()
unset(TEMP_DIR5)

find_path(TEMP_DIR6 NAMES "mongoose.c" PATHS ${DIR_MONGOOSE})
if((NOT TEMP_DIR6) OR (NOT EXISTS ${TEMP_DIR6}))
	message(FATAL_ERROR "Failed to clone Mongoose")
endif()
unset(TEMP_DIR6)

#Get log
find_path(TEMP_DIR7 NAMES "CMakeLists.txt" PATHS ${DIR_LOG})
if((NOT TEMP_DIR7) OR (NOT EXISTS ${TEMP_DIR7}))
	message("log not found - clone from GitHub")
	execute_process(COMMAND git clone https://github.com/martim01/log/ ${DIR_LOG})
endif()
unset(TEMP_DIR7)

find_path(TEMP_DIR8 NAMES "CMakeLists.txt" PATHS ${DIR_LOG})
if((NOT TEMP_DIR8) OR (NOT EXISTS ${TEMP_DIR8}))
	message(FATAL_ERROR "Failed to clone pml::Log")
endif()
unset(TEMP_DIR8)


add_subdirectory(${DIR_LOG} ${DIR_LOG}/build)

#Now restgoose
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
link_directories(${CMAKE_SOURCE_DIR}/lib)
include_directories(${DIR_MONGOOSE})
include_directories(${DIR_JSON}/dist)
include_directories(${DIR_LOG}/include)
include_directories("include")


SET(SOURCES "src/RestGoose.cpp" "src/mongooseserver.cpp" "src/utils.cpp" "src/websocketclient.cpp" "src/websocketclientimpl.cpp" "${DIR_MONGOOSE}/mongoose.c" "${DIR_JSON}/dist/jsoncpp.cpp")

add_library(restgoose SHARED ${SOURCES})
set_target_properties(restgoose PROPERTIES DEBUG_POSTFIX "d")

list(APPEND flags "-fPIC" "-Wall" "-fpermissive"  "-std=c++14")

if(CMAKE_BUILD_TYPE MATCHES Release)
   list(APPEND flags "-O3")
   target_compile_definitions(restgoose PUBLIC NDEBUG DLL_EXPORTS _MSL_STDINT_H)
else()
   list(APPEND flags "-g")
   target_compile_definitions(restgoose PUBLIC DEBUG DLL_EXPORTS _MSL_STDINT_H)
endif()

target_compile_options(restgoose PRIVATE ${flags})
target_link_libraries(restgoose pml_log)
set_target_properties(restgoose PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/lib/)
