#include "host/netpipe/tcp.h"
#include "host/cam_parser.h"
#include "linux_sdl/include/SDL.h"
#include "graphics/sdl_canvas.h"
#include "imgui_sdl/imgui_sdl.h"
#include "dear_imgui/imgui.h"
#include "dear_imgui/examples/imgui_impl_sdl.h"
#include "libjpeg_turbo/turbojpeg.h"
#include "plasticity/nnet/nnet.h"
#include "plasticity/compute/cl_buffer.h"

#include <unistd.h>
#include <atomic>
#include <cassert>
#include <chrono>
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

constexpr size_t kInputSize = 32 * 32 * 3;
constexpr size_t kOutputSize = 10;

enum Label : uint8_t {
  AIRPLANE = 0,
  AUTOMOBILE,
  BIRD,
  CAT,
  DEER,
  DOG,
  FROG,
  HORSE,
  SHIP,
  TRUCK,
  UNKNOWN
};

std::string LabelToString(uint8_t label) {
  switch (label) {
    case AIRPLANE:
      return "Airplane";
    case AUTOMOBILE:
      return "Automobile";
    case BIRD:
      return "Bird";
    case CAT:
      return "Cat";
    case DEER:
      return "Deer";
    case DOG:
      return "Dog";
    case FROG:
      return "Frog";
    case HORSE:
      return "Horse";
    case SHIP:
      return "Ship";
    case TRUCK:
      return "Truck";
    case UNKNOWN:
    default:
      return "Unknown";
  }
}

static constexpr float kClassifierThreshold = 0.5;
Label OneHotEncodedOutputToEnum(
    const std::unique_ptr<compute::ClBuffer> &buffer) {
  buffer->MoveToCpu();
  uint8_t max_index = 0;
  for (size_t index = 0; index < buffer->size(); ++index) {
    if (buffer->at(index) > buffer->at(max_index)) {
      max_index = index;
    }
  }

  // If the answer isn't confident, return UNKNOWN.
  if (buffer->at(max_index) < kClassifierThreshold) {
    return UNKNOWN;
  }

  return static_cast<Label>(max_index);
}

void NormalizeInput(const std::unique_ptr<compute::ClBuffer> &buffer) {
  buffer->MoveToCpu();
  double norm = 0;
  for (size_t i = 0; i < buffer->size() ; ++i) {
    norm += static_cast<double>(buffer->at(i)) * buffer->at(i);
  }
  norm = sqrt(norm);
  if (norm == 0) {
    norm = 1;
  }
  for (size_t i = 0; i < kInputSize; ++i) {
    buffer->at(i) = static_cast<double>(buffer->at(i)) / norm;
  }
}

// Flags to use during decompression. Options include:
// TJFLAG_FASTDCT
// TJFLAG_ACCURATEDCT
// TJFLAG_FASTUPSAMPLE
static constexpr int kDecompressionFlags = TJFLAG_ACCURATEDCT;

inline constexpr char kSaveDirectoryPrefix[] = "/home/sharf/argos_data/";
inline constexpr char kWeightFile[] = "host/network_weights.json";

bool file_exists(const std::string &path) {
  std::ifstream f(path.c_str());
  return f.good();
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
    // An object identified in the latest image. Size is assumed to be 32x32
    // pixels.
    struct Target {
      Label label;
      std::pair<int, int> coordinate;
    };

    ImageProcessingModule() {
      nnet::Architecture model(kInputSize);
      model
          .AddConvolutionLayer(
              {
                  32,  // width
                  32,  // height
                  3,   // R,G,B (depth).
              },
              {
                  5,   // filter x size.
                  5,   // filter y size.
                  3,   // filter z depth size.
                  1,   // stride.
                  2,   // padding.
                  16,  // number of filters.
              })
          .AddMaxPoolLayer(
              /* Input size */ nnet::VolumeDimensions{32, 32, 16},
              /* Output size */ nnet::AreaDimensions{16, 16})
          .AddConvolutionLayer(
              {
                  16,  // width
                  16,  // height
                  16,  // R,G,B (depth).
              },
              {
                  5,   // filter x size.
                  5,   // filter y size.
                  16,  // filter z depth size.
                  1,   // stride.
                  2,   // padding.
                  20,  // number of filters.
              })
          .AddMaxPoolLayer(
              /* Input size */ nnet::VolumeDimensions{16, 16, 20},
              /* output size */ nnet::AreaDimensions{8, 8})
          .AddConvolutionLayer(
              {
                  8,   // width
                  8,   // height
                  20,  // R,G,B (depth).
              },
              {
                  5,   // filter x size.
                  5,   // filter y size.
                  20,  // filter z depth size.
                  1,   // stride.
                  2,   // padding.
                  20,  // number of filters.
              })
          .AddMaxPoolLayer(/* Input size */ {8, 8, 20},
                           /* output size */ {4, 4})
          // No activation function, the next layer is softmax which functions as an
          // activation function
          .AddDenseLayer(10, symbolic::Identity)
          .AddSoftmaxLayer(10);
      classifier = std::make_unique<nnet::Nnet>(model, nnet::Nnet::Xavier, nnet::CrossEntropy);
      // Load weights.
      std::ifstream weight_file(kWeightFile);
      std::stringstream buffer;
      buffer << weight_file.rdbuf();
      std::string weight_string = buffer.str();
      if (weight_string.empty()) {
        std::cout
            << "Provided weight file is empty. Initializing with random weights"
            << std::endl;
      } else {
        if (!classifier->LoadWeightsFromString(weight_string)) {
          std::cerr << "Failed to load weights from file: " << kWeightFile << std::endl;
          std::exit(1);
        }
      }
    }
  void InputImage(uint8_t *image, int size_x, int size_y) {
    // This is actually a race condition since the calling thread might
    // deallocate image* before we're done using it. Modify this function to
    // make a copy.
    std::lock_guard<std::mutex> lock(control_lock_);
    if (std::get<0>(latest_image_) != nullptr) {
      delete[] std::get<0>(latest_image_);
    }
    latest_image_ = std::make_tuple(nullptr, size_x, size_y);
    std::get<0>(latest_image_) = new uint8_t[size_x * size_y * 3];  // Each pixel is 3 bytes.
    memcpy(std::get<0>(latest_image_), image, size_x * size_y * 3);
    latest_targets_.clear();
  }
  void operator()() {
    while (true) {
      usleep(10);
      std::lock_guard<std::mutex> lock(control_lock_);
      if (done_) {
        return;
      }
      if (std::get<0>(latest_image_) != nullptr) {
        ScanLatestImage();
      }
    }
  }
  bool targets_available() {
    std::lock_guard<std::mutex> lock(control_lock_);
    return !latest_targets_.empty();
  }
  std::vector<Target> targets() {
      std::lock_guard<std::mutex> lock(control_lock_);
      std::vector<Target> targets_copy = latest_targets_;
      latest_targets_.clear();
      return targets_copy;
  }

  bool done() { 
    std::lock_guard<std::mutex> lock(control_lock_);
    return done_;
  }
  void Exit() {
    std::lock_guard<std::mutex> lock(control_lock_);
    if (std::get<0>(latest_image_) != nullptr) {
      delete[] std::get<0>(latest_image_);
    }
    done_ = true;
  }
  private:
    // Scan through the latest image, searching a 32x32 pixel box
    // for potential targets (with 16-pixel overlap between each successive
    // box).
    void ScanLatestImage() {
      for (int x = 0; x < std::get<1>(latest_image_); x+= 16) {
        for (int y = 0; y < std::get<2>(latest_image_); y+= 16) {
          std::unique_ptr<compute::ClBuffer> input = classifier->MakeBuffer(kInputSize);
          PopulateSlice(input, x, y, 32, 32);
          NormalizeInput(input);
          std::unique_ptr<compute::ClBuffer> output = classifier->Evaluate(input);
          Label label = OneHotEncodedOutputToEnum(output);
          if (label != UNKNOWN) {
            latest_targets_.push_back({label, std::make_pair(x, y)});
          }
        }
      }
      // Delete the image.
      delete[] std::get<0>(latest_image_);
      std::get<0>(latest_image_) = nullptr;
    }

    void PopulateSlice(const std::unique_ptr<compute::ClBuffer> &buffer, int x, int y, int width, int height) {
      // The input is RGB, but the classifier expects images in
      // <R-channel><G-channel><B-channel>, so we'll need to de-interlace all
      // the R pixels, G pixels and B pixels to make separate images.
      assert(buffer->size() == static_cast<size_t>(width * height * 3));

      const int g_offset = width * height;
      const int b_offset = width * height * 2;

      uint8_t *image_data = std::get<0>(latest_image_);

      for (int i = 0; i < width; ++i) {
        for (int j = 0; j < height; ++j) {
          // Calculate the index in the raw image. This means *3 since RGB are
          // interlaced and offset by (x,y).
          int image_index = 3 * (i + x + (j + y) * width);
          // R-channel.
          buffer->at(i + j * width) = image_data[image_index + 0];
          // G-channel.
          buffer->at(i + j * width + g_offset) = image_data[image_index + 1];
          // B-channel.
          buffer->at(i + j * width + b_offset) = image_data[image_index + 2];
        }
      }
    }

    std::mutex control_lock_;
    bool done_ = false;
    // uint8_t *data, int size_x, int size_y.
    std::tuple<uint8_t *, int, int> latest_image_;
    std::vector<Target> latest_targets_;
    std::unique_ptr<nnet::Nnet> classifier;
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
          for (size_t i = 0; i < targets_.size(); ++i) {
            canvas_.DrawPointAtPixel(targets_[i].coordinate.second,
                                     targets_[i].coordinate.first);
          }
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
          ImGui::Text("%s at (%i, %i).", LabelToString(targets_[i].label).c_str(),
                      targets_[i].coordinate.first,
                      targets_[i].coordinate.second);
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

  void SetTargets(const std::vector<ImageProcessingModule::Target> &target) {
    std::lock_guard<std::mutex> lock(control_lock_);
    targets_ = target;
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
    std::vector<ImageProcessingModule::Target> targets_;

    std::chrono::high_resolution_clock::time_point previous_video_time_;
    std::chrono::high_resolution_clock::time_point previous_render_time_; 
    double video_framerate_;
};

int main(int argc, char *argv[]) {
  if (argc < 3) {
    std::cerr << "Usage: host_client server_ip server_port [file_prefix]." << std::endl;
    return -1;
  }

  // SDL scene.
  const int width = 1280;
  const int height = 720;

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
      }
    }

    if (image_processing.targets_available()) {
      std::vector<ImageProcessingModule::Target> targets = image_processing.targets();
      render_module.SetTargets(targets);
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
