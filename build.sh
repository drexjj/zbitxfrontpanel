#!/bin/sh

arduino-cli compile \
  --fqbn rp2040:rp2040:rpipicow:flash=2097152_524288 \
  --output-dir ./build \
  .