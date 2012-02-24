#!/bin/sh
cd /usr/src
make world
cd tools
make clean install
