# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file LICENSE.rst or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "/home/sujan/CLionProjects/Talos/cmake-build-debug/_deps/onnxruntime_prebuilt-src")
  file(MAKE_DIRECTORY "/home/sujan/CLionProjects/Talos/cmake-build-debug/_deps/onnxruntime_prebuilt-src")
endif()
file(MAKE_DIRECTORY
  "/home/sujan/CLionProjects/Talos/cmake-build-debug/_deps/onnxruntime_prebuilt-build"
  "/home/sujan/CLionProjects/Talos/cmake-build-debug/_deps/onnxruntime_prebuilt-subbuild/onnxruntime_prebuilt-populate-prefix"
  "/home/sujan/CLionProjects/Talos/cmake-build-debug/_deps/onnxruntime_prebuilt-subbuild/onnxruntime_prebuilt-populate-prefix/tmp"
  "/home/sujan/CLionProjects/Talos/cmake-build-debug/_deps/onnxruntime_prebuilt-subbuild/onnxruntime_prebuilt-populate-prefix/src/onnxruntime_prebuilt-populate-stamp"
  "/home/sujan/CLionProjects/Talos/cmake-build-debug/_deps/onnxruntime_prebuilt-subbuild/onnxruntime_prebuilt-populate-prefix/src"
  "/home/sujan/CLionProjects/Talos/cmake-build-debug/_deps/onnxruntime_prebuilt-subbuild/onnxruntime_prebuilt-populate-prefix/src/onnxruntime_prebuilt-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/home/sujan/CLionProjects/Talos/cmake-build-debug/_deps/onnxruntime_prebuilt-subbuild/onnxruntime_prebuilt-populate-prefix/src/onnxruntime_prebuilt-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/home/sujan/CLionProjects/Talos/cmake-build-debug/_deps/onnxruntime_prebuilt-subbuild/onnxruntime_prebuilt-populate-prefix/src/onnxruntime_prebuilt-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
