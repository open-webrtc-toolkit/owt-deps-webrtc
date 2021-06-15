#!/bin/bash

this=`dirname "$0"`
this=`cd "$this"; pwd`

ROOT=`cd "${this}/../.."; pwd`
SOURCE="${ROOT}/src"
TOOLS="${ROOT}/src/tools-woogeen"

download_dir=${TOOLS}/tmp
mkdir -p ${download_dir}

download_third_party_module() {
    local repo=https://chromium.googlesource.com/chromium/src/third_party
    local commit=ef69db4b743f832bd71de2ddc8d3e800e563c525
    local module_name=$1
    local dowmload_dir=$2
    local download_name=$3
    local dst=$4

    wget ${repo}/+archive/${commit}/${module_name}.tar.gz -O ${download_dir}/${download_name}.${commit}.tar.gz
    mkdir -p ${dst}
    tar -m -zxf ${download_dir}/${download_name}.${commit}.tar.gz -C ${dst}
}

download_project() {
    local repo=$1
    local commit=$2
    local dowmload_dir=$3
    local download_name=$4
    local dst=$5

    wget ${repo}/+archive/${commit}.tar.gz -O ${download_dir}/${download_name}.${commit}.tar.gz
    mkdir -p ${dst}
    tar -m -zxf ${download_dir}/${download_name}.${commit}.tar.gz -C ${dst}
}

# depto_tools
repo=https://chromium.googlesource.com/chromium/tools/depot_tools.git
commit=be812619bdaa990418316ca1cefac5de8bbd3adb
name=depto_tools
dst=depto_tools
download_project ${repo} ${commit} ${download_dir} ${name} ${download_dir}/${dst}

# third_party/yasm
module_name=yasm
name=yasm
dst=third_party/yasm
download_third_party_module ${module_name} ${download_dir} ${name} ${SOURCE}/${dst}

# third_party/libvpx
module_name=libvpx
name=libvpx
dst=third_party/libvpx
download_third_party_module ${module_name} ${download_dir} ${name} ${SOURCE}/${dst}

# third_party/opus
module_name=opus
name=opus
dst=third_party/opus
download_third_party_module ${module_name} ${download_dir} ${name} ${SOURCE}/${dst}

patch -d ${SOURCE}/${dst} -p1 < ${TOOLS}/opus-disable-test.patch

# build
repo=https://chromium.googlesource.com/chromium/src/build
commit=ab0b06d1c0554464933544734bf8b3d17084d263
name=build
dst=build
download_project ${repo} ${commit} ${download_dir} ${name} ${SOURCE}/${dst}

# buildtools
repo=https://chromium.googlesource.com/chromium/buildtools.git
commit=d3074448541662f242bcee623049c13a231b5648
name=buildtools
dst=buildtools
download_project ${repo} ${commit} ${download_dir} ${name} ${SOURCE}/${dst}

wget https://storage.googleapis.com/chromium-gn/a452edf26a551fef8a884496d143b7e56cbe2ad9 -O ${SOURCE}/${dst}/linux64/gn
chmod +x ${SOURCE}/${dst}/linux64/gn

# base
repo=https://chromium.googlesource.com/chromium/src/base
commit=6f94118f9a7b502db8f6b73f8ff7b9d19153cb76
name=base
dst=base
download_project ${repo} ${commit} ${download_dir} ${name} ${SOURCE}/${dst}

# third_party/yasm/source/patched-yasm
repo=https://chromium.googlesource.com/chromium/deps/yasm/patched-yasm.git
commit=7da28c6c7c6a1387217352ce02b31754deb54d2a
name=patched-yasm
dst=third_party/yasm/source/patched-yasm
download_project ${repo} ${commit} ${download_dir} ${name} ${SOURCE}/${dst}

# third_party/libvpx/source/libvpx
repo=https://chromium.googlesource.com/webm/libvpx.git
commit=6af42f5102ad7c00d3fed389b186663a88d812ee
name=libvpx_source
dst=third_party/libvpx/source/libvpx
download_project ${repo} ${commit} ${download_dir} ${name} ${SOURCE}/${dst}

# third_party/libyuv
repo=https://chromium.googlesource.com/libyuv/libyuv.git
commit=2adb84e39e360723d19c68f315d99e3e0f88318c
name=libyuv
dst=third_party/libyuv
download_project ${repo} ${commit} ${download_dir} ${name} ${SOURCE}/${dst}

patch -d ${SOURCE}/${dst} -p1 < ${TOOLS}/libyuv-disable-jpeg.patch

# dump gni
mkdir -p ${SOURCE}/testing
touch ${SOURCE}/testing/test.gni

mkdir -p ${SOURCE}/third_party/protobuf
touch ${SOURCE}/third_party/protobuf/proto_library.gni

