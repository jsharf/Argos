"""loads SDL2"""

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

def repo():
  http_archive(
      name = 'sdl_ttf',
      urls = [
        'https://www.libsdl.org/projects/SDL_ttf/release/SDL2_ttf-2.0.15.tar.gz',
      ],
      build_file = '@//third_party/sdl2_ttf:BUILD.sdl2_ttf',
      strip_prefix = 'SDL2_ttf-2.0.15',
  )
