#!/bin/bash
set -e

PLATFORM="$(uname -s)"
RAYLIB_LIB="vendor/raylib/lib/$PLATFORM/libraylib.a"

echo "=== Cleaning ==="
rm -f src/*.o tools/*.o universe universe_visual test_generate test_probe test_travel test_agent test_render universe.db

# Build Raylib for this platform if not already cached
if [ ! -f "$RAYLIB_LIB" ]; then
    echo "=== Building Raylib for $PLATFORM/$(uname -m) ==="
    mkdir -p "vendor/raylib/lib/$PLATFORM"
    make -C vendor/raylib/src clean || true
    if [ "$PLATFORM" = "Linux" ]; then
        C_INCLUDE_PATH=stub_x11 make -C vendor/raylib/src PLATFORM=PLATFORM_DESKTOP RAYLIB_LIBTYPE=STATIC
    else
        make -C vendor/raylib/src PLATFORM=PLATFORM_DESKTOP RAYLIB_LIBTYPE=STATIC
    fi
    cp vendor/raylib/src/libraylib.a "$RAYLIB_LIB"
    echo "=== Cached $RAYLIB_LIB ==="
fi

echo "=== Building headless ==="
make all

echo "=== Building visual ==="
make visual

echo "=== Running tests ==="
make test

echo ""
echo "Done. Binaries:"
ls -lh universe universe_visual

echo ""
echo "=== Launching visual ==="
LD_LIBRARY_PATH=. ./universe_visual --visual --seed 42
