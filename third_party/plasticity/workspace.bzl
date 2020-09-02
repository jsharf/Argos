load("@bazel_tools//tools/build_defs/repo:git.bzl", "git_repository")

def repo():
  git_repository(
    name = "plasticity",
    remote = "https://github.com/jsharf/plasticity.git",
    commit = "59c775349c598193613a3ff0be6971f15e02aace",
    shallow_since = "1599040208 -0700",
  )
