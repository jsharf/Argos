load("@bazel_tools//tools/build_defs/repo:git.bzl", "git_repository")

def repo():
  git_repository(
    name = "rapidjson",
    remote = "https://github.com/bazelregistry/rapidjson.git",
    commit = "930febe52f53ad1ddb5ad60ed861835f7559269f",
    shallow_since = "1593371139 -0700"
  )
