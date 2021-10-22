#
# MotionCam convert utility
#

cmake_minimum_required(VERSION 3.4.1)

project(convert)

# Homebrew
if (APPLE)
        LINK_DIRECTORIES(/usr/local/lib)
endif (APPLE)

set(libmotioncam-src ${PROJECT_SOURCE_DIR}/../../libMotionCam)
add_subdirectory(${libmotioncam-src} libmotioncam)

set(src ${PROJECT_SOURCE_DIR}/source)

add_executable (convert
        ${src}/main.cpp)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(convert PRIVATE Threads::Threads)

if (APPLE)
        target_link_libraries(convert PRIVATE
                "-framework CoreServices")
endif (APPLE)

target_link_libraries(convert PRIVATE        
        motioncam-static)
