#!/bin/bash

this=`dirname "$0"`
this=`cd "$this"; pwd`

ROOT=`cd "${this}/../.."; pwd`
SOURCE="${ROOT}/src"
TOOLS="${ROOT}/src/tools-woogeen"

REBUILD=false

download_dir=${TOOLS}/tmp
export PATH=${download_dir}/depto_tools:$PATH

usage() {
  echo
  echo "Usage:"
  echo "    -r              clean all, and rebuild"
  echo "Example:"
  echo "    build.sh        default build"
  echo "    build.sh -r     rebuild"
  echo
}

shopt -s extglob
while [[ $# -gt 0 ]]; do
  case $1 in
    *(-)r )
      REBUILD=true
      ;;
    *(-)help )
      usage
      exit 0
      ;;
    *(-)help )
      usage
      exit 0
      ;;
    * )
      echo -e "\x1b[33mUnknown argument\x1b[0m: $1"
      usage
      exit 0
      ;;
  esac
  shift
done

cd ${SOURCE}

if ${REBUILD} ; then
    if [ -e "out" ];
    then
        echo "rm out/"
        rm -rf out
    fi
fi

if [ ! -e "out" ];
then

echo "Generate ninja files"

gn gen out --args="
is_clang=false
is_debug=false
use_sysroot=false
linux_use_bundled_binutils=false
treat_warnings_as_errors=false

rtc_include_tests=false
rtc_use_openmax_dl=false
rtc_enable_protobuf=false
rtc_enable_sctp=false
rtc_build_expat=false
rtc_build_json=false
rtc_build_libjpeg=false
rtc_build_libsrtp=false
rtc_build_openmax_dl=false
rtc_build_ssl=false
rtc_build_usrsctp=false

libyuv_use_gflags=false
libyuv_include_tests=false
libyuv_disable_jpeg=true

rtc_use_h264=true
rtc_build_libvpx=true
rtc_libvpx_build_vp9=true
rtc_build_opus=true
rtc_include_opus=true
"

fi

ninja -C out video_coding audio_coding audio_conference_mixer metrics_default field_trial_default

if [ -e "${ROOT}/libwebrtc.a" ];
then
    rm -v ${ROOT}/libwebrtc.a
fi

all=`find ./out/obj/ -name "*.o"`
if [[ -n $all ]];
then
    ar cq ${ROOT}/libwebrtc.a $all
    echo "Generate libwebrtc.a OK"
else
    echo "Generate libwebrtc.a Fail"
fi

cd -


