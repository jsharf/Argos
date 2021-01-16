load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

def repo():
  # This repository is used to build foreign projects (makefile & cmake) and link
  # them with bazel binaries. In particularly, we use this to build darknet.
  http_archive(
     name = "rules_foreign_cc",
     strip_prefix = "rules_foreign_cc-master",
     url = "https://github.com/bazelbuild/rules_foreign_cc/archive/master.zip",
     sha256 = "3e6b0691fc57db8217d535393dcc2cf7c1d39fc87e9adb6e7d7bab1483915110",
  )

