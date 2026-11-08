#pragma once
#include <stdint.h>
#include "../Config.h"

enum class CmdType : uint8_t { START, STOP, RESET, TARGET };

struct Command {
  CmdType  type;
  uint16_t value;

  constexpr Command(CmdType t = CmdType::START, uint16_t v = 0)
      : type(t), value(v) {}
};

class CommandQueue {
 public:
  bool enqueue(CmdType type, uint16_t value = 0) {
    uint8_t next = (tail_ + 1) % Config::CMD_QUEUE_SIZE;
    if (next == head_) return false;
    buf_[tail_] = {type, value};
    tail_ = next;
    return true;
  }

  bool dequeue(Command& out) {
    if (head_ == tail_) return false;
    out = buf_[head_];
    head_ = (head_ + 1) % Config::CMD_QUEUE_SIZE;
    return true;
  }

 private:
  Command buf_[Config::CMD_QUEUE_SIZE];
  uint8_t head_ = 0;
  uint8_t tail_ = 0;
};
