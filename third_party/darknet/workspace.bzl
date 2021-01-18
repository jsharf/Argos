load("@bazel_tools//tools/build_defs/repo:git.bzl", "new_git_repository")

def repo():
  new_git_repository(
    name = "darknet",
    remote = "https://github.com/AlexeyAB/darknet.git",
    commit = "103d301ccbc19e47e002005bdfdbaf07a92cd880",
    build_file = "//third_party/darknet:darknet.BUILD",
    shallow_since = "1599040208 -0700",
  )

