workspace(name = "argos")


load("//third_party/nasm:workspace.bzl", nasm = "repo")
load("//third_party/jpeg:workspace.bzl", jpeg = "repo")
load("//third_party/imgui:workspace.bzl", imgui = "repo")
load("//third_party/imgui_sdl:workspace.bzl", imgui_sdl = "repo")
load("//third_party/sdl2:workspace.bzl", sdl2 = "repo")
load("//third_party/sdl2_ttf:workspace.bzl", sdl2_ttf = "repo")
load("//third_party/graphics:workspace.bzl", graphics = "repo")
load("//third_party/clutil:workspace.bzl", clutil = "repo")
load("//third_party/rapidjson:workspace.bzl", rapidjson = "repo")
load("//third_party/plasticity:workspace.bzl", plasticity = "repo")
load("//third_party/rules_foreign_cc:workspace.bzl", rules_foreign_cc = "repo")
rules_foreign_cc()
load("@rules_foreign_cc//:workspace_definitions.bzl", "rules_foreign_cc_dependencies")
load("//third_party/darknet:workspace.bzl", darknet = "repo")

rules_foreign_cc_dependencies([])
nasm()
jpeg()
imgui()
imgui_sdl()
sdl2()
sdl2_ttf()
graphics()
clutil()
rapidjson()
plasticity()
darknet()
