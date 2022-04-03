#!/bin/bash

echo "Building optrand..."
cmake . && 
    make -j $(nproc)
