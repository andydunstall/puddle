#include "otter/server/reader.h"

#include <arpa/inet.h>

#include "absl/log/log.h"
#include "absl/strings/str_format.h"

namespace otter {
namespace server {

Reader::Reader(puddle::Reader* reader, size_t init_buf_size,
               size_t max_buf_size)
    : reader_{reader}, buf_(init_buf_size) {}

absl::StatusOr<uint16_t> Reader::ReadUint16() {
  if (pending_size() < sizeof(uint16_t)) {
    // If we don't have enough pending bytes, read enough to parse.
    absl::Status status = ReadAtLeast(sizeof(uint16_t) - pending_size());
    if (!status.ok()) {
      return status;
    }
  }

  // We now know we have at least sizeof(uint16_t) bytes.
  uint16_t n_net;
  memcpy(&n_net, buf_.data() + start_idx_, sizeof(uint16_t));
  start_idx_ += sizeof(uint16_t);
  return htons(n_net);
}

absl::StatusOr<uint32_t> Reader::ReadUint32() {
  if (pending_size() < sizeof(uint32_t)) {
    // If we don't have enough pending bytes, read enough to parse.
    absl::Status status = ReadAtLeast(sizeof(uint32_t) - pending_size());
    if (!status.ok()) {
      return status;
    }
  }

  // We now know we have at least sizeof(uint32_t) bytes.
  uint32_t n_net;
  memcpy(&n_net, buf_.data() + start_idx_, sizeof(uint32_t));
  start_idx_ += sizeof(uint32_t);
  return htonl(n_net);
}

absl::StatusOr<MessageType> Reader::ReadMessageType() {
  absl::StatusOr<uint16_t> n = ReadUint16();
  if (!n.ok()) {
    return n.status();
  }
  return static_cast<MessageType>(*n);
}

absl::StatusOr<MessageType> Reader::ReadHeader() {
  absl::StatusOr<MessageType> message_type = ReadMessageType();
  if (!message_type.ok()) {
    return message_type.status();
  }

  absl::StatusOr<uint16_t> version = ReadUint16();
  if (!version.ok()) {
    return version.status();
  }
  if (*version != kProtocolVersion) {
    return absl::InvalidArgumentError(
        absl::StrFormat("unsupported protocol version: %d", *version));
  }

  return message_type;
}

absl::StatusOr<std::string> Reader::ReadString() {
  absl::StatusOr<uint32_t> size = ReadUint32();
  if (!size.ok()) {
    return size.status();
  }

  if (pending_size() < *size) {
    // If we don't have enough pending bytes, read enough to parse.
    absl::Status status = ReadAtLeast(*size - pending_size());
    if (!status.ok()) {
      return status;
    }
  }

  // We now know we have at least `size` bytes.
  //
  // Note use string rather than string_view as we may override buffer before
  // the string is processed.
  std::string s(buf_.data() + start_idx_, buf_.data() + start_idx_ + *size);
  start_idx_ += *size;
  return s;
}

absl::Status Reader::ReadAtLeast(size_t n) {
  DLOG(INFO) << "reader: read at least; n=" << n;

  // If over half the bytes in buf_ are unused, shift the bytes to the start.
  if (start_idx_ * 2 > buf_.size()) {
    DLOG(INFO) << "reader: shifting buffer";
    memcpy(buf_.data(), buf_.data() + start_idx_, end_idx_ - start_idx_);
    end_idx_ -= start_idx_;
    start_idx_ = 0;
  }

  if (buf_.size() - end_idx_ < n) {
    DLOG(INFO) << "reader: resizing buffer; size=" << end_idx_ + n;
    buf_.resize(end_idx_ + n);
  }

  size_t read = 0;
  while (read < n) {
    DLOG(INFO) << "reader: read; n=" << buf_.size() - end_idx_;
    absl::StatusOr<size_t> nn = reader_->Read(
        absl::Span<uint8_t>(buf_.data() + end_idx_, buf_.size() - end_idx_));
    if (!nn.ok()) {
      return nn.status();
    }
    DLOG(INFO) << "reader: read bytes; n=" << *nn;
    read += *nn;
    end_idx_ += *nn;
  }
  return absl::OkStatus();
}

}  // namespace server
}  // namespace otter