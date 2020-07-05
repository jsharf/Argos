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
#include <future>
#include <chrono>
#include <memory>
#include <thread>
#include <mutex>

struct Jpeg {
  int width;
  int height;
  int subsample;
  int colorspace; 
  uint8_t *data;
  size_t data_size;
};

// Flags to use during decompression. Options include:
// TJFLAG_FASTDCT
// TJFLAG_ACCURATEDCT
// TJFLAG_FASTUPSAMPLE
static constexpr int kDecompressionFlags = TJFLAG_ACCURATEDCT;

Jpeg decode_jpeg(uint8_t *data, size_t bytes) {
  tjhandle operation = tjInitDecompress();
  assert(operation != nullptr);
  Jpeg jpeg;
  assert(tjDecompressHeader3(operation, data, bytes, &jpeg.width, &jpeg.height, &jpeg.subsample, &jpeg.colorspace) >= 0);

  // Allocate the JPEG buffer. 24-bit colorspace.
  int pixelFormat = TJPF_RGB;
  jpeg.data = (uint8_t *) tjAlloc(jpeg.width * jpeg.height * tjPixelSize[pixelFormat]);
  jpeg.data_size = jpeg.width * jpeg.height * tjPixelSize[pixelFormat];
  assert(tjDecompress2(operation, data, bytes, jpeg.data, jpeg.width, 0, jpeg.height,
                      pixelFormat, kDecompressionFlags) >= 0);
  tjDestroy(operation);
  return jpeg;
}

class RenderThread {
  public:
    RenderThread(int width, int height) : canvas_(width, height), width_(width), height_(height) {
      IMGUI_CHECKVERSION();
      ImGui::CreateContext();
      ImGuiSDL::Initialize(canvas_.renderer(), 800, 600);
      ImGui_ImplSDL2_InitForOpenGL(canvas_.window(), nullptr);
      ImGui::StyleColorsDark();
      previous_video_time_ = std::chrono::high_resolution_clock::now();
      previous_render_time_ = std::chrono::high_resolution_clock::now(); 
    }
    void operator()() {
      while (true) {
        std::lock_guard<std::mutex> lock(control_lock_);
        if (done_) {
          return;
        }
        // UI handling.
        ImGuiIO& io = ImGui::GetIO();
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
          ImGui_ImplSDL2_ProcessEvent(&event);
          if (event.type == SDL_QUIT) done_ = true;
          if (event.type == SDL_WINDOWEVENT) {
            if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
              io.DisplaySize.x = static_cast<float>(event.window.data1);
              io.DisplaySize.y = static_cast<float>(event.window.data2);
            }
          }
        }
        // Clear the canvas before re-rendering.
        canvas_.Clear();
        if (bg_texture_) {
          SDL_RenderCopy(canvas_.renderer(), bg_texture_, NULL, NULL);
          SDL_RenderPresent(canvas_.renderer());
        }
        ImGui_ImplSDL2_NewFrame(canvas_.window());
        io.DeltaTime = 1 / 60.0f;
        ImGui::NewFrame();
        // ImGui UI defined here.
        ImGui::Begin("Info");
        auto render_time = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> render_dt = render_time - previous_render_time_;
        double render_framerate = (render_dt.count() != 0) ? 1 / (render_dt.count()) : 0;
        previous_render_time_ = render_time;

        ImGui::Text("Video Framerate %f", video_framerate_);
        ImGui::Text("Render Loop Framerate %f", render_framerate);
        ImGui::End();
        // End of ImGui UI definition.

        ImGui::Render();
        ImGuiSDL::Render(ImGui::GetDrawData());
        canvas_.Render();
    }
  }

  void SetBGImage(uint8_t *image, int image_size) {
    SDL_Surface * surface = SDL_CreateRGBSurfaceFrom(image,
                                        width_,
                                        height_,
                                        24,
                                        3 * width_,
                                        /*rmask=*/0x0000ff,
                                        /*gmask=*/0x00ff00,
                                        /*bmask=*/0xff0000,
                                        /*amask=*/0);
    SDL_Texture *texture = SDL_CreateTextureFromSurface(canvas_.renderer(), surface);
    {
      std::lock_guard<std::mutex> lock(control_lock_);
      if (bg_texture_!= nullptr) {
        SDL_DestroyTexture(bg_texture_);
      }
      bg_texture_ = texture;
    } 
    SDL_FreeSurface(surface);

    // Measure how frequently we're receiving BG images to calculate framerate.
    auto video_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> video_dt = video_time - previous_video_time_;
    if (video_dt.count() != 0) {
      video_framerate_ = 1 / (video_dt.count());
    }
    previous_video_time_ = video_time;
  }

  bool done() { 
    std::lock_guard<std::mutex> lock(control_lock_);
    return done_;
  }

  void Exit() {
    std::lock_guard<std::mutex> lock(control_lock_);
    done_ = true;
  }
  private:
    std::mutex control_lock_;
    bool done_ = false;
    SDL_Texture* bg_texture_ = nullptr;
    SdlCanvas canvas_;
    int width_, height_;

    std::chrono::high_resolution_clock::time_point previous_video_time_;
    std::chrono::high_resolution_clock::time_point previous_render_time_; 
    double video_framerate_;
};

int main(int argc, char *argv[]) {
  if (argc != 3) {
    std::cerr << "Usage: host_client server_ip server_port." << std::endl;
    return -1;
  }

  // SDL scene.
  const int width = 1280;
  const int height = 720;

  RenderThread render_module(width, height);
  auto render_future = std::async(std::launch::async, [&render_module](){render_module();});

  std::future<Jpeg> pending_img;
  std::unique_ptr<Jpeg> last_img;

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

  while (render_future.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
    uint8_t recv_buffer[16 * 1024];  // 16 KB.
    std::cout << "Read" << std::endl;
    int data_len = socket.Read(recv_buffer, sizeof(recv_buffer)-1);
    std::cout << "Read done" << std::endl;
    std::cout << data_len << std::endl;
    if (data_len == -1) {
      std::cout << "\nDone!" << std::endl;
      return 0;
    }
    recv_buffer[data_len] = '\0';
    http_parser.InsertBinary(recv_buffer, data_len);
    if (http_parser.IsImageAvailable()) {
      std::cout << "Images available: " << http_parser.ImagesAvailable() << std::endl;

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
      pending_img = std::async(std::launch::async, [&]()-> Jpeg {return decode_jpeg(jpeg_buffer, bytes_read);});
    }
    if (pending_img.valid()) {
      if (last_img) {
        tjFree(last_img->data);
      }
      last_img = std::make_unique<Jpeg>(pending_img.get());
      render_module.SetBGImage(last_img->data, last_img->data_size);
    }
  }
  std::cout << "EXITED NORMALLY" << std::endl;

  if (last_img) {
    //tjFree(last_img->data);
  }
  parsing_done = true;
  parse_thread.join();
  fclose(out);

  render_module.Exit();

  ImGuiSDL::Deinitialize();
  ImGui::DestroyContext();
  ImGui_ImplSDL2_Shutdown();

  std::cerr << "DONE." << std::endl;
  return 0;
}
