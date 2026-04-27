#!/bin/sh

arduino-cli compile \
  --fqbn rp2040:rp2040:rpipicow \
  --output-dir ./build \
  .