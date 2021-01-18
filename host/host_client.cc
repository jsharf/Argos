#include "host/netpipe/tcp.h"
#include "host/cam_parser.h"
#include "linux_sdl/include/SDL.h"
#include "graphics/sdl_canvas.h"
#include "imgui_sdl/imgui_sdl.h"
#include "dear_imgui/imgui.h"
#include "dear_imgui/examples/imgui_impl_sdl.h"
#include "libjpeg_turbo/turbojpeg.h"
#include "include/yolo_v2_class.hpp"

#include <unistd.h>
#include <atomic>
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cctype>
#include <ctime>
#include <fstream>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>

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

inline constexpr char kSaveDirectoryPrefix[] = "/home/sharf/argos_data/";
inline constexpr char kWeightFile[] = "host/yolov4.weights";
inline constexpr char kConfigFile[] = "host/yolov4.cfg";
inline constexpr char kObjectIdsFile[] = "external/darknet/data/coco.names";

bool file_exists(const std::string &path) {
  std::ifstream f(path.c_str());
  return f.good();
}

std::unique_ptr<std::unordered_map<int, std::string>> LoadObjectIds(const std::string &filepath) {
    std::cout << "Loading object ids from file " << filepath << std::endl;
    std::ifstream object_file(filepath.c_str());
    if (!object_file.good()) {
      std::cerr << "Invalid object ID file: " << filepath << std::endl;
      std::exit(1);
    }
    std::string line;
    auto object_ids = std::make_unique<std::unordered_map<int, std::string>>();
    int id = 0;
    while (std::getline(object_file, line)) {
      line.erase(std::remove_if(line.begin(), line.end(), ::isspace), line.end());
      (*object_ids)[id++] = line;
      std::cout << line << std::endl;
    }
    return object_ids;
}

std::string ObjIdToString(const int obj_id) {
  static std::unique_ptr<std::unordered_map<int, std::string>> obj_ids;
  if (!obj_ids) {
    obj_ids = LoadObjectIds(kObjectIdsFile);
  }

  if (obj_ids->count(obj_id) != 0) {
    return obj_ids->at(obj_id);
  }

  return "invalid";
}

int CalculateFrameStart(const std::string &prefix) {
  int frame_index = 1;
  while (file_exists(std::string(kSaveDirectoryPrefix) + prefix + "_" + std::to_string(frame_index) + ".jpg")) {
    frame_index++;
  }
  return frame_index;
}

Jpeg decode_jpeg(uint8_t *data, size_t bytes) {
  tjhandle operation = tjInitDecompress();
  if (operation == nullptr) {
    return {0, 0, 0, 0, nullptr, 0};
  }
  Jpeg jpeg;
  if (tjDecompressHeader3(operation, data, bytes, &jpeg.width, &jpeg.height, &jpeg.subsample, &jpeg.colorspace) < 0) {
    tjDestroy(operation);
    return {0, 0, 0, 0, nullptr, 0};
  }

  // Allocate the JPEG buffer. 24-bit colorspace.
  int pixelFormat = TJPF_RGB;
  jpeg.data = (uint8_t *) tjAlloc(jpeg.width * jpeg.height * tjPixelSize[pixelFormat]);
  jpeg.data_size = jpeg.width * jpeg.height * tjPixelSize[pixelFormat];
  if (tjDecompress2(operation, data, bytes, jpeg.data, jpeg.width, 0, jpeg.height,
                      pixelFormat, kDecompressionFlags) < 0) {
    tjDestroy(operation);
    return {0, 0, 0, 0, nullptr, 0};
  }
  tjDestroy(operation);
  return jpeg;
}

class ImageProcessingModule {
  public:
    ImageProcessingModule() : detector_(kConfigFile, kWeightFile) {
    }
  void InputImage(uint8_t *image, int size_x, int size_y) {
    std::lock_guard<std::mutex> lock(control_lock_);
    if (latest_image_.data != nullptr) {
      delete[] latest_image_.data;
      latest_image_.data = nullptr;
    }
    latest_image_ = {size_y, size_x, 3, nullptr};
    latest_image_.data = new float[size_x * size_y * 3];  // Each pixel is 3 bytes.

    for (int i = 0; i < size_y; i++) {
      for (int j = 0; j < size_x; j++) {
        for (int k = 0; k < 3; k++) {
          const int darknet_index = k * size_y * size_x + (i * size_x + j);
          const int source_index = (i * size_x + j) * 3 + k;
          latest_image_.data[darknet_index] = static_cast<float>(image[source_index]) / 255.0f;
        }
      }
    }
  }
  void operator()() {
    while (true) {
      usleep(10);
      {
        std::lock_guard<std::mutex> lock(control_lock_);
        if (done_) {
          // Cleanup and exit.
          if (latest_image_.data != nullptr) {
            delete[] latest_image_.data;
            latest_image_.data = nullptr;
          }
          return;
        }
      }
      if (latest_image_.data != nullptr) {
        std::cout << "Calling detect!" << std::endl;
        const auto image = latest_image_;
        const auto boxes = detector_.detect(image);
        {
          std::lock_guard<std::mutex> lock(box_lock_);
          untracked_objects_.clear();
          objects_.clear();
          for (size_t i = 0; i < boxes.size(); ++i) {
            if (boxes[i].track_id == 0) {
              untracked_objects_.push_back(boxes[i]);
              continue;
            }
            objects_[boxes[i].track_id] = boxes[i];
          }
        }
        std::cout << "DONE." << std::endl;
      }
    }
  }
  // Returns a map from persistent tracking ID -> object.
  std::unordered_map<int, bbox_t> objects() {
    std::lock_guard<std::mutex> lock(box_lock_);
    return objects_;
  }
  std::vector<bbox_t> untracked_objects() {
    std::lock_guard<std::mutex> lock(box_lock_);
    return untracked_objects_;
  }
  bool done() { 
    std::lock_guard<std::mutex> lock(control_lock_);
    return done_;
  }
  void Exit() {
    std::lock_guard<std::mutex> lock(control_lock_);
    if (latest_image_.data != nullptr) {
      delete[] latest_image_.data;
      latest_image_.data = nullptr;
    }
    done_ = true;
  }
  private:
    std::mutex control_lock_;
    std::mutex box_lock_;
    bool done_ = false;
    // uint8_t *data, int size_x, int size_y.
    image_t latest_image_ = {0, 0, 0, nullptr};
    std::unordered_map<int, bbox_t> objects_;
    std::vector<bbox_t> untracked_objects_;
    Detector detector_;
};

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
        usleep(1000);  // 1 ms.
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
          // for (size_t i = 0; i < targets_.size(); ++i) {
          //   canvas_.DrawPointAtPixel(targets_[i].y,
          //                            targets_[i].x);
          // }
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
        ImGui::Text("Objects detected: %lu", targets_.size());
        // Render target squares.
        for (size_t i = 0; i < targets_.size(); ++i) {
          ImGui::Text("%s at (%i, %i).", ObjIdToString(targets_[i].obj_id).c_str(),
                      targets_[i].x,
                      targets_[i].y);
        }
        ImGui::End();
        // End of ImGui UI definition.

        ImGui::Render();
        ImGuiSDL::Render(ImGui::GetDrawData());
        canvas_.Render();
    }
  }

  void SetBGImage(uint8_t *image, int image_size) {
    std::lock_guard<std::mutex> lock(control_lock_);
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
    if (bg_texture_!= nullptr) {
      SDL_DestroyTexture(bg_texture_);
    }
    bg_texture_ = texture;
    SDL_FreeSurface(surface);

    // Measure how frequently we're receiving BG images to calculate framerate.
    auto video_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> video_dt = video_time - previous_video_time_;
    if (video_dt.count() != 0) {
      video_framerate_ = 1 / (video_dt.count());
    }
    previous_video_time_ = video_time;
  }

  void SetObjectsDetected(std::vector<bbox_t> objects) {
    std::lock_guard<std::mutex> lock(control_lock_);
    targets_ = objects;
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
    std::vector<bbox_t> targets_;

    std::chrono::high_resolution_clock::time_point previous_video_time_;
    std::chrono::high_resolution_clock::time_point previous_render_time_; 
    double video_framerate_;
};

int main(int argc, char *argv[]) {
  if (argc < 3) {
    std::cerr << "Usage: host_client server_ip server_port [file_prefix]." << std::endl;
    return -1;
  }
  SDL_LogSetAllPriority(SDL_LOG_PRIORITY_VERBOSE);

  // SDL scene.
  const int width = 800;
  const int height = 600;

  const std::string kFilePrefix = (argc == 4) ? argv[3] : "";

  RenderThread render_module(width, height);
  auto render_future = std::async(std::launch::async, [&render_module](){render_module();});
  
  ImageProcessingModule image_processing;
  std::thread image_processing_thread([&image_processing]() {image_processing();});

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
  
  // Video output information.
  constexpr int kSecondsPerFrame = 1;
  auto last_jpeg_time = std::chrono::high_resolution_clock::now();
  int frame_count = CalculateFrameStart(kFilePrefix);
  std::cout << "Starting frame @ " << frame_count << std::endl;

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
    int data_len = socket.Read(recv_buffer, sizeof(recv_buffer)-1);
    if (data_len == -1) {
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
      auto now = std::chrono::high_resolution_clock::now();
      if (now - last_jpeg_time > std::chrono::seconds(kSecondsPerFrame)) {
        // Save a frame.
        if (!kFilePrefix.empty()) {
          std::ofstream frame_file;
          time_t rawtime;
          time(&rawtime);
          const struct tm *timeinfo = localtime(&rawtime);
          char buffer[80];
          strftime(buffer, sizeof(buffer), "%a_%b_%d_%T_%Z_%Y", timeinfo);
          const std::string filename = "/home/sharf/argos_data/" + kFilePrefix + "_" + std::to_string(frame_count) + "_" + std::string(buffer) + ".jpg";
          std::cout << "Writing frame: " << filename << std::endl;
          frame_count++;
          frame_file.open(filename, std::ios::out | std::ios::binary);
          frame_file.write(reinterpret_cast<const char *>(jpeg_buffer), bytes_read);
          frame_file.close();
        }
        last_jpeg_time = now;
      }
    }
    if (pending_img.valid()) {
      if (last_img) {
        tjFree(last_img->data);
      }
      auto image = std::make_unique<Jpeg>(pending_img.get());
      if (image->data != nullptr) {
        last_img = std::move(image);
        render_module.SetBGImage(last_img->data, last_img->data_size);
        if (last_img->data_size != width * height * 3) {
          std::cerr << "Could not run classifier as image did not fit the expected resolution.";
          continue;
        }
        image_processing.InputImage(last_img->data, width, height);
        const auto tracked_objects = image_processing.objects();
        const auto untracked_objects = image_processing.untracked_objects();
        std::vector<bbox_t> objects;
        for (const auto & [id, obj] : tracked_objects) {
          objects.push_back(obj);
        }
        for (const auto &obj : untracked_objects) {
          objects.push_back(obj);
        }
        if (objects.size() != 0) {
          // do something with render_module and objects.
          render_module.SetObjectsDetected(objects);
        }
      }
    }
  }
  std::cout << "EXITED NORMALLY" << std::endl;

  if (last_img) {
    //tjFree(last_img->data);
  }
  parsing_done = true;
  parse_thread.join();
  fclose(out);

  image_processing.Exit();
  render_module.Exit();

  image_processing_thread.join();

  ImGuiSDL::Deinitialize();
  ImGui::DestroyContext();
  ImGui_ImplSDL2_Shutdown();

  std::cerr << "DONE." << std::endl;
  return 0;
}
