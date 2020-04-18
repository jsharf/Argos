#include "host/cam_parser.h"

#include <inttypes.h>
#include <stdio.h>
#include <cassert>

namespace cam {

void CamParser::InsertBinary(const uint8_t *data, size_t len) {
  std::lock_guard<std::mutex> guard(lock_);
  in_buffer_.insert(v.end(), data, data + len);
}

bool CamParser::IsImageAvailable() const {
  std::lock_guard<std::mutex> guard(lock_);
  return images_.size() != 0;
}

bool CamParser::HttpResponseConsumed() {
  std::vector<uint8_t> line;
  if (!ConsumeHeaderLine(&line)) {
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
  if (!ConsumeHeaderLine(&line)) {
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
  if (!ConsumeHeaderLine(&line)) {
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
    // If there's no more lines available in the current chunk, mark it as
    // invalid and wait for a new chunk.
    std::cerr << "Was looking for separator, but couldn't find in current "
                 "chunk. Waiting for next chunk. This should never happen."
              << std::endl;
    chunk_.reset();
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
    // If there's no more lines available in the current chunk, mark it as
    // invalid and wait for a new chunk.
    std::cerr << "Was looking for JpegContentType, but couldn't find in current "
                 "chunk. Waiting for next chunk. This should never happen."
              << std::endl;
    chunk_.reset();
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
    // If there's no more lines available in the current chunk, mark it as
    // invalid and wait for a new chunk.
    std::cerr << "Was looking for ContentLength, but couldn't find in current "
                 "chunk. Waiting for next chunk. This should never happen."
              << std::endl;
    chunk_.reset();
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
  std::vector<uint8_t> end_of_header = {0xd, 0xa, 0xd, 0xa};
  auto iter = std::search(in_buffer_.begin(), in_buffer_.end(), end_of_header.begin(), end_of_header.end());
  if (iter == in_buffer_.end()) {
    return false;
  }

  // Now that we've extracted the bytes, suck them out of in_buffer_.
  in_buffer_.remove(in_buffer_.begin(), iter + 4);
  return true;
}

bool CamParser::EndOfMultipartHeaderConsumed() {
  std::vector<uint8_t> end_of_header = {0xd, 0xa, 0xd, 0xa};
  auto iter = std::search(chunk_.begin(), chunk_.end(), end_of_header.begin(), end_of_header.end());
  if (iter == chunk_.end()) {
    return false;
  }

  // Now that we've extracted the bytes, suck them out of in_buffer_.
  chunk_.remove(chunk_.begin(), iter + 4);
  return true;
}

bool CamParser::JpegConsumed() {
  if (chunk_.size() < parsed_.jpeg_length) {
    std::cerr << "Chunk received is much smaller than expected JPEG image. "
                 "This shouldn't really happen. Waiting for next chunk."
              << std::endl;"
    chunk_.reset();
    return false;
  }
  images_.push_back({
      /*image=*/{chunk_.begin(), chunk_.begin() + parsed_.jpeg_length},
      /*size=*/parsed_.jpeg_length,
      /*index=*/0
  });
  chunk_.remove(chunk_.begin(), chunk_.begin() + parsed_.jpeg_length);
  return true;
}

bool CamParser::ConsumeHeaderLine(std::vector<uint8_t>& value) {
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

bool CamParser::ConsumeLine(std::vector<uint8_t>& value) {
  // Look for \r.
  auto iter = std::find(chunk_.begin(), chunk_.end(), '\r');
  if (iter == chunk_.end()) {
    return false;
  }
  // Look for \n after the \r.
  iter++;
  if ((iter == chunk_.end()) || (*iter != '\n')) {
    return false;
  }

  // The + 1 at the end is so that the newline gets inserted into value.
  value.insert(value.end(), chunk_.begin(), iter + 1); 
  // Now that we've extracted the bytes, suck them out of in_buffer_.
  chunk_.remove(chunk_.begin(), iter + 1);
  return true;
}

bool CamParser::WaitingForChunk() {
  switch (state_) {
    case HTTP_RESPONSE:
    case MULTIPART:
    case FRAMERATE:
    case END_OF_HEADER:
      return false;
    default:
      break;
  }

  if (next_chunk_size_ != -1) {
    if (in_buffer_.size() < next_chunk_size_) {
      return true;
    }

    if (next_chunk_size_ == 0) {
      state_ = ZERO_CHUNK_FOUND;
      return false;
    }

    // Move to chunk_, clear in_buffer_, and return false.
    chunk_.resize(next_chunk_size_);
    auto chunk_end = in_buffer_.begin() + next_chunk_size_;
    std::copy(in_buffer_.begin(), chunk_end, chunk_.begin());
    in_buffer_.remove(in_buffer_.begin(), chunk_end);
    next_chunk_size_ = -1;
    return false;
  }

  // \r\n is expected at the beginning of all chunks (except the first).
  auto iter = in_buffer_.begin();
  if (*iter == '\r') {
    iter++;
    if (*iter != '\n')  {
      // Invalid byte. Error and drop byte.
      std::cerr << "Invalid byte found at beginning of chunk size. Expected: "
                   "\r\n, Got: \r" +
                       static_cast<char>(*iter)
                << std::endl;
    }
    iter++;
  }

  // Consume chunk size. Interpret in hex.
  auto end_of_size = std::find(iter, in_buffer_.end(), '\r');
  if ((end_of_size == in_buffer_.end()) || (end_of_size + 1 == in_buffer_.end())) {
    // Not enough bytes yet. Wait for chunk.
		return true;
  }
  if (*(end_of_size + 1) != '\n') {
    // Log an error,
    std::cerr
        << "Invalid byte found at end of chunk size. Expected: \r\n, Got: \r" +
               static_cast<char>(*(end_of_size + 1))
        << std::endl;
  }
	size_t size_of_size = end_of_size - iter;
  // If the size field is really big, then just fail because that's too much.	
	assert(size_of_size < 128);
	std::vector<uint8_t> size_hex({iter, end_of_size});
  // Add a null byte to make it a proper C string. I wish C++ included a
  // standard function like strntol so that we didn't need to do this. Instead
  // all we get is the unsafe strtol, which forces you to use C-strings.
  size_hex.push_back(0);

  int chunk_size = strtol(reinterpret_cast<char *>(size_hex.data()), nullptr, 16);
  next_chunk_size_ = chunk_size;
  in_buffer_.remove(in_buffer_.begin(), end_of_size + 2);
  return true;
}

// Call to drive parsing.
bool CamParser::Poll() {
  if (chunk_.size() == 0) {
    if (WaitingForChunk()) {
      return true;
    }
  }
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
        state_ = END_OF_HEADER;
      }
      break;
    case END_OF_HEADER:
      if (EndOfHeaderConsumed()) {
        state_ = SEPARATOR;
        chunk_.reset();
      }
      break;
    case SEPARATOR:
      if (SeparatorConsumed()) {
        state_ = JPEG_CONTENT_TYPE;
        chunk_.reset();
      }
      break;
    case JPEG_CONTENT_TYPE:
      if (JpegContentTypeConsumed()) {
        state_ = CONTENT_LENGTH;
      }
      break;
    case CONTENT_LENGTH:
      if (ContentLengthConsumed()) {
        state_ = END_OF_MULTIPART_HEADER;
      }
      break;
    case END_OF_MULTIPART_HEADER:
      if (EndOfMultipartHeaderConsumed())  {
        state_ = CONSUME_JPEG;
        chunk_.reset();
      } else {
        // If there's no more lines available in the current chunk, mark it as
        // invalid and wait for a new chunk.
        std::cerr << "Was looking for EndOfMultipartHeader, but couldn't find "
                     "in current chunk. Waiting for next chunk. This should "
                     "never happen."
                  << std::endl;
        chunk_.reset();
      }
      break;
    case CONSUME_JPEG:
      if (JpegConsumed()) {
        // Loop back to looking for the next separator.
        state_ = SEPARATOR;
      }
      break;
    case ZERO_CHUNK_FOUND:
      state_ = HTTP_RESPONSE;
      std::cerr << "ZERO_CHUNK_FOUND!" << std::endl;
      return false;
  }
  return true;
}

// Must be called until returns a value < len in order to confirm image has
// been fully retrieved. Image will be in JPEG binary format.
size_t CamParser::RetrieveJpeg(uint8_t *data, size_t len) {
  std::lock_guard<std::mutex> guard(lock_);
  if (images_.size() == 0) {
    // No image, return false.
    return 0;
  }
  size_t bytes_remaining = images_.front().size - images_.front().index;
  if (bytes_remaining == 0) {
    // Clear out this image and return 0.
    images_.pop();
    return 0;
  }
  size_t bytes = min(len, bytes_remaining);
  memcpy(data, images_.front().image.data() + images_.front().index, bytes);
  images_.front().index += bytes;
  return bytes;
}

}  // namespace cam
