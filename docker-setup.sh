#!/bin/bash

echo "Initializing submodules..."
git submodule init && \
    git submodule update || exit 1

echo "Installing dependencies..."
sudo apt-get update && \
    sudo apt-get install \
        cmake \
        g++ \
        libssl-dev \
        doxygen \
        pkg-config \
        libuv1-dev \
        build-essential \
        git \
        autoconf automake libtool \
        || exit 1

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


echo "Building optrand..."
cmake . && 
    make -j $(nproc)