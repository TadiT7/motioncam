#!/bin/bash
set -euxo pipefail

if [[ -z "${ANDROID_NDK-}" ]]; then
	echo -e "Please set ANDROID_NDK to point to the NDK installation path"
	exit 1
fi

if [[ -z "${LLVM_DIR-}" ]]; then
	echo -e "Please set LLVM_DIR to point to LLVM"
	exit 1
fi

NUM_CORES="$(python3 -c 'import multiprocessing as mp; print(mp.cpu_count())')"

ANDROID_ABI="arm64-v8a"
OPENCV_VERSION="4.5.4"
LIBEXPAT_VERSION="2.4.1"
LIBEXIV2_VERSION="0.27.4"
ZSTD_VERSION="v1.5.0"
DLIB_VERSION="19.22"
HALIDE_BRANCH=https://github.com/mirsadm/Halide
PFOR_BRANCH=https://github.com/mirsadm/TurboPFor-Integer-Compression.git # To remove -lrt flag for android cross-compilation

mkdir -p tmp
pushd tmp

build_opencv() {
	OPENCV_ARCHIVE="opencv-${OPENCV_VERSION}.zip"
	OPENCV_CONTRIB_ARCHIVE="opencv-contrib-${OPENCV_VERSION}.zip"

	curl -L https://github.com/opencv/opencv/archive/${OPENCV_VERSION}.zip --output ${OPENCV_ARCHIVE}
	curl -L https://github.com/opencv/opencv_contrib/archive/${OPENCV_VERSION}.zip --output ${OPENCV_CONTRIB_ARCHIVE}

	unzip ${OPENCV_ARCHIVE}
	unzip ${OPENCV_CONTRIB_ARCHIVE}

	pushd opencv-${OPENCV_VERSION}

	mkdir -p build
	pushd build

	cmake -DCMAKE_BUILD_TYPE=Release -DOPENCV_EXTRA_MODULES_PATH=../../opencv_contrib-${OPENCV_VERSION}/modules -DCMAKE_INSTALL_PREFIX=../build/${ANDROID_ABI} -DCMAKE_SYSTEM_NAME=Android 	\
		-DANDROID_NATIVE_API_LEVEL=21 -DCMAKE_SYSTEM_VERSION=21 -DANDROID_ABI=${ANDROID_ABI} -DCMAKE_ANDROID_ARCH_ABI=${ANDROID_ABI} -DANDROID_STL=c++_shared 								\
		-DCMAKE_ANDROID_NDK_TOOLCHAIN_VERSION=clang -DWITH_TBB=ON -DCPU_BASELINE=NEON -DENABLE_NEON=ON -DCMAKE_TOOLCHAIN_FILE=${ANDROID_NDK}/build/cmake/android.toolchain.cmake 			\
		-DWITH_OPENEXR=OFF \
		-DWITH_LAPACK=OFF \
		-DWITH_EIGEN=OFF \
		-DWITH_OPENCL=OFF \
		-DBUILD_ANDROID_EXAMPLES=OFF \
		-DBUILD_DOCS=OFF \
		-DBUILD_PERF_TESTS=OFF \
		-DBUILD_TESTS=OFF \
		-DBUILD_JAVA=OFF \
		-DBUILD_SHARED_LIBS=OFF \
		-DBUILD_opencv_calib3d=ON \
		-DBUILD_opencv_core=ON \
		-DBUILD_opencv_features2d=ON \
		-DBUILD_opencv_flann=ON \
		-DBUILD_opencv_imgcodecs=ON \
		-DBUILD_opencv_imgproc=ON \
		-DBUILD_opencv_objdetect=ON \
		-DBUILD_opencv_photo=ON \
		-DBUILD_opencv_video=ON \
		-DBUILD_opencv_xfeatures2d=ON \
		-DBUILD_opencv_ximgproc=ON \
		-DBUILD_opencv_alphamat=OFF \
		-DBUILD_opencv_apps=OFF \
		-DBUILD_opencv_aruco=OFF \
		-DBUILD_opencv_barcode=OFF \
		-DBUILD_opencv_bgsegm=OFF \
		-DBUILD_opencv_bioinspired=OFF \
		-DBUILD_opencv_ccalib=OFF \
		-DBUILD_opencv_datasets=OFF \
		-DBUILD_opencv_dnn=OFF \
		-DBUILD_opencv_dnn_objdetect=OFF \
		-DBUILD_opencv_dnn_superres=OFF \
		-DBUILD_opencv_dpm=OFF \
		-DBUILD_opencv_face=OFF \
		-DBUILD_opencv_freetype=OFF \
		-DBUILD_opencv_fuzzy=OFF \
		-DBUILD_opencv_gapi=OFF \
		-DBUILD_opencv_hdf=OFF \
		-DBUILD_opencv_hfs=OFF \
		-DBUILD_opencv_highgui=OFF \
		-DBUILD_opencv_img_hash=OFF \
		-DBUILD_opencv_intensity_transform=OFF \
		-DBUILD_opencv_java_bindings_generator=OFF \
		-DBUILD_opencv_js=OFF \
		-DBUILD_opencv_js_bindings_generator=OFF \
		-DBUILD_opencv_line_descriptor=OFF \
		-DBUILD_opencv_mcc=OFF \
		-DBUILD_opencv_ml=OFF \
		-DBUILD_opencv_objc_bindings_generator=OFF \
		-DBUILD_opencv_optflow=OFF \
		-DBUILD_opencv_phase_unwrapping=OFF \
		-DBUILD_opencv_plot=OFF \
		-DBUILD_opencv_python3=OFF \
		-DBUILD_opencv_python_bindings_generator=OFF \
		-DBUILD_opencv_python_tests=OFF \
		-DBUILD_opencv_quality=OFF \
		-DBUILD_opencv_rapid=OFF \
		-DBUILD_opencv_reg=OFF \
		-DBUILD_opencv_rgbd=OFF \
		-DBUILD_opencv_saliency=OFF \
		-DBUILD_opencv_sfm=OFF \
		-DBUILD_opencv_shape=OFF \
		-DBUILD_opencv_stereo=OFF \
		-DBUILD_opencv_stitching=OFF \
		-DBUILD_opencv_structured_light=OFF \
		-DBUILD_opencv_superres=OFF \
		-DBUILD_opencv_surface_matching=OFF \
		-DBUILD_opencv_text=OFF \
		-DBUILD_opencv_tracking=OFF \
		-DBUILD_opencv_videoio=OFF \
		-DBUILD_opencv_videostab=OFF \
		-DBUILD_opencv_wechat_qrcode=OFF \
		-DBUILD_opencv_world=OFF \
		-DBUILD_opencv_xobjdetect=OFF \
		-DBUILD_opencv_xphoto=OFF ..

	make -j${NUM_CORES}

	make install

	INSTALL_DIR="../../../libMotionCam/thirdparty/opencv"

	mkdir -p ${INSTALL_DIR}/libs
	mkdir -p ${INSTALL_DIR}/include
	mkdir -p ${INSTALL_DIR}/thirdparty

	cp -a ./lib/. ${INSTALL_DIR}/libs/.
	cp -a ./${ANDROID_ABI}/sdk/native/jni/include/. ${INSTALL_DIR}/include/.
	cp -a ./${ANDROID_ABI}/sdk/native/3rdparty/. ${INSTALL_DIR}/thirdparty/.

	popd # build
	popd # opencv-${OPENCV_VERSION}

	touch ".opencv-${OPENCV_VERSION}"
}

build_expat() {
	LIBEXPAT_ARCHIVE="libexpat-${LIBEXPAT_VERSION}.tar.gz"

	curl -L https://github.com/libexpat/libexpat/releases/download/R_2_4_1/expat-${LIBEXPAT_VERSION}.tar.gz --output ${LIBEXPAT_ARCHIVE}

	tar -xvf ${LIBEXPAT_ARCHIVE}

	pushd expat-${LIBEXPAT_VERSION}

	mkdir -p build
	pushd build

	cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=../build/${ANDROID_ABI} -DCMAKE_SYSTEM_NAME=Android 												\
		-DANDROID_NATIVE_API_LEVEL=21 -DCMAKE_SYSTEM_VERSION=21 -DANDROID_ABI=${ANDROID_ABI} -DCMAKE_ANDROID_ARCH_ABI=${ANDROID_ABI} -DANDROID_STL=c++_shared 	\
		-DEXPAT_BUILD_DOCS=OFF -DEXPAT_BUILD_EXAMPLES=OFF -DEXPAT_BUILD_TESTS=OFF -DEXPAT_BUILD_TOOLS=OFF -DEXPAT_SHARED_LIBS=OFF 								\
		-DCMAKE_ANDROID_NDK_TOOLCHAIN_VERSION=clang -DCMAKE_TOOLCHAIN_FILE=${ANDROID_NDK}/build/cmake/android.toolchain.cmake ..

	make -j${NUM_CORES}

	make install

	INSTALL_DIR="../../../libMotionCam/thirdparty/expat"

	mkdir -p ${INSTALL_DIR}/lib
	mkdir -p ${INSTALL_DIR}/include

	cp -a ./${ANDROID_ABI}/include/. ${INSTALL_DIR}/include/.
	cp -a ./${ANDROID_ABI}/lib/*.a ${INSTALL_DIR}/lib

	popd # build
	popd # expat-${LIBEXPAT_VERSION}

	touch ".expat-${LIBEXPAT_VERSION}"
}

build_exiv2() {
	LIBEXIV2_ARCHIVE="libexiv2-${LIBEXPAT_VERSION}.tar.gz"	

	curl -L https://www.exiv2.org/builds/exiv2-${LIBEXIV2_VERSION}-Source.tar.gz --output ${LIBEXIV2_ARCHIVE}

	tar -xvf ${LIBEXIV2_ARCHIVE}

	pushd exiv2-${LIBEXIV2_VERSION}-Source

	mkdir -p build
	pushd build

	cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=../build/${ANDROID_ABI} -DCMAKE_SYSTEM_NAME=Android 												\
		-DANDROID_NATIVE_API_LEVEL=21 -DCMAKE_SYSTEM_VERSION=21 -DANDROID_ABI=${ANDROID_ABI} -DCMAKE_ANDROID_ARCH_ABI=${ANDROID_ABI} -DANDROID_STL=c++_shared 	\
		-DEXIV2_BUILD_SAMPLES=OFF -DBUILD_SHARED_LIBS=OFF -DEXIV2_BUILD_EXIV2_COMMAND=OFF																		\
		-DEXPAT_LIBRARY=../../../libMotionCam/thirdparty/expat/lib/libexpat.a -DEXPAT_INCLUDE_DIR=../../../libMotionCam/thirdparty/expat/include 				\
		-DCMAKE_ANDROID_NDK_TOOLCHAIN_VERSION=clang -DCMAKE_TOOLCHAIN_FILE=${ANDROID_NDK}/build/cmake/android.toolchain.cmake ..

	make -j${NUM_CORES}

	make install

	INSTALL_DIR="../../../libMotionCam/thirdparty/exiv2"

	mkdir -p ${INSTALL_DIR}/lib
	mkdir -p ${INSTALL_DIR}/include

	cp -a ./${ANDROID_ABI}/include/. ${INSTALL_DIR}/include/.
	cp -a ./${ANDROID_ABI}/lib/*.a ${INSTALL_DIR}/lib/.

	popd # build
	popd # exiv2-${LIBEXIV2_VERSION}-Source

	touch ".exiv2-${LIBEXIV2_VERSION}"
}

build_zstd() {
	if [ ! -d "zstd-src" ]; then
		git clone https://github.com/facebook/zstd zstd-src
	fi

	pushd zstd-src

	git checkout ${ZSTD_VERSION}

	pushd build/cmake

	cmake  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=../output/${ANDROID_ABI} -DCMAKE_SYSTEM_NAME=Android 												\
		-DANDROID_NATIVE_API_LEVEL=21 -DCMAKE_SYSTEM_VERSION=21 -DANDROID_ABI=${ANDROID_ABI} -DCMAKE_ANDROID_ARCH_ABI=${ANDROID_ABI} -DANDROID_STL=c++_shared 	\
		-DZSTD_BUILD_STATIC=ON -DZSTD_BUILD_TESTS=OFF -DCMAKE_ANDROID_NDK_TOOLCHAIN_VERSION=clang -DCMAKE_TOOLCHAIN_FILE=${ANDROID_NDK}/build/cmake/android.toolchain.cmake .

	make -j${NUM_CORES}

	make install

	INSTALL_DIR="../../../../libMotionCam/thirdparty/zstd"

	mkdir -p ${INSTALL_DIR}/lib
	mkdir -p ${INSTALL_DIR}/include

	cp -a ../output/${ANDROID_ABI}/include/. ${INSTALL_DIR}/include/.
	cp -a ../output/${ANDROID_ABI}/lib/*.a ${INSTALL_DIR}/lib/.

	popd # /build/cmake
	popd # zstd-src

	touch ".zstd-${ZSTD_VERSION}"
}

build_dlib() {	
	DLIB_ARCHIVE="dlib-${DLIB_VERSION}.tar.bz2"	

	curl -L http://dlib.net/files/${DLIB_ARCHIVE} --output ${DLIB_ARCHIVE}

	tar -xvf ${DLIB_ARCHIVE}

	pushd dlib-${DLIB_VERSION}

	mkdir -p build
	pushd build

	cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=../build/${ANDROID_ABI} -DCMAKE_SYSTEM_NAME=Android 												\
		-DANDROID_NATIVE_API_LEVEL=21 -DCMAKE_SYSTEM_VERSION=21 -DANDROID_ABI=${ANDROID_ABI} -DCMAKE_ANDROID_ARCH_ABI=${ANDROID_ABI} -DANDROID_STL=c++_shared 	\
		-DBUILD_SHARED_LIBS=OFF -DDLIB_USE_CUDA=NO -DDLIB_USE_BLAS=NO -DDLIB_NO_GUI_SUPPORT=YES -DDLIB_LINK_WITH_SQLITE3=NO -DDLIB_PNG_SUPPORT=NO -DDLIB_JPEG_SUPPORT=NO \
		-DCMAKE_ANDROID_NDK_TOOLCHAIN_VERSION=clang -DCMAKE_TOOLCHAIN_FILE=${ANDROID_NDK}/build/cmake/android.toolchain.cmake ..

	make -j${NUM_CORES}

	make install

	INSTALL_DIR="../../../libMotionCam/thirdparty/dlib"

	mkdir -p ${INSTALL_DIR}/lib
	mkdir -p ${INSTALL_DIR}/include

	cp -a ./${ANDROID_ABI}/include/. ${INSTALL_DIR}/include/.
	cp -a ./${ANDROID_ABI}/lib/*.a ${INSTALL_DIR}/lib/.

	popd # build
	popd # dlib-${LIBEXIV2_VERSION}-Source

	touch ".dlib-${DLIB_VERSION}"	
}

build_halide() {
	if [ ! -d "halide-src" ]; then
		git clone ${HALIDE_BRANCH} halide-src
	fi

	pushd halide-src
	git pull

	mkdir -p build
	pushd build

	INSTALL_DIR="../../../libMotionCam/thirdparty/halide"

	mkdir -p ${INSTALL_DIR}

	cmake -DTARGET_WEBASSEMBLY=OFF -DWITH_TUTORIALS=OFF -DWITH_TESTS=OFF -DWITH_PYTHON_BINDINGS=OFF -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=${INSTALL_DIR} ..

	make -j${NUM_CORES}

	make install

	popd # build
	popd # halide-src

	touch ".halide"
}

halide_generate() {
	pushd ../libMotionCam/libMotionCam/generators

	./generate.sh

	popd
}

build_fpor() {
	if [ ! -d "pfor-src" ]; then
		git clone ${PFOR_BRANCH} pfor-src
	fi

	pushd pfor-src
	git pull

	INSTALL_DIR="../../libMotionCam/thirdparty/pfor"

	#
	# Build for host
	#

	make clean
	make -j${NUM_CORES}

	mkdir -p ${INSTALL_DIR}/host/lib
	mkdir -p ${INSTALL_DIR}/host/include

	cp libic.a ${INSTALL_DIR}/host/lib
	cp vint.h ${INSTALL_DIR}/host/include
	cp vp4.h ${INSTALL_DIR}/host/include
	cp bitpack.h ${INSTALL_DIR}/host/include

	#
	# Build for Android
	#

	mkdir -p ${INSTALL_DIR}/${ANDROID_ABI}/lib
	mkdir -p ${INSTALL_DIR}/${ANDROID_ABI}/include

	if [[ "$OSTYPE" == "darwin"* ]]; then
		TOOLCHAIN_HOST=darwin-x86_64
	elif [[ "$OSTYPE" == "linux-gnu"* ]]; then
		TOOLCHAIN_HOST=linux-x86_64
	fi

	export AR=${ANDROID_NDK}/toolchains/llvm/prebuilt/${TOOLCHAIN_HOST}/bin/llvm-ar
	export CL=${ANDROID_NDK}/toolchains/llvm/prebuilt/${TOOLCHAIN_HOST}/bin/aarch64-linux-android24-clang
	export CC=${ANDROID_NDK}/toolchains/llvm/prebuilt/${TOOLCHAIN_HOST}/bin/aarch64-linux-android24-clang
	export CXX=${ANDROID_NDK}/toolchains/llvm/prebuilt/${TOOLCHAIN_HOST}/bin/aarch64-linux-android24-clang++

	make clean
	make -j${NUM_CORES}

	cp libic.a ${INSTALL_DIR}/${ANDROID_ABI}/lib
	cp vint.h ${INSTALL_DIR}/${ANDROID_ABI}/include
	cp vp4.h ${INSTALL_DIR}/${ANDROID_ABI}/include
	cp bitpack.h ${INSTALL_DIR}/${ANDROID_ABI}/include

	popd
}

# Build dependencies
if [ ! -f ".opencv-${OPENCV_VERSION}" ]; then
    build_opencv
fi

if [ ! -f ".expat-${LIBEXPAT_VERSION}" ]; then
	build_expat
fi

if [ ! -f ".exiv2-${LIBEXIV2_VERSION}" ]; then
	build_exiv2
fi

if [ ! -f ".zstd-${ZSTD_VERSION}" ]; then
	build_zstd
fi

if [ ! -f ".dlib-${DLIB_VERSION}" ]; then
	build_dlib
fi

if [ ! -f ".halide" ]; then
	build_halide
fi

if [ ! -f ".pfor" ]; then
	build_fpor
fi

# Generate halide libraries
halide_generate

popd # tmp

# Compile utilities
if [[ "$OSTYPE" == "darwin"* ]]; then
	export CPLUS_INCLUDE_PATH=/usr/local/include:/usr/local/include/opencv4	
elif [[ "$OSTYPE" == "linux-gnu"* ]]; then
	export CPLUS_INCLUDE_PATH=/usr/include/opencv4
fi

pushd ./tools/convert

mkdir -p build

cd build

cmake ../

make -j${NUM_CORES}

popd

mkdir -p ./tools/bin

cp tools/convert/build/convert ./tools/bin/convert
