#ifndef CAM_PARSER_H
#define CAM_PARSER_H

#include <algorithm>
#include <thread>
#include <mutex>
#include <vector>
#include <queue>
#include <deque>


namespace cam {

// This class is threadsafe, so you can create another thread in the background
// which calls Poll() a lot.
class CamParser {
  public:
    CamParser() : state_(HTTP_RESPONSE), parsed_{} {}

    void InsertBinary(const uint8_t *data, size_t len);
    bool IsImageAvailable();

    // Call to drive parsing.
    bool Poll();

    // Must be called until returns a value < len in order to confirm image has
    // been fully retrieved. Image will be in the format RGB.
    size_t RetrieveJpeg(uint8_t *data, size_t len);

    size_t ImagesAvailable() const { return images_.size(); }

  private:
    enum {
      HTTP_RESPONSE = 0,
      MULTIPART,
      FRAMERATE,
      END_OF_HEADER,
      SEPARATOR,
      JPEG_CONTENT_TYPE,
      CONTENT_LENGTH,
      END_OF_MULTIPART_HEADER,
      CONSUME_JPEG,
      ZERO_CHUNK_FOUND,
    } state_;

    // -1 means chunk size hasn't been parsed yet.
    int next_chunk_size_ = -1;

    struct {
      int status_code;
      char boundary[256];
      int frame_rate;
      int jpeg_length;
    } parsed_;

    bool HttpResponseConsumed();
    bool MultipartConsumed();
    bool FramerateConsumed();
    bool SeparatorConsumed();
    bool JpegContentTypeConsumed();
    bool ContentLengthConsumed();
    bool EndOfHeaderConsumed();
    bool EndOfMultipartHeaderConsumed();
    bool JpegConsumed();

    bool WaitingForChunk();

    bool ConsumeHeaderLine(std::vector<uint8_t> *value);
    bool ConsumeLine(std::vector<uint8_t> *value);
    void RewindToSeparator();

    std::mutex lock_;

    struct Image {
      std::vector<uint8_t> image;
      size_t size;
      size_t index;
    };
    std::queue<Image> images_;

    std::deque<uint8_t> in_buffer_;
    std::vector<uint8_t> chunk_;
};

}  // namespace cam

#endif // CAM_PARSER_H
