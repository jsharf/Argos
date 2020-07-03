load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library")

cc_binary(
    name = "host_client",
    srcs = ["host_client.cc"],
    copts = [
        "-Iexternal/",
    ],
    deps = [
        ":cam_parser",
        "//host/netpipe:tcp",
        "@libjpeg_turbo//:jpeg",
        "@graphics//:sdl_canvas",
        "@dear_imgui//:imgui",
        "@dear_imgui//:sdl_inputs",
        "@imgui_sdl//:imgui_sdl",
        "@linux_sdl//:sdl2",
    ],
)

cc_library(
    name = "cam_parser",
    hdrs = ["cam_parser.h"],
    srcs = ["cam_parser.cc"],
    deps = [],
    linkopts = ["-lpthread",],
)
