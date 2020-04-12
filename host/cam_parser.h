#include "host/cam_parser.h"

#include <inttypes.h>
#include <stdio.h>

namespace cam {

void CamParser::InsertBinary(const uint8_t *data, size_t len) {
  std::lock_guard<std::mutex> guard(lock_);
  in_buffer_.insert(v.end(), data, data + len);
}

bool CamParser::IsImageAvailable() const {
  std::lock_guard<std::mutex> guard(lock_);
  return image_ != nullptr;
}

bool CamParser::HttpResponseConsumed() {
  std::vector<uint8_t> line;
  if (!ConsumeLine(&line)) {
    return false;
  }

  // This is to make sscanf happy -- it needs a null-terminated string.
  line.push_back('\0');

  int status_code = 0;
  int matched = std::sscanf(reinterpret_cast<const char *>(line.data()),
                            "HTTP/1.1 %i OK\r\n", &status_code);
  if ((matched == 0) || (status_code != 200)) {
    // No need to reset parser state since we're already at the beginning.
    return false;
  }
  parsed_.http_status = status_code;
  return true;
}

bool CamParser::MultipartConsumed() {
  std::vector<uint8_t> line;
  if (!ConsumeLine(&line)) {
    return false;
  }

  // This is to make sscanf happy -- it needs a null-terminated string.
  line.push_back('\0');

  int matched = std::sscanf(
      reinterpret_cast<const char *>(line.data()),
      "Content-Type: multipart/x-mixed-replace;boundary=%255s\r\n",
      response_.boundary);
  if ((matched == 0)) {
    return false;
  }
  return true;
}

bool CamParser::FramerateConsumed() {
  std::vector<uint8_t> line;
  if (!ConsumeLine(&line)) {
    return false;
  }

  // This is to make sscanf happy -- it needs a null-terminated string.
  line.push_back('\0');

  int matched = std::sscanf(reinterpret_cast<const char *>(line.data()),
                            "X-Framerate: %i\r\n", &response_.frame_rate);
  if ((matched == 0)) {
    return false;
  }
  return true;
}

bool CamParser::SeparatorConsumed() {
  std::vector<uint8_t> line;
  if (!ConsumeLine(&line)) {
    return false;
  }

  // This is to make sscanf happy -- it needs a null-terminated string.
  line.push_back('\0');

  char boundary[256];
  int matched = std::sscanf(reinterpret_cast<const char *>(line.data()),
                            "--%255s\r\n", boundary);
  return ((matched != 0) && (strncmp(boundary, parsed_.boundary, 256) == 0));
}

bool CamParser::JpegContentTypeConsumed() {
  std::vector<uint8_t> line;
  if (!ConsumeLine(&line)) {
    return false;
  }

  // This is to make sscanf happy -- it needs a null-terminated string.
  line.push_back('\0');

  char content_type[11];
  int matched = std::sscanf(reinterpret_cast<const char *>(line.data()),
                            "Content-Type: %10s\r\n", content_type);
  char expected_content_type[] = "image/jpeg";
  if (matched == 0) {
    return false;
  }
  if (strncmp(content_type, expected_content_type,
              sizeof(expected_content_type)) != 0) {
    // Rewind parser state -- let's just wait for the next separator.
    RewindToSeparator();
  }

  return true;
}

void CamParser::RewindToSeparator() {
  state_ = SEPARATOR;
  parsed_.jpeg_length = 0;
}

bool CamParser::ContentLengthConsumed() {
  std::vector<uint8_t> line;
  if (!ConsumeLine(&line)) {
    return false;
  }

  // This is to make sscanf happy -- it needs a null-terminated string.
  line.push_back('\0');

  int matched = std::sscanf(reinterpret_cast<const char *>(line.data()),
                            "Content-Length: %i\r\n", &parsed_.jpeg_length);
  if (matched == 0) {
    return false;
  }
  if (parsed_.jpeg_length < 0) {
    // Negative jpeg length is invalid. Rewind parser -- look for next
    // separator.
    RewindToSeparator();
    return false;
  }
  return ((matched != 0) && (parsed_.jpeg_length > 0));
}

bool CamParser::EndOfHeaderConsumed() {

}
bool CamParser::JpegConsumed() {}

bool CamParser::ConsumeLine(std::vector<uint8_t>& value) {
  // Look for \r.
  auto iter = std::find(in_buffer_.begin(), in_buffer_.end(), '\r');
  if (iter == in_buffer_.end()) {
    return false;
  }
  // Look for \n after the \r.
  iter++;
  if ((iter == in_buffer_.end()) || (*iter != '\n')) {
    return false;
  }

  // The + 1 at the end is so that the newline gets inserted into value.
  value.insert(value.end(), in_buffer_.begin(), iter + 1); 
  // Now that we've extracted the bytes, suck them out of in_buffer_.
  in_buffer_.remove(in_buffer_.begin(), iter + 1);
  return true;
}

// Call to drive parsing.
void CamParser::Poll() {
  std::string line;
  switch (state_) {
    case HTTP_RESPONSE:
      if (HttpResponseConsumed()) {
        state_ = MULTIPART;
      }
      break;
    case MULTIPART:
      if (MultipartConsumed()) {
        state_ = FRAMERATE;
      }
      break;
    case FRAMERATE:
      if (FramerateConsumed()) {
        state_ = SEPARTOR;
      }
      break;
    case SEPARATOR:
      if (SeparatorConsumed()) {
        state_ = JPEG_CONTENT_TYPE;
      }
      break;
    case JPEG_CONTENT_TYPE:
      if (JpegContentTypeConsumed()) {
        state_ = CONTENT_LENGTH;
      }
      break;
    case CONTENT_LENGTH:
      if (ContentLengthConsumed()) {
        state_ = END_OF_HEADER;
      }
      break;
    case END_OF_HEADER:
      if (EndOfHeaderConsumed())  {
        state_ = CONSUME_JPEG;
      }
      break;
    case CONSUME_JPEG:
      if (JpegConsumed()) {
        // Loop back to looking for the next separator.
        state_ = SEPARATOR;
      }
      break;
  }
}

// Must be called until returns a value < len in order to confirm image has
// been fully retrieved. Image will be in the format RGB.
size_t CamParser::RetrieveImage(uint8_t *data, size_t len) {
  std::lock_guard<std::mutex> guard(lock_);
  if (image_ == nullptr) {
    // No image, return false.
    return 0;
  }
  size_t bytes_remaining = image_len_ - image_index_;
  size_t bytes = min(len, bytes_remaining);
  memcpy(data, &image_[image_index_], bytes);
  image_index_ += bytes;
  if (image_index_ >= image_len_) {
    image_ == nullptr;
    image_index_ = 0;
    image_len_ = 0;
  }
  return bytes;
}

}  // namespace cam
