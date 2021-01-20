load("@rules_foreign_cc//tools/build_defs:configure.bzl", "configure_make")

cc_library(
    name = "sdl_ttf",
    srcs = [],
    hdrs = [],
    deps = [
        "@sdl_ttf//:sdl_ttf_api",
        ":make_sdl_ttf",
    ],
    visibility = ["//visibility:public"],
)

configure_make(
    name = "make_sdl_ttf",
    configure_options = [
    ],
    lib_source = "@sdl_ttf//:sdl_ttf_sources",
    shared_libraries = ["libSDL2_ttf.so"],
    configure_in_place = True,
    out_include_dir = ".libs/",
    visibility = ["//visibility:public"],
)

