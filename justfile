# Build type: Debug, Release, RelWithDebInfo, MinSizeRel.
# Override per invocation: `just build_type=Release run`
set positional-arguments

build_type := "Debug"
build_dir := "build"

# List available recipes
default:
    @just --list

# Configure (if needed) and compile
build:
    cmake -S . -B {{build_dir}} -DCMAKE_BUILD_TYPE={{build_type}}
    cmake --build {{build_dir}} -j

# Build, then launch the browser; extra args are URLs or search terms
run *args: build
    ./{{build_dir}}/otto "$@"

# Remove build artifacts
clean:
    rm -rf {{build_dir}}

# Build and run the unit tests
test: build
    ctest --test-dir {{build_dir}} --output-on-failure
