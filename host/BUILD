load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library")

cc_binary(
    name = "host_client",
    srcs = ["host_client.cc"],
    deps = [
        ":cam_parser",
        "//host/netpipe:tcp",
    ],
)

cc_library(
    name = "cam_parser",
    hdrs = ["cam_parser.h"],
    srcs = ["cam_parser.cc"],
    deps = [],
    linkopts = ["-lpthread",],
)
