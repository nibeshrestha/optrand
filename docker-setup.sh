#!/bin/bash

ROOT=$(pwd)

function install_libff () {
    echo "Installing libff"
    cd depends || exit 1
    cd libff && \
        git submodule init && \
        git submodule update && \
        mkdir -p build || exit 1
    cd build && \
        rm -rf * && \
        cmake .. && \
        make -j $(nproc) && \
        sudo make install
}

function install_gf_complete () {
    # sudo apt-get install 
    echo "Installing gf-complete..."
    git clone https://github.com/ceph/gf-complete.git && \
        cd gf-complete && \
        ./autogen.sh && \
        sudo ./configure && \
        make -j $(nproc) && \
        sudo make install && \
        cd .. && \
        rm -rf gf-complete || $(rm -rf gf-complete && exit 1)
}

function install_jerasure () {
    echo "Installing jerasure..."
    git clone https://github.com/ceph/jerasure.git && \
        cd jerasure && \
        autoreconf --force --install && \
        ./configure && \
        make -j $(nproc) && \
        sudo make install && \
        cd .. && rm -rf jerasure || $(rm -rf jerasure && exit 1)
}

function initialize_submodules () {
    echo "Initializing submodules..."
    git submodule init && \
        git submodule update || exit 1
}

function install_deps () {
    echo "Installing dependencies..."
    sudo apt-get update && \
        sudo apt-get install -y \
            cmake \
            g++ \
            libssl-dev \
            doxygen \
            pkg-config \
            libuv1-dev \
            build-essential \
            libtool \
            git \
            autoconf automake libtool \
            libboost-all-dev libgmp3-dev libprocps-dev libsodium-dev \
            || exit 1
}

function install_ate_pairing () {
    CLEAN_BUILD=${CLEAN_BUILD:-0}
    echo "Installing ate-pairing..."
    cd depends/ate-pairing/
    if [ "$CLEAN_BUILD" -eq 1 ]; then
        echo "Cleaning previous build..."
        echo
        make clean
    fi
    make -j $NUM_CPUS -C src \
        SUPPORT_SNARK=1 \
        $ATE_PAIRING_FLAGS

    INCL_DIR=/usr/local/include/ate-pairing/include
    sudo mkdir -p "$INCL_DIR"
    sudo cp include/bn.h  "$INCL_DIR"
    sudo cp include/zm.h  "$INCL_DIR"
    sudo cp include/zm2.h "$INCL_DIR"

    # WARNING: Need this because (the newer 'develop' branch of) libff uses #include "ate-pairing/include/bn.h"
    # Actually, no longer the case, so removing
    #sudo ln -sf "$INCL_DIR" "$INCL_DIR/include"

    # WARNING: Need this due to a silly #include "depends/[...]" directive from libff
    # (/usr/local/include/libff/algebra/curves/bn128/bn128_g1.hpp:12:44: fatal error: depends/ate-pairing/include/bn.h: No such file or directory)
    # Actually, no longer the case, so removing
    #sudo mkdir -p "$INCL_DIR/../depends/ate-pairing/"
    #sudo ln -sf "$INCL_DIR" "$INCL_DIR/../depends/ate-pairing/include"

    LIB_DIR=/usr/local/lib
    sudo cp lib/libzm.a "$LIB_DIR"
    # NOTE: Not sure why, but getting error at runtime that this cannot be loaded. Maybe it should be zm.so?
    #sudo cp lib/zm.so "$LIB_DIR/libzm.so"
    cd ../..
}

function build () {
    echo "Building optrand..."
    mkdir -p "build/" && \
        cd build && \
        cmake .. && \
        make -j $(nproc)
}

initialize_submodules

cd "${ROOT}"
install_deps

cd "${ROOT}"
install_gf_complete

cd "${ROOT}"
install_jerasure

cd "${ROOT}"
install_libff

cd "${ROOT}"
install_ate_pairing

cd "${ROOT}"
build

cd "${ROOT}"