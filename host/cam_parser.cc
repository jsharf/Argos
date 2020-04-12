#ifndef CAM_PARSER_H
#define CAM_PARSER_H

#include <algorithm>
#include <thread>
#include <mutex>
#include <vector>

namespace cam {

// This class is threadsafe, so you can create another thread in the background
// which calls Poll() a lot.
class CamParser {
  public:
    CamParser(){}

    void InsertBinary(const uint8_t *data, size_t len);
    bool IsImageAvailable() const;

    // Call to drive parsing.
    void Poll();

    // Must be called until returns a value < len in order to confirm image has
    // been fully retrieved. Image will be in the format RGB.
    size_t RetrieveImage(uint8_t *data, size_t len);

  private:
    enum {
      HTTP_RESPONSE = 0,
      MULTIPART,
      FRAMERATE,
      SEPARATOR,
      JPEG_CONTENT_TYPE,
      CONTENT_LENGTH,
      END_OF_HEADER,
      CONSUME_JPEG,
    } state_;

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
    bool JpegConsumed();

    bool ConsumeLine();
    void RewindToSeparator();

    std::mutex lock_;

    uint8_t *image_ = nullptr;
    size_t image_len_ = 0;
    size_t image_index_ = 0;

    std::deque<uint8_t> in_buffer_;
};

}  // namespace cam

#endif // CAM_PARSER_H
