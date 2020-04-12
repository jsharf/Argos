#include "host/netpipe/tcp.h"

#include <cassert>

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

  for (int i = 0; i < 20000; ++i) {
    uint8_t recv_buffer[256];
    int data_len = socket.Read(recv_buffer, sizeof(recv_buffer)-1);
    if (data_len == -1) {
      std::cout << "\nDone!" << std::endl;
      return 0;
    }
    recv_buffer[data_len] = '\0';

    int bytes_written = 0;
    while (bytes_written < data_len) {
      int bytes =
          fwrite(recv_buffer + bytes_written, 1, data_len - bytes_written, out);
      if (bytes < 0) {
        std::cerr << "Error writing output." << std::endl;
        fclose(out);
        return -1;
      }
      bytes_written += bytes;
    }
    usleep(100);
  }

  fclose(out);
  return 0;
}
