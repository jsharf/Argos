load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

def repo():
  # This repository is used to build foreign projects (makefile & cmake) and link
  # them with bazel binaries. In particularly, we use this to build darknet.
  http_archive(
      name = "rules_foreign_cc",
      sha256 = "c2cdcf55ffaf49366725639e45dedd449b8c3fe22b54e31625eb80ce3a240f1e",
      strip_prefix = "rules_foreign_cc-0.1.0",
      url = "https://github.com/bazelbuild/rules_foreign_cc/archive/0.1.0.zip",
  )

