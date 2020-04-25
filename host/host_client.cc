#include "host/netpipe/tcp.h"
#include "host/cam_parser.h"

#include <unistd.h>

#include <atomic>
#include <cassert>
#include <thread>

int main(int argc, char *argv[]) {
  if (argc != 3) {
    std::cerr << "Usage: host_client server_ip server_port." << std::endl;
    return -1;
  }

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
      std::cerr << "Image is available. Retreiving..." << std::endl;
      int bytes_read = http_parser.RetrieveJpeg(recv_buffer, sizeof(recv_buffer));
      while (bytes_read != 0) {
        int bytes = fwrite(recv_buffer, 1, data_len, out);
        if (bytes < data_len) {
          std::cerr << "Bytes lost from image!" << std::endl;
        }
        bytes_read = http_parser.RetrieveJpeg(recv_buffer, sizeof(recv_buffer));
      }
    }
    usleep(10);
  }
  parsing_done = true;
  parse_thread.join();
  fclose(out);

  std::cerr << "DONE." << std::endl;
  return 0;
}
