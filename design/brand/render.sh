#!/bin/sh
# Rebuild ui/public + docs brand assets from design/brand specs.
set -e
cd "$(dirname "$0")/../.."
python3 design/brand/render.py
