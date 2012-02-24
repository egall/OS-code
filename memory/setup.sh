#!/bin/sh
cd /usr/src/servers
make image
make install
cd /usr/src/
make libraries
cd /usr/src/tools
make hdboot
make install
