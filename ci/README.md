# Building and running tests as a developer

CI targets can be run in Docker with `./ci/run_docker.sh ./ci/do_ci.sh {TARGET}`,
or inside the [devcontainer](../CONTRIBUTING.md#devcontainer-setup-for-project)
with `./ci/do_ci.sh {TARGET}`.

Available targets in `./ci/do_ci.sh`:

Additionally, `./ci/run_docker.sh` can be invoked with no arguments to open a
Docker shell where targets can be run manually.

## CMake targets

* `cmake.test`: API/SDK build and tests.
* `cmake.maintainer.sync.test`: maintainer mode ABI v1 build (sync export) and tests.
* `cmake.maintainer.async.test`: maintainer mode ABI v1 build (async export) and tests.
* `cmake.maintainer.abiv2.test`: maintainer mode ABI v2 build and tests.
* `cmake.maintainer.yaml.test`: maintainer mode ABI v1 build with configuration example and tests.
* `cmake.with_async_export.test`: standard build with async export and tests.
* `cmake.opentracing_shim.test`: build with OpenTracing shim and run tests.
* `cmake.opentracing_shim.install.test`: OpenTracing shim install validation test.
* `cmake.c++14.test`: API/SDK build with C++14 standard and nostd.
* `cmake.c++17.test`: API/SDK build with C++17 standard and nostd.
* `cmake.c++20.test`: API/SDK build with C++20 standard and nostd.
* `cmake.c++23.test`: API/SDK build with C++23 standard and nostd.
* `cmake.c++14.stl.test`: API/SDK build with C++14 STL.
* `cmake.c++17.stl.test`: API/SDK build with C++17 STL.
* `cmake.c++20.stl.test`: API/SDK build with C++20 STL.
* `cmake.c++23.stl.test`: API/SDK build with C++23 STL.
* `cmake.clang_tidy.test`: build with `clang-tidy` enabled and emit tidy log.
* `cmake.exporter.otprotocol.test`: OTLP exporter build and tests.
* `cmake.exporter.otprotocol.shared_libs.with_static_grpc.test`: OTLP exporter test with shared libs.
* `cmake.exporter.otprotocol.with_async_export.test`: OTLP exporter test with async export.
* `cmake.w3c.trace-context.build-server`: build W3C trace-context test server.
* `cmake.do_not_install.test`: verify build/test flow with `OPENTELEMETRY_INSTALL=OFF`.
* `cmake.install.test`: install tree validation and downstream CMake package test.
* `cmake.fetch_content.test`: CMake `FetchContent` integration validation test.
* `cmake.test_example_plugin`: build and load-test the example plugin.

## Bazel targets

* `bazel.test`: build and test all Bazel targets.
* `bazel.with_async_export.test`: build and test with async export enabled.
* `bazel.benchmark`: run benchmark workflow through Docker collector.
* `bazel.macos.test`: macOS-oriented Bazel build/test flags.
* `bazel.legacy.test`: Bazel legacy compatibility test subset.
* `bazel.noexcept`: build/test with exceptions disabled where supported.
* `bazel.nortti`: build/test with RTTI disabled where supported.
* `bazel.asan`: run Bazel tests with AddressSanitizer.
* `bazel.tsan`: run Bazel tests with ThreadSanitizer.
* `bazel.valgrind`: run Bazel tests under Valgrind.

## Other targets

* `benchmark`: run benchmark binaries and collect result artifacts.
* `format`: run `tools/format.sh` and fail if tracked files change.
* `code.coverage`: build with coverage flags and collect `lcov` data.
