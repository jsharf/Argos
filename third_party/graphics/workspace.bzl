"""loads libjpeg_turbo"""

load("@bazel_tools//tools/build_defs/repo:git.bzl", "git_repository")

def repo():
  git_repository(
    name = "graphics",
    remote = "https://github.com/jsharf/graphics.git",
    commit = "197ee0820c9888b381a5f4105dd56e2d183b93d1",
    shallow_since = "1593858827 -0700"
  )
