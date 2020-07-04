#include "host/netpipe/tcp.h"
#include "host/cam_parser.h"
#include "external/linux_sdl/include/SDL.h"
#include "external/graphics/sdl_canvas.h"
#include "external/imgui_sdl/imgui_sdl.h"
#include "external/dear_imgui/imgui.h"
#include "external/dear_imgui/examples/imgui_impl_sdl.h"
#include "external/libjpeg_turbo/turbojpeg.h"

#include <unistd.h>
#include <atomic>
#include <cassert>
#include <thread>
#include <chrono>
#include <memory>

struct Jpeg {
  int width;
  int height;
  int subsample;
  int colorspace; 
  uint8_t bit_depth;
  uint8_t *data;
  size_t data_size;
};

// Flags to use during decompression. Options include:
// TJFLAG_FASTDCT
// TJFLAG_ACCURATEDCT
// TJFLAG_FASTUPSAMPLE
static constexpr int kDecompressionFlags = 0;

std::unique_ptr<Jpeg> decode_jpeg(uint8_t *data, size_t bytes) {
  tjhandle operation = tjInitDecompress();
  assert(operation != nullptr);
  auto jpeg = std::make_unique<Jpeg>();
  if (tjDecompressHeader3(operation, data, bytes, &jpeg->width, &jpeg->height, &jpeg->subsample, &jpeg->colorspace) < 0) {
    return nullptr;
  }

  // Allocate the JPEG buffer. 24-bit colorspace.
  int pixelFormat = TJPF_RGB;
  jpeg->data = (uint8_t *) tjAlloc(jpeg->width * jpeg->height * tjPixelSize[pixelFormat]);
  jpeg->data_size = jpeg->height * 3;
  if (tjDecompress2(operation, data, bytes, jpeg->data, jpeg->width, 0, jpeg->height,
                      pixelFormat, kDecompressionFlags) < 0) {
    return nullptr;
  }
  tjDestroy(operation);
  return std::move(jpeg);
}

int main(int argc, char *argv[]) {
  if (argc != 3) {
    std::cerr << "Usage: host_client server_ip server_port." << std::endl;
    return -1;
  }

  auto previous_video_time = std::chrono::high_resolution_clock::now();
  auto previous_render_time = std::chrono::high_resolution_clock::now();
  double video_framerate = 0;

  // SDL scene.
  const int width = 800;
  const int height = 600;
  SdlCanvas canvas(width, height);
  auto *renderer = canvas.renderer();
  auto *window = canvas.window();

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
	ImGuiSDL::Initialize(renderer, 800, 600);
  ImGui_ImplSDL2_InitForOpenGL(window, nullptr);
  //ImGui::StyleColorsDark();

  std::string address(argv[1], strlen(argv[1]));
  int port = strtol(argv[2], nullptr, 10);

  TCPSender socket(address, port, /*debug=*/true);

  assert(socket.Initialize());
  assert(socket.Connect());

  std::string request = R"request(GET /stream HTTP/1.1
Host: 192.168.1.104:81

  )request";

  socket.Write(reinterpret_cast<const uint8_t *>(request.c_str()), request.size());

  // Open a clone of stdout in binary mode.
  FILE *const out = fdopen(dup(fileno(stdout)), "wb");
  if (out == nullptr) {
    fclose(out);
    std::cerr << "Issue opening stdout in binary mode.";
  }

  cam::CamParser http_parser;
  std::atomic<bool> parsing_done;
  parsing_done = false;

  // Open a thread
  std::thread parse_thread([&http_parser, &parsing_done]() {
    while (!parsing_done && http_parser.Poll()) {
      usleep(10);
    }
  });

  for (int i = 0; i < 20000; ++i) {
    uint8_t recv_buffer[256];
    int data_len = socket.Read(recv_buffer, sizeof(recv_buffer)-1);
    if (data_len == -1) {
      std::cout << "\nDone!" << std::endl;
      return 0;
    }
    recv_buffer[data_len] = '\0';
    http_parser.InsertBinary(recv_buffer, data_len);
    std::unique_ptr<Jpeg> raw_img;
    if (http_parser.IsImageAvailable()) {
      std::cerr << "Image is available" << std::endl;

      auto video_time = std::chrono::high_resolution_clock::now();
      std::chrono::duration<double> video_dt = video_time - previous_video_time;
      if (video_dt.count() != 0) {
        video_framerate = 1 / (video_dt.count());
      }
      previous_video_time = video_time;

      int bytes_read = 0;
      int num_bytes = 0;
      uint8_t jpeg_buffer[4 * 1024 * 1024];  // 4MB.
      while (num_bytes = http_parser.RetrieveJpeg(
                 jpeg_buffer + bytes_read, sizeof(jpeg_buffer) - bytes_read),
             num_bytes > 0) {
        bytes_read += num_bytes;
        if ((size_t)bytes_read >= sizeof(jpeg_buffer)) {
          std::cerr << "JPEG buffer isn't big enough, ran out of memory." << std::endl;
          exit(1);
        }
      }
      raw_img = decode_jpeg(jpeg_buffer, bytes_read);
      assert(raw_img);
    }
    SDL_Texture *texture = nullptr;
    if (raw_img) {
      SDL_Surface * image = SDL_CreateRGBSurfaceFrom(raw_img->data,
                                          raw_img->width,
                                          raw_img->height,
                                          24,
                                          3 * raw_img->width,
                                          /*rmask=*/0xff0000,
                                          /*gmask=*/0x00ff00,
                                          /*bmask=*/0x0000ff,
                                          /*amask=*/0);
       texture = SDL_CreateTextureFromSurface(renderer, image);
    }
    // Clear the canvas before re-rendering.
    canvas.Clear();
    // UI handling.
    ImGuiIO& io = ImGui::GetIO();
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      ImGui_ImplSDL2_ProcessEvent(&event);
      if (event.type == SDL_QUIT) return 0;
			if (event.type == SDL_WINDOWEVENT) {
				if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
					io.DisplaySize.x = static_cast<float>(event.window.data1);
					io.DisplaySize.y = static_cast<float>(event.window.data2);
				}
			}
    }
    ImGui_ImplSDL2_NewFrame(window);
    io.DeltaTime = 1 / 60.0f;
    ImGui::NewFrame();
    // ImGui UI defined here.
    ImGui::Begin("Video Stream");
    if (texture != nullptr) {
      ImGui::Image(texture, ImVec2(raw_img->width, raw_img->height));
    }
    ImGui::End();
    ImGui::Begin("Info");
    auto render_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> render_dt = render_time - previous_render_time;
    double render_framerate = (render_dt.count() != 0) ? 1 / (render_dt.count()) : 0;
    previous_render_time = render_time;

    ImGui::Text("Video Framerate %f", video_framerate);
    ImGui::Text("Render Loop Framerate %f", render_framerate);
    ImGui::End();
    // End of ImGui UI definition.

    ImGui::Render();
    ImGuiSDL::Render(ImGui::GetDrawData());
    canvas.Render();
    usleep(100);
  }
  parsing_done = true;
  parse_thread.join();
  fclose(out);

  ImGuiSDL::Deinitialize();
  ImGui::DestroyContext();
  ImGui_ImplSDL2_Shutdown();

  std::cerr << "DONE." << std::endl;
  return 0;
}
