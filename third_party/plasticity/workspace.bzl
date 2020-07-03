load("@bazel_tools//tools/build_defs/repo:git.bzl", "git_repository")

def repo():
  # Use plasticity for the math/geometry libraries.
  git_repository(
    name = "plasticity",
    remote = "https://github.com/jsharf/plasticity.git",
    commit = "a3e7f213251f4f6b4904be6ea6fde93d9519e1ce",
    shallow_since = "1590994780 -0700"
  )
