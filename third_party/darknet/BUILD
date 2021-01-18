load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library")
load("@rules_foreign_cc//tools/build_defs:make.bzl", "make")

make(
    name = "make_darknet",
    lib_source = "@darknet//:darknet_sources",
    shared_libraries = ["libdarknet.so"],
    make_env_vars = {
        "GPU": "1",
        "CUDNN": "1",
        "OPENCV": "1",
        "LIBSO": "1",
    },
    make_commands = ["make GPU=1 CUDNN=1 LIBSO=1 OPENCV=1 libdarknet.so; cp libdarknet.so $INSTALLDIR/lib/"],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "darknet",
    srcs = [],
    hdrs = [],
    deps = [
        "@darknet//:darknet_cc_api",
        ":make_darknet",
    ],
    visibility = ["//visibility:public"],
)
