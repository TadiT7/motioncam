#
# MotionCam static library
#

cmake_minimum_required(VERSION 3.4.1)

project(libmotioncam)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Paths
set(thirdparty-libs
        ${PROJECT_SOURCE_DIR}/thirdparty)

set(libmotioncam-src
        ${PROJECT_SOURCE_DIR}/libMotionCam)

#
# Halide libraries
#

add_library(fast_preview STATIC IMPORTED)
set_target_properties(fast_preview PROPERTIES IMPORTED_LOCATION
        ${libmotioncam-src}/halide/host/fast_preview.a)

add_library(deghost STATIC IMPORTED)
set_target_properties(deghost PROPERTIES IMPORTED_LOCATION
        ${libmotioncam-src}/halide/host/deghost.a)

add_library(build_bayer STATIC IMPORTED)
set_target_properties(build_bayer PROPERTIES IMPORTED_LOCATION
        ${libmotioncam-src}/halide/host/build_bayer.a)

add_library(camera_preview2_raw10 STATIC IMPORTED)
set_target_properties(camera_preview2_raw10 PROPERTIES IMPORTED_LOCATION
        ${libmotioncam-src}/halide/host/camera_preview2_raw10.a)

add_library(camera_preview3_raw10 STATIC IMPORTED)
set_target_properties(camera_preview3_raw10 PROPERTIES IMPORTED_LOCATION
        ${libmotioncam-src}/halide/host/camera_preview3_raw10.a)

add_library(camera_preview4_raw10 STATIC IMPORTED)
set_target_properties(camera_preview4_raw10 PROPERTIES IMPORTED_LOCATION
        ${libmotioncam-src}/halide/host/camera_preview4_raw10.a)

add_library(camera_preview2_raw16 STATIC IMPORTED)
set_target_properties(camera_preview2_raw16 PROPERTIES IMPORTED_LOCATION
        ${libmotioncam-src}/halide/host/camera_preview2_raw16.a)

add_library(camera_preview3_raw16 STATIC IMPORTED)
set_target_properties(camera_preview3_raw16 PROPERTIES IMPORTED_LOCATION
        ${libmotioncam-src}/halide/host/camera_preview3_raw16.a)

add_library(camera_preview4_raw16 STATIC IMPORTED)
set_target_properties(camera_preview4_raw16 PROPERTIES IMPORTED_LOCATION
        ${libmotioncam-src}/halide/host/camera_preview4_raw16.a)

add_library(hdr_mask STATIC IMPORTED)
set_target_properties(hdr_mask PROPERTIES IMPORTED_LOCATION
        ${libmotioncam-src}/halide/host/hdr_mask.a)

add_library(linear_image STATIC IMPORTED)
set_target_properties(linear_image PROPERTIES IMPORTED_LOCATION
        ${libmotioncam-src}/halide/host/linear_image.a)

add_library(generate_edges STATIC IMPORTED)
set_target_properties(generate_edges PROPERTIES IMPORTED_LOCATION
        ${libmotioncam-src}/halide/host/generate_edges.a)

add_library(measure_image STATIC IMPORTED)
set_target_properties(measure_image PROPERTIES IMPORTED_LOCATION
        ${libmotioncam-src}/halide/host/measure_image.a)

add_library(deinterleave_raw STATIC IMPORTED)
set_target_properties(deinterleave_raw PROPERTIES IMPORTED_LOCATION
        ${libmotioncam-src}/halide/host/deinterleave_raw.a)

#

add_library(preview_portrait2 STATIC IMPORTED)
set_target_properties(preview_portrait2 PROPERTIES IMPORTED_LOCATION
        ${libmotioncam-src}/halide/host/preview_portrait2.a)

add_library(preview_landscape2 STATIC IMPORTED)
set_target_properties(preview_landscape2 PROPERTIES IMPORTED_LOCATION
        ${libmotioncam-src}/halide/host/preview_landscape2.a)

add_library(preview_reverse_portrait2 STATIC IMPORTED)
set_target_properties(preview_reverse_portrait2 PROPERTIES IMPORTED_LOCATION
        ${libmotioncam-src}/halide/host/preview_reverse_portrait2.a)

add_library(preview_reverse_landscape2 STATIC IMPORTED)
set_target_properties(preview_reverse_landscape2 PROPERTIES IMPORTED_LOCATION
        ${libmotioncam-src}/halide/host/preview_reverse_landscape2.a)

#

add_library(preview_portrait4 STATIC IMPORTED)
set_target_properties(preview_portrait4 PROPERTIES IMPORTED_LOCATION
        ${libmotioncam-src}/halide/host/preview_portrait4.a)

add_library(preview_landscape4 STATIC IMPORTED)
set_target_properties(preview_landscape4 PROPERTIES IMPORTED_LOCATION
        ${libmotioncam-src}/halide/host/preview_landscape4.a)

add_library(preview_reverse_portrait4 STATIC IMPORTED)
set_target_properties(preview_reverse_portrait4 PROPERTIES IMPORTED_LOCATION
        ${libmotioncam-src}/halide/host/preview_reverse_portrait4.a)

add_library(preview_reverse_landscape4 STATIC IMPORTED)
set_target_properties(preview_reverse_landscape4 PROPERTIES IMPORTED_LOCATION
        ${libmotioncam-src}/halide/host/preview_reverse_landscape4.a)

#

add_library(preview_portrait8 STATIC IMPORTED)
set_target_properties(preview_portrait8 PROPERTIES IMPORTED_LOCATION
        ${libmotioncam-src}/halide/host/preview_portrait8.a)

add_library(preview_landscape8 STATIC IMPORTED)
set_target_properties(preview_landscape8 PROPERTIES IMPORTED_LOCATION
        ${libmotioncam-src}/halide/host/preview_landscape8.a)

add_library(preview_reverse_portrait8 STATIC IMPORTED)
set_target_properties(preview_reverse_portrait8 PROPERTIES IMPORTED_LOCATION
        ${libmotioncam-src}/halide/host/preview_reverse_portrait8.a)

add_library(preview_reverse_landscape8 STATIC IMPORTED)
set_target_properties(preview_reverse_landscape8 PROPERTIES IMPORTED_LOCATION
        ${libmotioncam-src}/halide/host/preview_reverse_landscape8.a)

#

add_library(postprocess STATIC IMPORTED)
set_target_properties(postprocess PROPERTIES IMPORTED_LOCATION
        ${libmotioncam-src}/halide/host/postprocess.a)

add_library(measure_noise STATIC IMPORTED)
set_target_properties(measure_noise PROPERTIES IMPORTED_LOCATION
        ${libmotioncam-src}/halide/host/measure_noise.a)

add_library(fuse_denoise_5x5 STATIC IMPORTED)
set_target_properties(fuse_denoise_5x5 PROPERTIES IMPORTED_LOCATION
        ${libmotioncam-src}/halide/host/fuse_denoise_5x5.a)

add_library(fuse_denoise_7x7 STATIC IMPORTED)
set_target_properties(fuse_denoise_7x7 PROPERTIES IMPORTED_LOCATION
        ${libmotioncam-src}/halide/host/fuse_denoise_7x7.a)

add_library(fuse_denoise_11x11 STATIC IMPORTED)
set_target_properties(fuse_denoise_11x11 PROPERTIES IMPORTED_LOCATION
        ${libmotioncam-src}/halide/host/fuse_denoise_11x11.a)

add_library(forward_transform STATIC IMPORTED)
set_target_properties(forward_transform PROPERTIES IMPORTED_LOCATION
        ${libmotioncam-src}/halide/host/forward_transform.a)

add_library(fuse_image STATIC IMPORTED)
set_target_properties(fuse_image PROPERTIES IMPORTED_LOCATION
        ${libmotioncam-src}/halide/host/fuse_image.a)

add_library(inverse_transform STATIC IMPORTED)
set_target_properties(inverse_transform PROPERTIES IMPORTED_LOCATION
        ${libmotioncam-src}/halide/host/inverse_transform.a)

add_library(halide_runtime_host STATIC IMPORTED)
set_target_properties(halide_runtime_host PROPERTIES IMPORTED_LOCATION
        ${libmotioncam-src}/halide/host/halide_runtime_host.a)

#
# Processing library
#

add_library( # Sets the name of the library.
        motioncam-static

        # Sets the library as a static library.
        STATIC

        # DNG files
        ${thirdparty-libs}/dng_sdk/dng/dng_1d_function.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_1d_table.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_abort_sniffer.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_area_task.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_bad_pixels.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_bottlenecks.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_camera_profile.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_color_space.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_color_spec.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_date_time.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_exceptions.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_exif.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_file_stream.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_filter_task.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_fingerprint.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_gain_map.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_globals.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_host.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_hue_sat_map.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_ifd.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_image.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_image_writer.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_info.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_iptc.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_jpeg_image.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_jpeg_memory_source.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_lens_correction.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_linearization_info.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_lossless_jpeg.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_matrix.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_memory.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_memory_stream.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_misc_opcodes.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_mosaic_info.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_mutex.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_negative.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_opcode_list.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_opcodes.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_orientation.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_parse_utils.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_pixel_buffer.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_point.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_preview.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_pthread.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_rational.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_read_image.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_rect.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_ref_counted_block.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_reference.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_render.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_resample.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_safe_arithmetic.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_shared.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_simple_image.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_spline.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_stream.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_string.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_string_list.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_tag_types.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_temperature.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_tile_iterator.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_tone_curve.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_utils.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_validate.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_xmp.cpp
        ${thirdparty-libs}/dng_sdk/dng/dng_xy_coord.cpp

        # Source files
        ${thirdparty-libs}/json11/json11/json11.cpp
        ${thirdparty-libs}/miniz/miniz.c
        ${thirdparty-libs}/miniz/miniz_zip.c
        ${thirdparty-libs}/miniz/miniz_tinfl.c
        ${thirdparty-libs}/miniz/miniz_tdef.c
        ${libmotioncam-src}/source/CameraProfile.cpp
        ${libmotioncam-src}/source/Color.cpp
        ${libmotioncam-src}/source/ImageOps.cpp
        ${libmotioncam-src}/source/ImageProcessor.cpp
        ${libmotioncam-src}/source/CameraPreview.cpp
        ${libmotioncam-src}/source/Logger.cpp
        ${libmotioncam-src}/source/Measure.cpp
        ${libmotioncam-src}/source/RawBufferManager.cpp
        ${libmotioncam-src}/source/RawBufferStreamer.cpp
        ${libmotioncam-src}/source/MotionCam.cpp
        ${libmotioncam-src}/source/RawContainer.cpp
        ${libmotioncam-src}/source/Temperature.cpp
        ${libmotioncam-src}/source/Settings.cpp
        ${libmotioncam-src}/source/Util.cpp)

# Include directories
target_include_directories(motioncam-static PRIVATE
        ${libmotioncam-src}/halide/host
        ${thirdparty-libs}/json11
        ${thirdparty-libs}/miniz
        ${thirdparty-libs}/halide/include
        ${thirdparty-libs}/queue
        ${thirdparty-libs}/atomic_queue
        ${thirdparty-libs}/dng_sdk)

target_include_directories(motioncam-static PUBLIC
        ${libmotioncam-src}/include)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DqDNGThreadSafe")

target_link_libraries(motioncam-static
        deghost
        fast_preview
        build_bayer
        hdr_mask
        linear_image
        generate_edges
        measure_image
        deinterleave_raw
        preview_portrait2
        preview_reverse_portrait2
        preview_landscape2
        preview_reverse_landscape2
        preview_portrait4
        preview_reverse_portrait4
        preview_landscape4
        preview_reverse_landscape4
        preview_portrait8
        preview_reverse_portrait8
        preview_landscape8
        preview_reverse_landscape8
        postprocess
        measure_noise
        fuse_denoise_5x5
        fuse_denoise_7x7
        fuse_denoise_11x11
        forward_transform
        fuse_image
        inverse_transform
        halide_runtime_host

        dl
        z
        exiv2
        zstd
        tbb
        opencv_core
        opencv_imgcodecs
        opencv_imgproc
        opencv_objdetect
        opencv_video
        opencv_highgui
        opencv_xfeatures2d
        opencv_features2d
        opencv_calib3d
)
