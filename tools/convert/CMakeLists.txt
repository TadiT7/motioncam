#
# MotionCam convert utility
#

cmake_minimum_required(VERSION 3.4.1)

project(convert)

set(libmotioncam-src ${PROJECT_SOURCE_DIR}/../../libMotionCam)
add_subdirectory(${libmotioncam-src} libmotioncam)

set(src ${PROJECT_SOURCE_DIR}/source)

add_executable (convert
        ${src}/main.cpp)

find_library( zstd zstd )
find_library( tbb tbb )
find_library( exiv2 exiv2 )
find_library( libz z )
find_library( opencv_core opencv_core )
find_library( opencv_imgcodecs opencv_imgcodecs )
find_library( opencv_imgproc opencv_imgproc )
find_library( opencv_objdetect opencv_objdetect )
find_library( opencv_video opencv_video )

if (APPLE)
            target_link_libraries(convert
                        "-framework CoreServices")        
endif (APPLE)

target_link_libraries(convert
        ${zstd}
        ${tbb}
        ${exiv2}
        ${libz}
        ${opencv_core}
        ${opencv_imgcodecs}
        ${opencv_imgproc}
        ${opencv_objdetect}
        ${opencv_video}
        motioncam-static)
