load("@bazel_tools//tools/build_defs/repo:git.bzl", "git_repository")

def repo():
  git_repository(
    name = "clutil",
    remote = "https://github.com/jsharf/clutil.git",
    commit = "5b05715412458f05fc2731dbc27621042499f434",
    shallow_since = "1578262118 -0800"
  )
