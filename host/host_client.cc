#include "host/netpipe/tcp.h"
#include "host/cam_parser.h"
#include "external/linux_sdl/include/SDL.h"
#include "external/graphics/sdl_canvas.h"
#include "external/imgui_sdl/imgui_sdl.h"
#include "external/dear_imgui/imgui.h"
#include "external/dear_imgui/examples/imgui_impl_sdl.h"

#include <unistd.h>
#include <atomic>
#include <cassert>
#include <thread>

int main(int argc, char *argv[]) {
  if (argc != 3) {
    std::cerr << "Usage: host_client server_ip server_port." << std::endl;
    return -1;
  }

  // SDL scene.
  const int width = 800;
  const int height = 600;
  SdlCanvas canvas(width, height);
  auto *renderer = canvas.renderer();
  auto *window = canvas.window();

  ImGui::CreateContext();
	ImGuiSDL::Initialize(renderer, 800, 600);
  ImGui_ImplSDL2_InitForOpenGL(window, nullptr);

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
    if (http_parser.IsImageAvailable()) {
      std::cerr << "Image is available" << std::endl;
      int bytes_read = http_parser.RetrieveJpeg(recv_buffer, sizeof(recv_buffer));
      // std::pair<uint8_t *, size_t> buffer = decode_jpeg(recv_buffer, bytes_read);

      //while (bytes_read != 0) {
      //  int bytes = fwrite(recv_buffer, 1, data_len, out);
      //  if (bytes < data_len) {
      //    std::cerr << "Bytes lost from image!" << std::endl;
      //  }
      //  bytes_read = http_parser.RetrieveJpeg(recv_buffer, sizeof(recv_buffer));
      //}
    }
    ImGui::NewFrame();
    ImGui_ImplSDL2_NewFrame(window);
    SDL_Event event;
    if (SDL_PollEvent(&event)) {
      ImGui_ImplSDL2_ProcessEvent(&event);
    }
    // ImGui UI defined here.
    ImGui::ShowDemoWindow();
    // End of ImGui UI definition.
    ImGui::Render();
    ImGuiSDL::Render(ImGui::GetDrawData());
    canvas.Render();
    usleep(10);
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
