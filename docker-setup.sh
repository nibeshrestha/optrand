#!/bin/bash

function install_libff () {
    echo "Installing libff"
    git clone https://github.com/scipr-lab/libff || exit 1
    cd libff && \
        git submodule init && git submodule update && \
        mkdir build || ( rm -rf libff && exit 1 )
    cd build && \
        cmake .. && make -j $(nproc) && \
        sudo make install && \
        cd ../.. && \
        rm -rf libff || \
        ( rm -rf libff && exit 1 )

}

echo "Initializing submodules..."
git submodule init && \
    git submodule update || exit 1

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
        git \
        autoconf automake libtool \
        libboost-all-dev libgmp3-dev libprocps-dev libsodium-dev \
        || exit 1

# sudo apt-get install 
echo "Installing gf-complete..."
git clone https://github.com/ceph/gf-complete.git && \
    cd gf-complete && \
    ./autogen.sh && \
    ./configure && \
    make -j $(nproc) && \
    sudo make install && \
    cd .. && \
    rm -rf gf-complete || $(rm -rf gf-complete && exit 1)

echo "Installing jerasure..."
git clone https://github.com/ceph/jerasure.git && \
    cd jerasure && \
    autoreconf --force --install && \
    ./configure && \
    make -j $(nproc) && \
    sudo make install && \
    cd .. && rm -rf jerasure || $(rm -rf jerasure && exit 1)

install_libff

echo "Building optrand..."
cmake . && 
    make -j $(nproc)