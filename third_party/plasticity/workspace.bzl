load("@bazel_tools//tools/build_defs/repo:git.bzl", "git_repository")

def repo():
  git_repository(
    name = "plasticity",
    remote = "https://github.com/jsharf/plasticity.git",
    commit = "3a0a96c5a898457213f8c60ef73f2b654ca9e114",
    shallow_since = "1598951439 -0700",
  )
