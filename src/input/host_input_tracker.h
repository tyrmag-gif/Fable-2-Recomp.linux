// fable_2 - Portable host keyboard/mouse state tracker (non-Windows hosts)
//
// On Windows, KeyboardGamepadDriver polls the host directly with
// GetAsyncKeyState / GetCursorPos / SetCursorPos. Those calls have no
// equivalent on Linux (X11 and Wayland expose no global key state, and
// Wayland forbids warping the pointer), so on every other platform the driver
// uses this tracker instead.
//
// The tracker listens to the SDK window's own input events (SDL3 underneath),
// keeps a held-key table indexed by rex::ui::VirtualKey (the same Win32-style
// codes the keyboard_gamepad_map parser produces), and accumulates mouse
// motion. Mouse look uses SDL's relative mouse mode (pointer lock), which is
// the supported way to get unbounded mouse deltas on both X11 and Wayland.
//
// Threading: input events arrive on the UI thread; GetDeviceState polls from a
// guest thread. All shared state is guarded by `mutex_`. Window calls that
// must run on the UI thread (relative mode, cursor visibility) are posted
// with CallInUIThreadDeferred, exactly like the SDK's MnkInputDriver.

#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>

#include <rex/ui/ui_event.h>
#include <rex/ui/virtual_key.h>
#include <rex/ui/window.h>
#include <rex/ui/window_listener.h>
#include <rex/ui/windowed_app_context.h>

namespace fable2 {

class HostInputTracker final : public rex::ui::WindowInputListener,
                               public rex::ui::WindowListener {
 public:
  HostInputTracker() = default;
  HostInputTracker(const HostInputTracker&) = delete;
  HostInputTracker& operator=(const HostInputTracker&) = delete;

  ~HostInputTracker() override { Detach(); }

  // Called from InputDriver::OnWindowAvailable (UI thread during setup).
  void Attach(rex::ui::Window* window, size_t z_order) {
    if (!window) return;
    {
      std::lock_guard lock(mutex_);
      if (window_) return;
      window_ = window;
      has_focus_ = window->HasFocus();
    }
    window->AddInputListener(this, z_order);
    window->AddListener(this);
  }

  void Detach() {
    rex::ui::Window* window;
    {
      std::lock_guard lock(mutex_);
      window = window_;
    }
    if (!window) return;
    window->app_context().CallInUIThreadSynchronous([this, window] {
      {
        std::lock_guard lock(mutex_);
        window_ = nullptr;
      }
      if (capture_update_queued_.load(std::memory_order_relaxed)) {
        window->app_context().ExecutePendingFunctionsFromUIThread();
      }
      ReleaseCaptureFromUIThread(window);
      window->RemoveInputListener(this);
      window->RemoveListener(this);
    });
  }

  bool focused() const { return has_focus_.load(std::memory_order_relaxed); }

  bool IsDown(uint16_t vk) const {
    if (vk >= kKeyCount) return false;
    std::lock_guard lock(mutex_);
    return key_down_[vk];
  }

  // Returns the mouse motion accumulated since the previous call and resets
  // it. Drained unconditionally so a backlog never turns into a camera snap.
  void TakeMouseDelta(float& dx, float& dy) {
    std::lock_guard lock(mutex_);
    dx = mouse_dx_;
    dy = mouse_dy_;
    mouse_dx_ = 0.0f;
    mouse_dy_ = 0.0f;
  }

  // Request (or release) pointer lock + hidden cursor for mouse look. Cheap
  // to call every poll: the UI-thread hop is only queued on a state change.
  void SetMouseLook(bool enable) {
    capture_requested_.store(enable, std::memory_order_relaxed);
    if (enable == capture_applied_.load(std::memory_order_relaxed)) return;
    bool expected = false;
    if (!capture_update_queued_.compare_exchange_strong(expected, true)) return;
    std::lock_guard lock(mutex_);
    if (!window_) {
      capture_update_queued_.store(false, std::memory_order_relaxed);
      return;
    }
    // Deferred, not CallInUIThread: running inline would re-enter mutex_.
    window_->app_context().CallInUIThreadDeferred([this] {
      capture_update_queued_.store(false, std::memory_order_relaxed);
      ApplyCaptureFromUIThread();
    });
  }

  // --- WindowInputListener (UI thread) -------------------------------------
  void OnKeyDown(rex::ui::KeyEvent& e) override {
    if (!focused()) return;
    SetKey(static_cast<uint16_t>(e.virtual_key()), true);
  }
  void OnKeyUp(rex::ui::KeyEvent& e) override {
    SetKey(static_cast<uint16_t>(e.virtual_key()), false);
  }
  void OnMouseDown(rex::ui::MouseEvent& e) override {
    if (!focused()) return;
    SetKey(MouseButtonKey(e.button()), true);
  }
  void OnMouseUp(rex::ui::MouseEvent& e) override {
    SetKey(MouseButtonKey(e.button()), false);
  }
  void OnMouseMove(rex::ui::MouseEvent& e) override {
    if (!focused()) return;
    std::lock_guard lock(mutex_);
    if (relative_mode_) {
      // Pointer is locked: x/y stay put, the relative deltas still move.
      mouse_dx_ += e.dx();
      mouse_dy_ += e.dy();
    } else if (capture_applied_.load(std::memory_order_relaxed)) {
      // Pointer lock unavailable: fall back to absolute position differences.
      if (have_prev_pos_) {
        mouse_dx_ += float(e.x() - prev_x_);
        mouse_dy_ += float(e.y() - prev_y_);
      }
    }
    prev_x_ = e.x();
    prev_y_ = e.y();
    have_prev_pos_ = true;
  }

  // --- WindowListener (UI thread) ------------------------------------------
  void OnGotFocus(rex::ui::UISetupEvent&) override { has_focus_ = true; }
  void OnLostFocus(rex::ui::UISetupEvent&) override {
    has_focus_ = false;
    // Withdraw the capture request too, or an update queued before the focus
    // loss grabs the pointer straight back. Release keys so nothing sticks
    // down while alt-tabbed (the key-up goes to the other window).
    capture_requested_.store(false, std::memory_order_relaxed);
    {
      std::lock_guard lock(mutex_);
      for (bool& k : key_down_) k = false;
      mouse_dx_ = mouse_dy_ = 0.0f;
    }
    if (rex::ui::Window* w = window_) ReleaseCaptureFromUIThread(w);
  }
  void OnClosing(rex::ui::UIEvent&) override { Detach(); }

 private:
  static constexpr size_t kKeyCount = 256;

  static uint16_t MouseButtonKey(rex::ui::MouseEvent::Button b) {
    using B = rex::ui::MouseEvent::Button;
    using V = rex::ui::VirtualKey;
    switch (b) {
      case B::kLeft: return static_cast<uint16_t>(V::kLButton);
      case B::kRight: return static_cast<uint16_t>(V::kRButton);
      case B::kMiddle: return static_cast<uint16_t>(V::kMButton);
      case B::kX1: return static_cast<uint16_t>(V::kXButton1);
      case B::kX2: return static_cast<uint16_t>(V::kXButton2);
      default: return 0;
    }
  }

  void SetKey(uint16_t vk, bool down) {
    if (vk == 0 || vk >= kKeyCount) return;
    std::lock_guard lock(mutex_);
    key_down_[vk] = down;
  }

  void ApplyCaptureFromUIThread() {
    rex::ui::Window* window = window_;
    if (!window) return;
    const bool want = capture_requested_.load(std::memory_order_relaxed) && focused();
    if (want == capture_applied_.load(std::memory_order_relaxed)) return;
    if (!want) {
      ReleaseCaptureFromUIThread(window);
      return;
    }
    window->SetCursorVisibility(rex::ui::Window::CursorVisibility::kHidden);
    window->CaptureMouse();
    const bool relative = window->SetRelativeMouseMode(true);
    std::lock_guard lock(mutex_);
    relative_mode_ = relative;
    have_prev_pos_ = false;
    mouse_dx_ = mouse_dy_ = 0.0f;  // no spike when the lock engages
    capture_applied_.store(true, std::memory_order_relaxed);
  }

  void ReleaseCaptureFromUIThread(rex::ui::Window* window) {
    if (!capture_applied_.load(std::memory_order_relaxed)) return;
    window->SetRelativeMouseMode(false);
    window->ReleaseMouse();
    window->SetCursorVisibility(rex::ui::Window::CursorVisibility::kVisible);
    std::lock_guard lock(mutex_);
    relative_mode_ = false;
    capture_applied_.store(false, std::memory_order_relaxed);
  }

  mutable std::mutex mutex_;
  rex::ui::Window* window_ = nullptr;
  bool key_down_[kKeyCount] = {};
  float mouse_dx_ = 0.0f;
  float mouse_dy_ = 0.0f;
  int32_t prev_x_ = 0;
  int32_t prev_y_ = 0;
  bool have_prev_pos_ = false;
  bool relative_mode_ = false;
  std::atomic<bool> has_focus_{false};
  std::atomic<bool> capture_requested_{false};
  std::atomic<bool> capture_applied_{false};
  std::atomic<bool> capture_update_queued_{false};
};

}  // namespace fable2
