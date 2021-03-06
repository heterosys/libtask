#!/bin/sh
set -e

sudo apt-get purge -y libgcc-*-dev || true
sudo apt-get install -y \
  build-essential \
  libboost-coroutine-dev \
  libboost-stacktrace-dev \
  libgoogle-glog-dev \
  python3-pip

sudo apt-get autoremove -y
sudo -H python3 -m pip install --upgrade pip==20.3.4
sudo -H python3 -m pip install cmake