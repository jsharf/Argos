"""loads libjpeg_turbo"""

load("@bazel_tools//tools/build_defs/repo:git.bzl", "git_repository")

def repo():
  git_repository(
    name = "graphics",
    remote = "https://github.com/jsharf/graphics.git",
    commit = "b8ebc5f048508a5349600faf6cf67a1116b5d6ff",
    shallow_since = "1593769723 -0700",
  )
