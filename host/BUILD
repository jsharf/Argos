load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library")
load("@rules_foreign_cc//tools/build_defs:make.bzl", "make")

cc_binary(
    name = "host_client",
    srcs = ["host_client.cc"],
    copts = [
        "--std=c++17",
        "-O3",
        "-Iexternal/",
    ],
    data = [
        ":yolov4_model",
        # Used to access the object ID file.
        "@darknet//:darknet_sources",
    ],
    linkopts = select({
        "@clutil//:osx": ["-framework OpenCL"],
        "@clutil//:linux": [
            "-lOpenCL",
            "-L/usr/local/cuda-8.0/targets/x86_64-linux/lib",
            "-L/usr/lib/x86_64-linux-gnu/",
        ],
        "//conditions:default": [
            "-lOpenCL",
            "-L/usr/local/cuda-8.0/targets/x86_64-linux/lib",
            "-L/usr/lib/x86_64-linux-gnu/",
        ],
    }),
    deps = [
        ":cam_parser",
        "//host/netpipe:tcp",
        "//third_party/darknet:darknet",
        "//third_party/sdl2_ttf:sdl_ttf",
        "@libjpeg_turbo//:turbojpeg",
        "@graphics//:sdl_canvas",
        "@clutil//:util",
        "@dear_imgui//:imgui",
        "@dear_imgui//:sdl_inputs",
        "@imgui_sdl//:imgui_sdl",
        "@linux_sdl//:sdl2",
    ],
)

filegroup(
    name = "yolov4_model",
    srcs = [
        "yolov4.cfg",
        "yolov4-tiny.cfg",
        "yolov4.weights",
        "yolov4-tiny.weights",
    ],
)

cc_library(
    name = "cam_parser",
    hdrs = ["cam_parser.h"],
    srcs = ["cam_parser.cc"],
    deps = [],
    linkopts = ["-lpthread",],
)
