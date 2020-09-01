load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library")

cc_binary(
    name = "host_client",
    srcs = ["host_client.cc"],
    copts = [
        "--std=c++17",
        "-Iexternal/",
    ],
    data = [
        ":network_weights",
    ],
    deps = [
        ":cam_parser",
        "//host/netpipe:tcp",
        "@libjpeg_turbo//:turbojpeg",
        "@graphics//:sdl_canvas",
        "@clutil//:util",
        "@plasticity//compute:cl_buffer",
        "@plasticity//nnet:nnet",
        "@dear_imgui//:imgui",
        "@dear_imgui//:sdl_inputs",
        "@imgui_sdl//:imgui_sdl",
        "@linux_sdl//:sdl2",
    ],
)

filegroup(
    name = "network_weights",
    srcs = [
        "network_weights.json",
    ],
)

cc_library(
    name = "cam_parser",
    hdrs = ["cam_parser.h"],
    srcs = ["cam_parser.cc"],
    deps = [],
    linkopts = ["-lpthread",],
)
