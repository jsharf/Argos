"""loads SDL2"""

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

def repo():
  http_archive(
      name = 'linux_sdl',
      urls = [
        'https://www.libsdl.org/release/SDL2-2.0.7.tar.gz',
      ],
      build_file = '@//third_party/sdl2:BUILD.sdl2',
      strip_prefix = 'SDL2-2.0.7',
      sha256 = "ee35c74c4313e2eda104b14b1b86f7db84a04eeab9430d56e001cea268bf4d5e",
  )
