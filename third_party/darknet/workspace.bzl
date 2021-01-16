load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

def repo():
  http_archive(
    name = "darknet",
    urls = [ 
        "https://github.com/AlexeyAB/darknet/archive/darknet_yolo_v4_pre.tar.gz",
    ],
    build_file = "//third_party/darknet:darknet.BUILD",
  )

