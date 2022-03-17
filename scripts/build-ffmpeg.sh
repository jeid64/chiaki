#!/bin/bash

cd $(dirname "${BASH_SOURCE[0]}")/..
cd "./$1"
shift
ROOT="`pwd`"

TAG=n5.0

git clone https://git.ffmpeg.org/ffmpeg.git --depth 1 -b $TAG && cd ffmpeg || exit 1

./configure --disable-all --enable-gpl --enable-version3 --enable-libx264 --enable-libx265 --enable-avcodec --enable-decoder=h264 --enable-decoder=hevc --enable-hwaccel=h264_d3d11va --enable-hwaccel=hevc_d3d11va --enable-dxva2 --enable-d3d11va --prefix="$ROOT/ffmpeg-prefix" "$@" || exit 1
make -j4 || exit 1
make install || exit 1
