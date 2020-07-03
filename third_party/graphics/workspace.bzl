"""loads libjpeg_turbo"""

load("@bazel_tools//tools/build_defs/repo:git.bzl", "git_repository")

def repo():
  git_repository(
    name = "graphics",
    remote = "https://github.com/jsharf/graphics.git",
    commit = "3bd434bf02990686deba440a3b34b0bf9bf2d05b",
    shallow_since = "1593769723 -0700",
  )
