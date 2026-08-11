# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "/home/sujan/CLionProjects/Talos/cmake-build-debug/_deps/onnxruntime_prebuilt-src"
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
