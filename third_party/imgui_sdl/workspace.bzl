"""loads libjpeg_turbo"""

load("@bazel_tools//tools/build_defs/repo:git.bzl", "new_git_repository")

def repo():
  new_git_repository(
    name = "imgui_sdl",
    remote = "https://github.com/Tyyppi77/imgui_sdl.git",
    commit = "0812f5ed05c0eb801fdb8ae71eb0cc75c95a8cff",
    shallow_since = "1580404908 +0200",
    build_file_content = """
cc_library(
  name = "imgui_sdl",
  srcs = glob(['*.cpp']),
  hdrs = glob(['*.h']),
  includes = [
    "external/linux_sdl/include",
    "external/dear_imgui/",
  ],
  copts = [
    "-std=c++1z",
  ],
  visibility = ["//visibility:public"],
  deps = [
    "@linux_sdl//:sdl2",
    "@dear_imgui//:imgui",
  ],
)
    """,

  )
