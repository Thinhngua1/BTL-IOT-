#pragma once
#include <functional>
#include <utility>
#include <vector>

// Lightweight Signal/Slot implementation for MVVM upward notifications
template <typename... Args>
class Signal {
 public:
  using Slot = std::function<void(Args...)>;

  void connect(Slot slot) {
    slots_.push_back(std::move(slot));
  }

  void emit(Args... args) const {
    for (const auto& s : slots_) {
      if (s) s(args...);
    }
  }

 private:
  std::vector<Slot> slots_;
};
