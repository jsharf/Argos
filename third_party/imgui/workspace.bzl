""" Loads imgui. """

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

def repo():
  http_archive(
        name = "dear_imgui",
        urls = ['https://github.com/ocornut/imgui/archive/v1.77.tar.gz'],
        strip_prefix = 'imgui-1.77',
        build_file_content = """
cc_library(
  name = "imgui",
  srcs = glob(['*.cpp']),
  hdrs = glob(['*.h']),
  visibility = ["//visibility:public"],
)

cc_library(
  name = "sdl_inputs",
  srcs = ["examples/imgui_impl_sdl.cpp"],
  hdrs = ["examples/imgui_impl_sdl.h"],
  visibility = ["//visibility:public"],
  includes = [
    "external/linux_sdl/include",
  ],
  deps = [
    ":imgui",
    "@linux_sdl//:sdl2",
  ],
)
        """,
        sha256 = "c0dae830025d4a1a169df97409709f40d9dfa19f8fc96b550052224cbb238fa8",
  )
