/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xam/xam_ui.h"
#include "xenia/app/emulator_window.h"
#include "xenia/base/png_utils.h"
#include "xenia/base/system.h"
#include "xenia/hid/input_system.h"
#include "xenia/kernel/XLiveAPI.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/user_module.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xam/xam_content_device.h"
#include "xenia/kernel/xam/xam_private.h"
#include "xenia/ui/file_picker.h"
#include "xenia/ui/imgui_dialog.h"
#include "xenia/ui/imgui_drawer.h"
#include "xenia/ui/imgui_guest_notification.h"
#include "xenia/ui/imgui_host_notification.h"

#include "xenia/kernel/xam/ui/community_sessions_ui.h"
#include "xenia/kernel/xam/ui/create_profile_ui.h"
#include "xenia/kernel/xam/ui/friends_ui.h"
#include "xenia/kernel/xam/ui/game_achievements_ui.h"
#include "xenia/kernel/xam/ui/gamercard_from_xuid_ui.h"
#include "xenia/kernel/xam/ui/gamercard_ui.h"
#include "xenia/kernel/xam/ui/passcode_ui.h"
#include "xenia/kernel/xam/ui/signin_ui.h"
#include "xenia/kernel/xam/ui/title_info_ui.h"

DEFINE_bool(storage_selection_dialog, false,
            "Show storage device selection dialog when the game requests it.",
            "UI");

DECLARE_int32(license_mask);

namespace xe {
namespace kernel {
namespace xam {
// TODO(gibbed): This is all one giant WIP that seems to work better than the
// previous immediate synchronous completion of dialogs.
//
// The deferred execution of dialog handling is done in such a way that there is
// a pre-, peri- (completion), and post- callback steps.
//
// pre();
// result = completion();
// CompleteOverlapped(result);
// post();
//
// There are games that are batshit insane enough to wait for the X_OVERLAPPED
// to be completed (ie not X_ERROR_PENDING) before creating a listener to
// receive a notification, which is why we have distinct pre- and post- steps.
//
// We deliberately delay the XN_SYS_UI = false notification to give games time
// to create a listener (if they're insane enough do this).

extern std::atomic<int> xam_dialogs_shown_;

template <typename T>
X_RESULT xeXamDispatchDialog(T* dialog,
                             std::function<X_RESULT(T*)> close_callback,
                             uint32_t overlapped) {
  auto pre = []() {
    kernel_state()->BroadcastNotification(kXNotificationSystemUI, true);
  };
  auto run = [dialog, close_callback]() -> X_RESULT {
    X_RESULT result;
    dialog->set_close_callback([&dialog, &result, &close_callback]() {
      result = close_callback(dialog);
    });
    xe::threading::Fence fence;
    xe::ui::WindowedAppContext& app_context =
        kernel_state()->emulator()->display_window()->app_context();
    if (app_context.CallInUIThreadSynchronous(
            [&dialog, &fence]() { dialog->Then(&fence); })) {
      ++xam_dialogs_shown_;
      fence.Wait();
      --xam_dialogs_shown_;
    } else {
      delete dialog;
    }
    // dialog should be deleted at this point!
    return result;
  };
  auto post = []() {
    xe::threading::Sleep(std::chrono::milliseconds(100));
    kernel_state()->BroadcastNotification(kXNotificationSystemUI, false);
  };
  if (!overlapped) {
    pre();
    auto result = run();
    post();
    return result;
  } else {
    kernel_state()->CompleteOverlappedDeferred(run, overlapped, pre, post);
    return X_ERROR_IO_PENDING;
  }
}

template <typename T>
X_RESULT xeXamDispatchDialogEx(
    T* dialog, std::function<X_RESULT(T*, uint32_t&, uint32_t&)> close_callback,
    uint32_t overlapped) {
  auto pre = []() {
    kernel_state()->BroadcastNotification(kXNotificationSystemUI, true);
  };
  auto run = [dialog, close_callback](uint32_t& extended_error,
                                      uint32_t& length) -> X_RESULT {
    auto display_window = kernel_state()->emulator()->display_window();
    X_RESULT result;
    dialog->set_close_callback(
        [&dialog, &result, &extended_error, &length, &close_callback]() {
          result = close_callback(dialog, extended_error, length);
        });
    xe::threading::Fence fence;
    if (display_window->app_context().CallInUIThreadSynchronous(
            [&dialog, &fence]() { dialog->Then(&fence); })) {
      ++xam_dialogs_shown_;
      fence.Wait();
      --xam_dialogs_shown_;
    } else {
      delete dialog;
    }
    // dialog should be deleted at this point!
    return result;
  };
  auto post = []() {
    xe::threading::Sleep(std::chrono::milliseconds(100));
    kernel_state()->BroadcastNotification(kXNotificationSystemUI, false);
  };
  if (!overlapped) {
    pre();
    uint32_t extended_error, length;
    auto result = run(extended_error, length);
    post();
    // TODO(gibbed): do something with extended_error/length?
    return result;
  } else {
    kernel_state()->CompleteOverlappedDeferredEx(run, overlapped, pre, post);
    return X_ERROR_IO_PENDING;
  }
}

X_RESULT xeXamDispatchHeadless(std::function<X_RESULT()> run_callback,
                               uint32_t overlapped) {
  auto pre = []() {
    kernel_state()->BroadcastNotification(kXNotificationSystemUI, true);
  };
  auto post = []() {
    xe::threading::Sleep(std::chrono::milliseconds(100));
    kernel_state()->BroadcastNotification(kXNotificationSystemUI, false);
  };
  if (!overlapped) {
    pre();
    auto result = run_callback();
    post();
    return result;
  } else {
    kernel_state()->CompleteOverlappedDeferred(run_callback, overlapped, pre,
                                               post);
    return X_ERROR_IO_PENDING;
  }
}

X_RESULT xeXamDispatchHeadlessEx(
    std::function<X_RESULT(uint32_t&, uint32_t&)> run_callback,
    uint32_t overlapped) {
  auto pre = []() {
    kernel_state()->BroadcastNotification(kXNotificationSystemUI, true);
  };
  auto post = []() {
    xe::threading::Sleep(std::chrono::milliseconds(100));
    kernel_state()->BroadcastNotification(kXNotificationSystemUI, false);
  };
  if (!overlapped) {
    pre();
    uint32_t extended_error, length;
    auto result = run_callback(extended_error, length);
    post();
    // TODO(gibbed): do something with extended_error/length?
    return result;
  } else {
    kernel_state()->CompleteOverlappedDeferredEx(run_callback, overlapped, pre,
                                                 post);
    return X_ERROR_IO_PENDING;
  }
}

template <typename T>
X_RESULT xeXamDispatchDialogAsync(T* dialog,
                                  std::function<void(T*)> close_callback) {
  kernel_state()->BroadcastNotification(kXNotificationSystemUI, true);
  ++xam_dialogs_shown_;

  // Important to pass captured vars by value here since we return from this
  // without waiting for the dialog to close so the original local vars will be
  // destroyed.
  dialog->set_close_callback([dialog, close_callback]() {
    close_callback(dialog);

    --xam_dialogs_shown_;

    auto run = []() -> void {
      xe::threading::Sleep(std::chrono::milliseconds(100));
      kernel_state()->BroadcastNotification(kXNotificationSystemUI, false);
    };

    std::thread thread(run);
    thread.detach();
  });

  return X_ERROR_SUCCESS;
}

X_RESULT xeXamDispatchHeadlessAsync(std::function<void()> run_callback) {
  kernel_state()->BroadcastNotification(kXNotificationSystemUI, true);
  ++xam_dialogs_shown_;

  auto display_window = kernel_state()->emulator()->display_window();
  display_window->app_context().CallInUIThread([run_callback]() {
    run_callback();

    --xam_dialogs_shown_;

    auto run = []() -> void {
      xe::threading::Sleep(std::chrono::milliseconds(100));
      kernel_state()->BroadcastNotification(kXNotificationSystemUI, false);
    };

    std::thread thread(run);
    thread.detach();
  });

  return X_ERROR_SUCCESS;
}

void MessageBoxDialog::OnDraw(ImGuiIO& io) {
  bool first_draw = false;
  if (!has_opened_) {
    ImGui::OpenPopup(title_.c_str());
    has_opened_ = true;
    first_draw = true;
  }
  if (ImGui::BeginPopupModal(title_.c_str(), nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    if (description_.size()) {
      ImGui::Text("%s", description_.c_str());
    }
    if (first_draw) {
      ImGui::SetKeyboardFocusHere();
    }
    for (size_t i = 0; i < buttons_.size(); ++i) {
      if (ImGui::Button(buttons_[i].c_str())) {
        chosen_button_ = static_cast<uint32_t>(i);
        ImGui::CloseCurrentPopup();
        Close();
      }
      ImGui::SameLine();
    }
    ImGui::Spacing();
    ImGui::Spacing();
    ImGui::EndPopup();
  } else {
    Close();
  }
}

void KeyboardInputDialog::OnDraw(ImGuiIO& io) {
  bool first_draw = false;
  if (!has_opened_) {
    ImGui::OpenPopup(title_.c_str());
    has_opened_ = true;
    first_draw = true;
  }
  if (ImGui::BeginPopupModal(title_.c_str(), nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    if (description_.size()) {
      ImGui::TextWrapped("%s", description_.c_str());
    }
    if (first_draw) {
      ImGui::SetKeyboardFocusHere();
    }
    ImGui::PushID("input_text");
    bool input_submitted =
        ImGui::InputText("##body", text_buffer_.data(), text_buffer_.size(),
                         ImGuiInputTextFlags_EnterReturnsTrue);
    // Context menu for paste functionality
    if (ImGui::BeginPopupContextItem("input_context_menu")) {
      if (ImGui::MenuItem("Paste")) {
        if (ImGui::GetClipboardText() != nullptr) {
          std::string clipboard_text = ImGui::GetClipboardText();
          xe::string_util::copy_truncating(text_buffer_.data(), clipboard_text,
                                           text_buffer_.size());
        }
      }
      ImGui::EndPopup();
    }
    ImGui::PopID();
    if (input_submitted) {
      text_ = std::string(text_buffer_.data(), text_buffer_.size());
      cancelled_ = false;
      ImGui::CloseCurrentPopup();
      Close();
    }
    if (ImGui::Button("OK")) {
      text_ = std::string(text_buffer_.data(), text_buffer_.size());
      cancelled_ = false;
      ImGui::CloseCurrentPopup();
      Close();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
      text_ = "";
      cancelled_ = true;
      ImGui::CloseCurrentPopup();
      Close();
    }
    ImGui::Spacing();
    ImGui::EndPopup();
  } else {
    Close();
  }
}

static dword_result_t XamShowMessageBoxUi(
    dword_t user_index, lpu16string_t title_ptr, lpu16string_t text_ptr,
    dword_t button_count, lpdword_t button_ptrs, dword_t active_button,
    dword_t flags, pointer_t<MESSAGEBOX_RESULT> result_ptr,
    pointer_t<XAM_OVERLAPPED> overlapped) {
  std::string title = title_ptr ? xe::to_utf8(title_ptr.value()) : "";
  std::string text = text_ptr ? xe::to_utf8(text_ptr.value()) : "";

  std::vector<std::string> buttons;
  for (uint32_t i = 0; i < button_count; ++i) {
    uint32_t button_ptr = button_ptrs[i];
    auto button = xe::load_and_swap<std::u16string>(
        kernel_state()->memory()->TranslateVirtual(button_ptr));
    buttons.push_back(xe::to_utf8(button));
  }

  X_RESULT result;
  if (cvars::headless) {
    // Auto-pick the focused button.
    auto run = [result_ptr, active_button]() -> X_RESULT {
      result_ptr->ButtonPressed = static_cast<uint32_t>(active_button);
      return X_ERROR_SUCCESS;
    };

    result = xeXamDispatchHeadless(run, overlapped);
  } else {
    switch (flags & 0xF) {
      case XMBox_NOICON: {
      } break;
      case XMBox_ERRORICON: {
      } break;
      case XMBox_WARNINGICON: {
      } break;
      case XMBox_ALERTICON: {
      } break;
    }

    const Emulator* emulator = kernel_state()->emulator();
    xe::ui::ImGuiDrawer* imgui_drawer = emulator->imgui_drawer();

    if (flags & XMBox_PASSCODEMODE || flags & XMBox_VERIFYPASSCODEMODE) {
      auto close = [result_ptr,
                    active_button](ui::ProfilePasscodeUI* dialog) -> X_RESULT {
        if (dialog->SelectedSignedIn()) {
          // Logged in
          return X_ERROR_SUCCESS;
        } else {
          return X_ERROR_FUNCTION_FAILED;
        }
      };

      result = xeXamDispatchDialog<ui::ProfilePasscodeUI>(
          new ui::ProfilePasscodeUI(imgui_drawer, title, text, result_ptr),
          close, overlapped);
    } else {
      auto close = [result_ptr](MessageBoxDialog* dialog) -> X_RESULT {
        result_ptr->ButtonPressed = dialog->chosen_button();
        return X_ERROR_SUCCESS;
      };

      result = xeXamDispatchDialog<MessageBoxDialog>(
          new MessageBoxDialog(imgui_drawer, title, text, buttons,
                               static_cast<uint32_t>(active_button)),
          close, overlapped);
    }
  }

  return result;
}

dword_result_t XamIsUIActive_entry() { return xeXamIsUIActive(); }
DECLARE_XAM_EXPORT2(XamIsUIActive, kUI, kImplemented, kHighFrequency);

// https://www.se7ensins.com/forums/threads/working-xshowmessageboxui.844116/
dword_result_t XamShowMessageBoxUI_entry(
    dword_t user_index, lpu16string_t title_ptr, lpu16string_t text_ptr,
    dword_t button_count, lpdword_t button_ptrs, dword_t active_button,
    dword_t flags, pointer_t<MESSAGEBOX_RESULT> result_ptr,
    pointer_t<XAM_OVERLAPPED> overlapped) {
  return XamShowMessageBoxUi(user_index, title_ptr, text_ptr, button_count,
                             button_ptrs, active_button, flags, result_ptr,
                             overlapped);
}
DECLARE_XAM_EXPORT1(XamShowMessageBoxUI, kUI, kImplemented);

dword_result_t XamShowMessageBoxUIEx_entry(
    dword_t user_index, lpu16string_t title_ptr, lpu16string_t text_ptr,
    dword_t button_count, lpdword_t button_ptrs, dword_t active_button,
    dword_t flags, dword_t unknown_unused,
    pointer_t<MESSAGEBOX_RESULT> result_ptr,
    pointer_t<XAM_OVERLAPPED> overlapped) {
  return XamShowMessageBoxUi(user_index, title_ptr, text_ptr, button_count,
                             button_ptrs, active_button, flags, result_ptr,
                             overlapped);
}
DECLARE_XAM_EXPORT1(XamShowMessageBoxUIEx, kUI, kImplemented);

dword_result_t XNotifyQueueUI_entry(dword_t exnq, dword_t dwUserIndex,
                                    qword_t qwAreas,
                                    lpu16string_t displayText_ptr,
                                    lpvoid_t contextData) {
  std::string displayText = "";
  const uint8_t position_id = static_cast<uint8_t>(qwAreas);

  if (displayText_ptr) {
    displayText = xe::to_utf8(displayText_ptr.value());
  }

  XELOGI("XNotifyQueueUI: {}", displayText);

  const Emulator* emulator = kernel_state()->emulator();
  xe::ui::ImGuiDrawer* imgui_drawer = emulator->imgui_drawer();

  new xe::ui::XNotifyWindow(imgui_drawer, "", displayText, dwUserIndex,
                            position_id);

  // XNotifyQueueUI -> XNotifyQueueUIEx -> XMsgProcessRequest ->
  // XMsgStartIORequestEx & XMsgInProcessCall
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XNotifyQueueUI, kUI, kSketchy);

// https://www.se7ensins.com/forums/threads/release-how-to-use-xshowkeyboardui-release.906568/
dword_result_t XamShowKeyboardUI_entry(
    dword_t user_index, dword_t flags, lpu16string_t default_text,
    lpu16string_t title, lpu16string_t description, lpu16string_t buffer,
    dword_t buffer_length, pointer_t<XAM_OVERLAPPED> overlapped) {
  if (!buffer) {
    return X_ERROR_INVALID_PARAMETER;
  }

  assert_not_null(overlapped);

  auto buffer_size = static_cast<size_t>(buffer_length) * 2;

  X_RESULT result;
  if (cvars::headless) {
    auto run = [default_text, buffer, buffer_length,
                buffer_size]() -> X_RESULT {
      // Redirect default_text back into the buffer.
      if (!default_text) {
        std::memset(buffer, 0, buffer_size);
      } else {
        string_util::copy_and_swap_truncating(buffer, default_text.value(),
                                              buffer_length);
      }
      return X_ERROR_SUCCESS;
    };
    result = xeXamDispatchHeadless(run, overlapped);
  } else {
    auto close = [buffer, buffer_length](KeyboardInputDialog* dialog,
                                         uint32_t& extended_error,
                                         uint32_t& length) -> X_RESULT {
      if (dialog->cancelled()) {
        extended_error = X_ERROR_CANCELLED;
        length = 0;
        return X_ERROR_SUCCESS;
      } else {
        // Zero the output buffer.
        auto text = xe::to_utf16(dialog->text());
        string_util::copy_and_swap_truncating(buffer, text, buffer_length);
        extended_error = X_ERROR_SUCCESS;
        length = 0;
        return X_ERROR_SUCCESS;
      }
    };
    const Emulator* emulator = kernel_state()->emulator();
    xe::ui::ImGuiDrawer* imgui_drawer = emulator->imgui_drawer();

    std::string title_str = title ? xe::to_utf8(title.value()) : "";
    std::string desc_str = description ? xe::to_utf8(description.value()) : "";
    std::string def_text_str =
        default_text ? xe::to_utf8(default_text.value()) : "";

    result = xeXamDispatchDialogEx<KeyboardInputDialog>(
        new KeyboardInputDialog(imgui_drawer, title_str, desc_str, def_text_str,
                                buffer_length),
        close, overlapped);
  }
  return result;
}
DECLARE_XAM_EXPORT1(XamShowKeyboardUI, kUI, kImplemented);

dword_result_t XamShowDeviceSelectorUI_entry(
    dword_t user_index, dword_t content_type, dword_t content_flags,
    qword_t total_requested, lpdword_t device_id_ptr,
    pointer_t<XAM_OVERLAPPED> overlapped) {
  if (!overlapped) {
    return X_ERROR_INVALID_PARAMETER;
  }

  if ((user_index >= XUserMaxUserCount && user_index != XUserIndexAny) ||
      (content_flags & 0x83F00008) != 0 || !device_id_ptr) {
    XOverlappedSetExtendedError(overlapped, X_ERROR_INVALID_PARAMETER);
    return X_ERROR_INVALID_PARAMETER;
  }

  if (user_index != XUserIndexAny &&
      !kernel_state()->xam_state()->IsUserSignedIn(user_index)) {
    kernel_state()->CompleteOverlappedImmediate(overlapped,
                                                X_ERROR_NO_SUCH_USER);
    return X_ERROR_IO_PENDING;
  }

  std::vector<const DummyDeviceInfo*> devices = ListStorageDevices();

  if (cvars::headless || !cvars::storage_selection_dialog) {
    // Default to the first storage device (HDD) if headless.
    return xeXamDispatchHeadless(
        [device_id_ptr, devices]() -> X_RESULT {
          if (devices.empty()) return X_ERROR_CANCELLED;

          const DummyDeviceInfo* device_info = devices.front();
          *device_id_ptr = static_cast<uint32_t>(device_info->device_id);
          return X_ERROR_SUCCESS;
        },
        overlapped);
  }

  auto close = [device_id_ptr, devices](MessageBoxDialog* dialog) -> X_RESULT {
    uint32_t button = dialog->chosen_button();
    if (button >= devices.size()) return X_ERROR_CANCELLED;

    const DummyDeviceInfo* device_info = devices.at(button);
    *device_id_ptr = static_cast<uint32_t>(device_info->device_id);
    return X_ERROR_SUCCESS;
  };

  std::string title = "Select storage device";
  std::string desc = "";

  cxxopts::OptionNames buttons;
  for (auto& device_info : devices) {
    buttons.push_back(to_utf8(device_info->name));
  }
  buttons.push_back("Cancel");

  const Emulator* emulator = kernel_state()->emulator();
  xe::ui::ImGuiDrawer* imgui_drawer = emulator->imgui_drawer();
  return xeXamDispatchDialog<MessageBoxDialog>(
      new MessageBoxDialog(imgui_drawer, title, desc, buttons, 0), close,
      overlapped);
}
DECLARE_XAM_EXPORT1(XamShowDeviceSelectorUI, kUI, kImplemented);

void XamShowDirtyDiscErrorUI_entry(dword_t user_index) {
  if (cvars::headless) {
    assert_always();
    exit(1);
    return;
  }

  std::string title = "Disc Read Error";
  std::string desc =
      "There's been an issue reading content from the game disc.\nThis is "
      "likely caused by bad or unimplemented file IO calls.";

  const Emulator* emulator = kernel_state()->emulator();
  xe::ui::ImGuiDrawer* imgui_drawer = emulator->imgui_drawer();
  xeXamDispatchDialog<MessageBoxDialog>(
      new MessageBoxDialog(imgui_drawer, title, desc, {"OK"}, 0),
      [](MessageBoxDialog*) -> X_RESULT { return X_ERROR_SUCCESS; }, 0);
  // This is death, and should never return.
  // TODO(benvanik): cleaner exit.
  exit(1);
}
DECLARE_XAM_EXPORT1(XamShowDirtyDiscErrorUI, kUI, kImplemented);

dword_result_t XamShowPartyUI_entry(unknown_t r3, unknown_t r4) {
  return X_ERROR_FUNCTION_FAILED;
}
DECLARE_XAM_EXPORT1(XamShowPartyUI, kNone, kStub);

// this is supposed to do a lot more, calls another function that triggers some
// cbs
dword_result_t XamSetDashContext_entry(dword_t value,
                                       const ppc_context_t& ctx) {
  ctx->kernel_state->dash_context_ = value;
  kernel_state()->BroadcastNotification(
      kXNotificationDvdDriveUnknownDashContext, 0);
  return 0;
}

DECLARE_XAM_EXPORT1(XamSetDashContext, kNone, kImplemented);

dword_result_t XamGetDashContext_entry(const ppc_context_t& ctx) {
  return ctx->kernel_state->dash_context_;
}

DECLARE_XAM_EXPORT1(XamGetDashContext, kNone, kImplemented);

// https://gitlab.com/GlitchyScripts/xlivelessness/-/blob/master/xlivelessness/xlive/xdefs.hpp?ref_type=heads#L1235
dword_result_t XamShowMarketplaceUIEx_entry(dword_t user_index, dword_t ui_type,
                                            qword_t offer_id,
                                            dword_t offer_type,
                                            dword_t content_category,
                                            unknown_t unk6, unknown_t unk7,
                                            dword_t title_id) {
  // ui_type:
  // 0 - view all content for the current title
  // 1 - view content specified by offer id
  // offer_types:
  // filter for content list, usually just -1
  // content_category:
  // filter on item types for games (e.g. cars, maps, weapons, etc)
  if (user_index >= XUserMaxUserCount) {
    return X_ERROR_INVALID_PARAMETER;
  }

  if (!kernel_state()->xam_state()->IsUserSignedIn(user_index)) {
    return X_ERROR_NO_SUCH_USER;
  }

  if (cvars::headless) {
    return xeXamDispatchHeadlessAsync([]() {});
  }

  bool is_xbla_unlock_offer =
      (offer_id == ((uint64_t(kernel_state()->title_id()) << 32) | 1ull));

  auto close = [ui_type,
                is_xbla_unlock_offer](MessageBoxDialog* dialog) -> void {
    if (ui_type == 1 && is_xbla_unlock_offer) {
      uint32_t button = dialog->chosen_button();
      if (button == 0) {
        cvars::license_mask = 1;

        kernel_state()->BroadcastNotification(
            kXNotificationLiveContentInstalled, 0);
      }
    }
  };

  std::string title = "Xbox Marketplace";
  std::string desc = "";
  cxxopts::OptionNames buttons;

  switch (ui_type) {
    case X_MARKETPLACE_ENTRYPOINT::ContentList:
      desc =
          "Game requested to open marketplace page with all content for the "
          "current title ID.";
      break;
    case X_MARKETPLACE_ENTRYPOINT::ContentItem:
      desc = fmt::format(
          "Game requested to open marketplace page for offer ID 0x{:016X}.",
          static_cast<uint64_t>(offer_id));
      break;
    case X_MARKETPLACE_ENTRYPOINT::MembershipList:
      desc =
          "Game requested to open marketplace page with all Xbox Live "
          "memberships.";
      break;
    case X_MARKETPLACE_ENTRYPOINT::MembershipItem:
      desc = fmt::format(
          "Game requested to open marketplace page for an Xbox Live "
          "membership offer 0x{:016X}.",
          static_cast<uint64_t>(offer_id));
      break;
    case X_MARKETPLACE_ENTRYPOINT::ContentList_Background:
      // Used when accessing microsoft points
      desc = fmt::format(
          "Xbox Marketplace requested access to Microsoft Points offer page "
          "0x{:016X}.",
          static_cast<uint64_t>(offer_id));
      break;
    case X_MARKETPLACE_ENTRYPOINT::ContentItem_Background:
      // Used when accessing credit card information and calls
      // XamShowCreditCardUI
      desc = fmt::format(
          "Xbox Marketplace requested access to credit card information page "
          "0x{:016X}.",
          static_cast<uint64_t>(offer_id));
      break;
    case X_MARKETPLACE_ENTRYPOINT::ForcedNameChangeV1:
      // Used by XamShowForcedNameChangeUI v1888
      desc = fmt::format("Changing gamertag currently not implemented.");
      break;
    case X_MARKETPLACE_ENTRYPOINT::ForcedNameChangeV2:
      // Used by XamShowForcedNameChangeUI NXE and up
      desc = fmt::format("Changing gamertag currently not implemented.");
      break;
    case X_MARKETPLACE_ENTRYPOINT::ProfileNameChange:
      // Used by dashboard when selecting change gamertag in profile menu
      desc = fmt::format("Changing gamertag currently not implemented.");
      break;
    case X_MARKETPLACE_ENTRYPOINT::ActiveDownloads:
      // Used in profile tabs when clicking active downloads
      desc = fmt::format(
          "There are no current plans to download files from Xbox "
          "Marketplace.");
      break;
    default:
      desc = fmt::format("Unknown marketplace op {:d}",
                         static_cast<uint32_t>(ui_type));
      break;
  }

  desc +=
      "\nNote that since Xenia cannot access Xbox Marketplace, any DLC must be "
      "installed manually using File -> Install Content.";

  switch (ui_type) {
    case X_MARKETPLACE_ENTRYPOINT::ContentList:
    default:
      buttons.push_back("OK");
      break;
    case X_MARKETPLACE_ENTRYPOINT::ContentItem:
      if (is_xbla_unlock_offer) {
        desc +=
            "\n\nTo start trial games in full mode, set license_mask to 1 in "
            "Xenia config file.\n\nDo you wish to change license_mask to 1 for "
            "*this session*?";
        buttons.push_back("Yes");
        buttons.push_back("No");
      } else {
        buttons.push_back("OK");
      }
      break;
  }

  const Emulator* emulator = kernel_state()->emulator();
  xe::ui::ImGuiDrawer* imgui_drawer = emulator->imgui_drawer();
  return xeXamDispatchDialogAsync<MessageBoxDialog>(
      new MessageBoxDialog(imgui_drawer, title, desc, buttons, 0), close);
}
DECLARE_XAM_EXPORT1(XamShowMarketplaceUIEx, kUI, kSketchy);

dword_result_t XamShowMarketplaceUI_entry(dword_t user_index, dword_t ui_type,
                                          qword_t offer_id, dword_t offer_type,
                                          dword_t content_category,
                                          dword_t title_id) {
  return XamShowMarketplaceUIEx_entry(user_index, ui_type, offer_id, offer_type,
                                      content_category, 0, 0, title_id);
}
DECLARE_XAM_EXPORT1(XamShowMarketplaceUI, kUI, kSketchy);

dword_result_t XamShowMarketplaceDownloadItemsUI_entry(
    dword_t user_index, dword_t ui_type, lpqword_t offers, dword_t num_offers,
    lpdword_t hresult_ptr, pointer_t<XAM_OVERLAPPED> overlapped) {
  if (user_index >= XUserMaxUserCount || !offers || num_offers > 6) {
    return X_ERROR_INVALID_PARAMETER;
  }

  if (!kernel_state()->xam_state()->IsUserSignedIn(user_index)) {
    if (overlapped) {
      kernel_state()->CompleteOverlappedImmediate(overlapped,
                                                  X_ERROR_NO_SUCH_USER);
      return X_ERROR_IO_PENDING;
    }
    return X_ERROR_NO_SUCH_USER;
  }

  if (cvars::headless) {
    return xeXamDispatchHeadless(
        [hresult_ptr]() -> X_RESULT {
          if (hresult_ptr) {
            *hresult_ptr = X_E_SUCCESS;
          }
          return X_ERROR_SUCCESS;
        },
        overlapped);
  }

  auto close = [hresult_ptr](MessageBoxDialog* dialog) -> X_RESULT {
    if (hresult_ptr) {
      // TODO
      *hresult_ptr = X_E_SUCCESS;
    }
    return X_ERROR_SUCCESS;
  };

  std::string title = "Xbox Marketplace";
  std::string desc = "";
  cxxopts::OptionNames buttons = {"OK"};

  switch (ui_type) {
    case X_MARKETPLACE_DOWNLOAD_ITEMS_ENTRYPOINTS::FREEITEMS:
      desc =
          "Game requested to open download page for the following free offer "
          "IDs:";
      break;
    case X_MARKETPLACE_DOWNLOAD_ITEMS_ENTRYPOINTS::PAIDITEMS:
      desc =
          "Game requested to open download page for the following offer IDs:";
      break;
    default:
      return X_ERROR_INVALID_PARAMETER;
  }

  for (uint32_t i = 0; i < num_offers; i++) {
    desc += fmt::format("\n0x{:16X}", offers[i].get());
  }

  desc +=
      "\n\nNote that since Xenia cannot access Xbox Marketplace, any DLC "
      "must "
      "be installed manually using File -> Install Content.";

  const Emulator* emulator = kernel_state()->emulator();
  xe::ui::ImGuiDrawer* imgui_drawer = emulator->imgui_drawer();
  return xeXamDispatchDialog<MessageBoxDialog>(
      new MessageBoxDialog(imgui_drawer, title, desc, buttons, 0), close,
      overlapped);
}
DECLARE_XAM_EXPORT1(XamShowMarketplaceDownloadItemsUI, kUI, kSketchy);

dword_result_t XamShowForcedNameChangeUI_entry(dword_t user_index) {
  // Changes from 6 to 8 past NXE
  return XamShowMarketplaceUIEx_entry(user_index, 6, 0, 0xffffffff, 0, 0, 0, 0);
}
DECLARE_XAM_EXPORT1(XamShowForcedNameChangeUI, kUI, kImplemented);

bool xeDrawProfileContent(xe::ui::ImGuiDrawer* imgui_drawer,
                          const uint64_t xuid, const uint8_t user_index,
                          const X_XAMACCOUNTINFO* account,
                          const xe::ui::ImmediateTexture* profile_icon,
                          std::function<bool()> context_menu,
                          std::function<void()> on_profile_change,
                          uint64_t* selected_xuid) {
  const ImVec2 start_position = ImGui::GetCursorPos();

  ImGui::BeginGroup();
  {
    if (profile_icon) {
      ImGui::Image(reinterpret_cast<ImTextureID>(profile_icon),
                   xe::ui::default_image_icon_size);
    } else {
      if (user_index < XUserMaxUserCount) {
        const auto icon = imgui_drawer->GetNotificationIcon(user_index);
        ImGui::Image(reinterpret_cast<ImTextureID>(icon),
                     xe::ui::default_image_icon_size);
      } else {
        ImGui::Dummy(xe::ui::default_image_icon_size);
      }
    }

    ImGui::SameLine();

    ImGui::BeginGroup();
    {
      ImGui::TextUnformatted(
          fmt::format("User: {}\n", account->GetGamertagString()).c_str());
      ImGui::TextUnformatted(fmt::format("XUID: {:016X}  \n", xuid).c_str());

      const std::string live_enabled = fmt::format(
          "Xbox Live Enabled: {}", account->IsLiveEnabled() ? "True" : "False");

      ImGui::TextUnformatted(live_enabled.c_str());

      if (user_index != XUserIndexAny) {
        ImGui::TextUnformatted(
            fmt::format("Assigned to slot: {}\n", user_index + 1).c_str());
      } else {
        ImGui::TextUnformatted(fmt::format("Profile is not signed in").c_str());
      }
    }
    ImGui::EndGroup();
  }
  ImGui::EndGroup();

  if (xuid && selected_xuid) {
    const ImVec2 end_draw_position =
        ImVec2(ImGui::GetCursorPos().x - start_position.x,
               ImGui::GetCursorPos().y - start_position.y);

    ImGui::SetCursorPos(start_position);
    if (ImGui::Selectable("##Selectable", *selected_xuid == xuid,
                          ImGuiSelectableFlags_SpanAllColumns,
                          end_draw_position)) {
      *selected_xuid = xuid;
    }

    if (context_menu) {
      return context_menu();
    }
  }

  return true;
}

bool xeDrawFriendContent(xe::ui::ImGuiDrawer* imgui_drawer,
                         UserProfile* profile,
                         FriendPresenceObjectJSON& presence,
                         uint64_t* selected_xuid_, uint64_t* removed_xuid_) {
  const uint32_t user_index =
      kernel_state()->xam_state()->GetUserIndexAssignedToProfileFromXUID(
          profile->GetLogonXUID());

  const ImVec2 drawing_start_position = ImGui::GetCursorPos();
  ImVec2 current_drawing_position = ImGui::GetCursorPos();

  ImGui::TextUnformatted(presence.Gamertag().c_str());

  uint32_t index = 1;

  const uint32_t title_id = presence.TitleIDValue();

  if (!presence.TitleID().empty()) {
    ImGui::SameLine();
    ImGui::SetCursorPos(current_drawing_position);
    ImGui::SetCursorPosY(current_drawing_position.y +
                         ImGui::GetTextLineHeight());

    if (title_id) {
      if (title_id == kernel_state()->title_id()) {
        ImGui::TextUnformatted(
            fmt::format("Game: {}", kernel_state()->emulator()->title_name())
                .c_str());
      } else {
        ImGui::TextUnformatted(
            fmt::format("Title ID: {}", presence.TitleID()).c_str());
      }

      index++;
    }
  }

  ImGui::SameLine();
  ImGui::SetCursorPos(current_drawing_position);
  ImGui::SetCursorPosY(current_drawing_position.y +
                       index * ImGui::GetTextLineHeight());

  const uint64_t friend_xuid = presence.XUID();
  const std::string friend_xuid_str = fmt::format("{:016X}", friend_xuid);

  ImGui::TextUnformatted(
      fmt::format("Online XUID: {:016X}\n", friend_xuid).c_str());
  index++;

  if (!presence.RichPresence().empty()) {
    ImGui::SameLine();
    ImGui::SetCursorPos(current_drawing_position);
    ImGui::SetCursorPosY(current_drawing_position.y +
                         index * ImGui::GetTextLineHeight());

    std::string presense_string =
        xe::string_util::trim(xe::to_utf8(presence.RichPresence()));

    presense_string =
        std::regex_replace(presense_string, std::regex("\\n"), ", ");

    ImGui::TextWrapped(fmt::format("Status: {}", presense_string).c_str());

    index++;
  }

  ImGui::Spacing();

  float btn_height = 25;
  float btn_width = (ImGui::GetContentRegionAvail().x * 0.5f) -
                    (ImGui::GetStyle().ItemSpacing.x * 0.5f);
  ImVec2 half_width_btn = ImVec2(btn_width, btn_height);

  bool are_friends = profile->IsFriend(friend_xuid, nullptr);
  bool is_self = profile->GetOnlineXUID() == presence.XUID();

  const std::string join_label =
      std::format("Join Session##{}", friend_xuid_str);

  const std::string remove_label = std::format("Remove##{}", friend_xuid_str);

  const std::string add_label = std::format("Add##{}", friend_xuid_str);

  const bool same_title = title_id == kernel_state()->title_id();

  if (!is_self) {
    ImGui::BeginDisabled(!presence.SessionID() || !same_title);
    if (ImGui::Button(join_label.c_str(), half_width_btn)) {
      X_INVITE_INFO* invite = profile->GetSelfInvite();

      memset(invite, 0, sizeof(X_INVITE_INFO));

      invite->from_game_invite = false;
      invite->title_id = kernel_state()->title_id();
      invite->xuid_invitee = profile->GetOnlineXUID();
      invite->xuid_inviter = presence.XUID();

      kernel_state()->BroadcastNotification(kXNotificationLiveInviteAccepted,
                                            user_index);
    }
    ImGui::EndDisabled();

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
      if (kernel_state()->title_id() == 0 || title_id == 0 || same_title) {
        ImGui::SetTooltip("Join gaming session");
      } else {
        ImGui::SetTooltip(
            fmt::format("{} is playing a different game", presence.Gamertag())
                .c_str());
      }
    }
  }

  ImGui::SameLine();

  if (are_friends && !is_self) {
    if (ImGui::Button(remove_label.c_str(), half_width_btn)) {
      if (profile->RemoveFriend(friend_xuid)) {
        if (removed_xuid_) {
          *removed_xuid_ = friend_xuid;
        }

        XLiveAPI::RemoveFriend(friend_xuid);
        kernel_state()->BroadcastNotification(
            kXNotificationFriendsFriendRemoved, user_index);

        std::string description =
            !presence.Gamertag().empty() ? presence.Gamertag() : "Success";

        kernel_state()
            ->emulator()
            ->display_window()
            ->app_context()
            .CallInUIThread([&]() {
              new xe::ui::HostNotificationWindow(imgui_drawer, "Removed Friend",
                                                 description, 0);
            });
      }
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
      ImGui::SetTooltip("Remove Friend");
    }
  }

  if (!are_friends && !is_self) {
    if (ImGui::Button(add_label.c_str(), half_width_btn)) {
      bool added = profile->AddFriendFromXUID(friend_xuid);

      if (added) {
        XLiveAPI::AddFriend(friend_xuid);

        kernel_state()->BroadcastNotification(kXNotificationFriendsFriendAdded,
                                              user_index);
      }

      std::string description =
          !presence.Gamertag().empty() ? presence.Gamertag() : "Success";

      if (!added) {
        description = "Failed!";
      }

      kernel_state()
          ->emulator()
          ->display_window()
          ->app_context()
          .CallInUIThread([&]() {
            new xe::ui::HostNotificationWindow(imgui_drawer, "Added Friend",
                                               description, 0);
          });
    }

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
      ImGui::SetTooltip("Add Friend");
    }
  }
  ImGui::Spacing();

  ImVec2 drawing_end_position = ImGui::GetCursorPos();

  if (selected_xuid_) {
    ImGui::SetCursorPos(drawing_start_position);

    const std::string selectable_label =
        std::format("##Selectable{}", friend_xuid_str);
    const std::string context_label =
        std::format("Friend Menu##{}", friend_xuid_str);

    auto selectable_area =
        ImVec2(drawing_end_position.x - drawing_start_position.x,
               (drawing_end_position.y - drawing_start_position.y) - 35);

    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32(50, 100, 200, 50));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, IM_COL32(0, 0, 0, 0));
    if (ImGui::Selectable(selectable_label.c_str(), false,
                          ImGuiSelectableFlags_SpanAllColumns ||
                              ImGuiSelectableFlags_Disabled,
                          selectable_area)) {
      *selected_xuid_ = friend_xuid;
    }
    ImGui::PopStyleColor(2);

    if (ImGui::BeginPopupContextItem(context_label.c_str())) {
      if (ImGui::BeginMenu("Copy")) {
        if (ImGui::MenuItem("Gamertag")) {
          ImGui::SetClipboardText(presence.Gamertag().c_str());
        }

        ImGui::Separator();

        if (ImGui::MenuItem("XUID Online")) {
          ImGui::SetClipboardText(fmt::format("{:016X}", friend_xuid).c_str());
        }

        ImGui::EndMenu();
      }
      ImGui::EndPopup();
    }
  }

  ImGui::SetCursorPos(drawing_end_position);

  return true;
}

bool xeDrawAddFriend(xe::ui::ImGuiDrawer* imgui_drawer, UserProfile* profile,
                     ui::AddFriendArgs& args) {
  ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImVec2 center = viewport->GetCenter();

  if (!args.add_friend_open) {
    args.add_friend_first_draw = false;
  }

  float btn_height = 25;

  ImGui::SetNextWindowContentSize(ImVec2(200, 0));
  ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  if (ImGui::BeginPopupModal("Add Friend", &args.add_friend_open,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::SetWindowFontScale(1.05f);

    ImVec2 btn_size = ImVec2(ImGui::GetContentRegionAvail().x, btn_height);

    uint32_t user_index =
        kernel_state()->xam_state()->GetUserIndexAssignedToProfileFromXUID(
            profile->GetLogonXUID());

    bool max_friends = profile->GetFriendsCount() >= X_ONLINE_MAX_FRIENDS;

    if (max_friends) {
      ImGui::Text("Max Friends Reached!");
      ImGui::Separator();
    } else if (args.are_friends) {
      ImGui::Text("Friend Added!");
      ImGui::Separator();
    }

    const std::string xuid_string = std::string(args.add_xuid_);

    uint64_t xuid = 0;

    if (xuid_string.length() == 16) {
      if (xuid_string.starts_with("0009")) {
        xuid = string_util::from_string<uint64_t>(xuid_string, true);

        args.valid_xuid = IsOnlineXUID(xuid);
        args.are_friends = profile->IsFriend(xuid);
      }

      if (!args.valid_xuid) {
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(240, 50, 50, 255));
        if (xuid_string.starts_with("E")) {
          ImGui::Text("This is an offline XUID!");
        } else {
          ImGui::Text("Invalid XUID!");
        }
        ImGui::PopStyleColor();

        ImGui::Separator();
      }
    } else {
      args.valid_xuid = false;
      args.are_friends = false;
    }

    ImGui::Text("Friend's Online XUID:");

    ImGui::SameLine();

    const float window_width = ImGui::GetContentRegionAvail().x;

    const std::string friends_count =
        fmt::format("{}/100", profile->GetFriendsCount());

    ImGui::SetCursorPosX((ImGui::GetCursorPosX() + window_width -
                          ImGui::CalcTextSize(friends_count.c_str()).x));

    ImGui::Text(friends_count.c_str());

    if (!args.add_friend_first_draw && std::string(args.add_xuid_).empty()) {
      args.add_friend_first_draw = true;
      ImGui::SetKeyboardFocusHere();
    }

    ImVec2 drawing_start_position = ImGui::GetCursorPos();

    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);
    ImGui::InputTextWithHint("##AddFriend", "0009XXXXXXXXXXXX", args.add_xuid_,
                             sizeof(args.add_xuid_),
                             ImGuiInputTextFlags_CharsHexadecimal |
                                 ImGuiInputTextFlags_CharsUppercase);
    ImGui::PopItemWidth();

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
      ImGui::SetTooltip("Right Click");
    }

    ImVec2 drawing_end_position = ImGui::GetCursorPos();

    ImGui::SetCursorPos(drawing_start_position);

    auto selectable_area =
        ImVec2(drawing_end_position.x - drawing_start_position.x,
               (drawing_end_position.y - drawing_start_position.y));

    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, IM_COL32(0, 0, 0, 0));
    if (ImGui::Selectable("##SelectableAddFriend", false,
                          ImGuiSelectableFlags_SpanAllColumns ||
                              ImGuiSelectableFlags_Disabled,
                          selectable_area)) {
    }
    ImGui::PopStyleColor(2);

    if (ImGui::BeginPopupContextItem("##AddFriendContexts")) {
      if (ImGui::MenuItem("Paste")) {
        const char* clipboard = ImGui::GetClipboardText();

        if (clipboard && strlen(clipboard)) {
          strncpy(args.add_xuid_, clipboard, 16);
        }
      }

      ImGui::Separator();

      if (ImGui::MenuItem("Clear")) {
        memset(args.add_xuid_, 0, sizeof(args.add_xuid_));
      }

      ImGui::EndPopup();
    }

    ImGui::SetCursorPos(drawing_end_position);

    ImGui::BeginDisabled(!args.valid_xuid || args.are_friends || max_friends);
    if (ImGui::Button("Add", btn_size)) {
      bool added = profile->AddFriendFromXUID(xuid);

      if (added) {
        XLiveAPI::AddFriend(xuid);
        args.added_friend = true;

        kernel_state()->BroadcastNotification(kXNotificationFriendsFriendAdded,
                                              user_index);
      }

      std::string desc = xuid_string;

      if (!added) {
        desc = "Failed!";
      }

      kernel_state()
          ->emulator()
          ->display_window()
          ->app_context()
          .CallInUIThread([&]() {
            new xe::ui::HostNotificationWindow(imgui_drawer, "Added Friend",
                                               desc, 0);
          });
    }
    ImGui::EndDisabled();

    ImGui::EndPopup();
  }

  return true;
}

bool xeDrawFriendsContent(xe::ui::ImGuiDrawer* imgui_drawer,
                          UserProfile* profile, ui::FriendsContentArgs& args,
                          std::vector<FriendPresenceObjectJSON>* presences) {
  if (!profile || !presences) {
    return false;
  }

  uint32_t user_index =
      kernel_state()->xam_state()->GetUserIndexAssignedToProfileFromXUID(
          profile->GetLogonXUID());

  ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImVec2 center = viewport->GetCenter();

  ImGui::SetNextWindowSizeConstraints(ImVec2(400, 205), ImVec2(400, 600));
  ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  if (ImGui::BeginPopupModal("Friends", &args.friends_open,
                             ImGuiWindowFlags_NoCollapse |
                                 ImGuiWindowFlags_AlwaysAutoResize |
                                 ImGuiWindowFlags_AlwaysVerticalScrollbar)) {
    ImGui::SetWindowFontScale(1.05f);

    const float window_width = ImGui::GetContentRegionAvail().x;

    float btn_height = 25;
    float btn_width =
        (window_width * 0.5f) - (ImGui::GetStyle().ItemSpacing.x * 0.5f);
    ImVec2 half_width_btn = ImVec2(btn_width, btn_height);

    ImGui::Text("Search:");
    ImVec2 drawing_start_position = ImGui::GetCursorPos();

    if (args.first_draw) {
      args.first_draw = false;
      ImGui::SetKeyboardFocusHere();
    }

    args.filter.Draw("##Search", window_width);

    ImVec2 pos = ImGui::GetItemRectMin();
    ImVec2 size = ImGui::GetItemRectSize();

    if (std::string(args.filter.InputBuf).empty()) {
      ImGui::SetCursorScreenPos(ImVec2(pos.x + 4, pos.y + (size.y / 6)));
      ImGui::TextDisabled("Gamertag or XUID...");
      ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + size.y + 4));
    }

    ImVec2 drawing_end_position = ImGui::GetCursorPos();

    ImGui::SetCursorPos(drawing_start_position);

    auto selectable_area =
        ImVec2(drawing_end_position.x - drawing_start_position.x,
               (drawing_end_position.y - drawing_start_position.y));

    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, IM_COL32(0, 0, 0, 0));
    if (ImGui::Selectable("##SelectableFriends", false,
                          ImGuiSelectableFlags_SpanAllColumns ||
                              ImGuiSelectableFlags_Disabled,
                          selectable_area)) {
    }
    ImGui::PopStyleColor(2);

    if (ImGui::BeginPopupContextItem("##SearchFilter")) {
      if (ImGui::MenuItem("Paste")) {
        const char* clipboard = ImGui::GetClipboardText();

        if (clipboard && strlen(clipboard)) {
          memset(args.filter.InputBuf, 0, sizeof(args.filter.InputBuf));

          strncpy(args.filter.InputBuf, clipboard,
                  sizeof(args.filter.InputBuf) - 1);

          args.filter.Build();
        }
      }

      ImGui::Separator();

      if (ImGui::MenuItem("Clear")) {
        memset(args.filter.InputBuf, 0, sizeof(args.filter.InputBuf));
        args.filter.Build();
      }

      ImGui::EndPopup();
    }

    const std::string friends_count =
        fmt::format("{}/100", profile->GetFriendsCount());

    ImGui::SetCursorPosX((ImGui::GetCursorPosX() + window_width -
                          ImGui::CalcTextSize(friends_count.c_str()).x));
    ImGui::Text(friends_count.c_str());

    ImGui::SetCursorPosY((ImGui::GetCursorPosY() - ImGui::GetTextLineHeight()) -
                         4);

    ImGui::Text("Filters:");

    ImGui::Checkbox("Joinable", &args.filter_joinable);
    ImGui::SameLine();
    ImGui::Checkbox("Same Game", &args.filter_title);
    ImGui::SameLine();
    ImGui::Checkbox("Hide Offline", &args.filter_offline);

    ImGui::Spacing();
    ImGui::Spacing();

    if (ImGui::Button("Add Friend",
                      ImVec2(ImGui::GetContentRegionAvail().x, btn_height))) {
      args.add_friend_args.add_friend_open = true;
      ImGui::OpenPopup("Add Friend");
    }

    ImGui::BeginDisabled(!profile->GetFriendsCount());
    if (ImGui::Button("Refresh", half_width_btn)) {
      args.refresh_presence = true;
      *presences = {};
    }
    ImGui::EndDisabled();

    ImGui::SameLine();

    ImGui::BeginDisabled(!profile->GetFriendsCount());
    if (ImGui::Button("Remove All", half_width_btn)) {
      ImGui::OpenPopup("Remove All Friends");
    }
    ImGui::EndDisabled();

    xeDrawAddFriend(imgui_drawer, profile, args.add_friend_args);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (args.refresh_presence || args.refresh_presence_sync ||
        args.add_friend_args.added_friend) {
      auto run = [presences, user_index]() {
        *presences = kernel::XLiveAPI::GetAllFriendsPresence(user_index);
      };

      if (args.refresh_presence_sync) {
        run();

        args.refresh_presence_sync = false;
      } else {
        std::thread get_presences_thread(run);
        get_presences_thread.detach();

        args.refresh_presence = false;
        args.add_friend_args.added_friend = false;
      }
    }

    for (uint32_t index = 0; auto& presence : *presences) {
      bool filter_gamertags =
          args.filter.PassFilter(presence.Gamertag().c_str());
      bool filter_xuid = args.filter.PassFilter(
          fmt::format("{:016X}", presence.XUID().get()).c_str());

      if (filter_gamertags || filter_xuid) {
        if (profile->GetOnlineXUID() == presence.XUID()) {
          continue;
        }

        const bool same_title =
            presence.TitleIDValue() &&
            presence.TitleIDValue() == kernel_state()->title_id();

        if (args.filter_joinable && (!presence.SessionID() || !same_title)) {
          continue;
        }

        if (args.filter_title && !same_title) {
          continue;
        }

        if (args.filter_offline &&
            (!presence.State() || !IsValidXUID(presence.XUID()))) {
          continue;
        }

        uint64_t selected_xuid_ = 0;
        uint64_t removed_xuid_ = 0;
        xeDrawFriendContent(imgui_drawer, profile, presence, &selected_xuid_,
                            &removed_xuid_);

        if (removed_xuid_) {
          presences->erase(presences->begin() + index);
          removed_xuid_ = 0;
        }

        ImGui::Separator();
        ImGui::Spacing();
        ImGui::Spacing();
      }

      index++;
    }

    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(225, 90), ImVec2(225, 90));
    if (ImGui::BeginPopupModal("Remove All Friends", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
      float btn_width = (ImGui::GetContentRegionAvail().x * 0.5f) -
                        (ImGui::GetStyle().ItemSpacing.x * 0.5f);
      ImVec2 btn_size = ImVec2(btn_width, btn_height);

      const std::string desc = "Are you sure?";

      ImVec2 desc_size = ImGui::CalcTextSize(desc.c_str());

      ImGui::SetCursorPosX((ImGui::GetWindowWidth() - desc_size.x) * 0.5f);
      ImGui::Text(desc.c_str());
      ImGui::Separator();

      if (ImGui::Button("Yes", btn_size)) {
        profile->RemoveAllFriends();

        *presences = {};

        kernel_state()->BroadcastNotification(
            kXNotificationFriendsFriendRemoved, user_index);

        kernel_state()
            ->emulator()
            ->display_window()
            ->app_context()
            .CallInUIThread([&]() {
              new xe::ui::HostNotificationWindow(
                  imgui_drawer, "Removed All Friends", "Success", 0);
            });

        ImGui::CloseCurrentPopup();
      }

      ImGui::SameLine();

      if (ImGui::Button("Cancel", btn_size)) {
        ImGui::CloseCurrentPopup();
      }

      ImGui::EndPopup();
    }

    ImGui::EndPopup();
  }

  return true;
}

bool xeDrawSessionContent(xe::ui::ImGuiDrawer* imgui_drawer,
                          UserProfile* profile,
                          std::unique_ptr<SessionObjectJSON>& session) {
  const uint32_t user_index =
      kernel_state()->xam_state()->GetUserIndexAssignedToProfileFromXUID(
          profile->GetOnlineXUID());

  const auto& title_version = kernel_state()->emulator()->title_version();

  const auto& media_id = kernel_state()
                             ->GetExecutableModule()
                             ->xex_module()
                             ->opt_execution_info()
                             ->media_id;

  const std::string mediaId_str = fmt::format("{:08X}", media_id.get());

  bool version_mismatch = title_version != session->Version();
  bool media_id_mismatch = mediaId_str != session->MediaID();

  uint32_t num_players =
      session->FilledPublicSlotsCount() + session->FilledPrivateSlotsCount();

  ImGui::Text(fmt::format("Players: {}", num_players).c_str());

  if (!session->Version().empty()) {
    ImGui::Text(fmt::format("Version: {}", session->Version()).c_str());
  }

  if (!session->MediaID().empty()) {
    ImGui::Text(fmt::format("Media ID: {}", session->MediaID()).c_str());
  }

  ImGui::Text(fmt::format("Open Private Slots: {}",
                          session->OpenPrivateSlotsCount().get())
                  .c_str());
  ImGui::Text(fmt::format("Open Public Slots: {}",
                          session->OpenPublicSlotsCount().get())
                  .c_str());

  ImGui::Spacing();
  ImGui::Spacing();

  const std::string join_label =
      std::format("Join Session##{}", session->SessionID());

  bool caller = MacAddress(session->MacAddress()).to_uint64() ==
                XLiveAPI::mac_address_->to_uint64();

  std::string version_text = "Version mismatch!";
  std::string media_text = "Media ID mismatch!";

  auto version_width_btn = (ImGui::GetContentRegionAvail().x -
                            ImGui::CalcTextSize(version_text.c_str()).x) *
                           0.5f;

  auto media_id_width_btn = (ImGui::GetContentRegionAvail().x -
                             ImGui::CalcTextSize(media_text.c_str()).x) *
                            0.5f;

  ImGui::SetCursorPosX(version_width_btn);
  ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(240, 50, 50, 255));
  if (version_mismatch && !session->Version().empty()) {
    ImGui::Text("Version mismatch!");
  }

  ImGui::SetCursorPosX(media_id_width_btn);
  if (media_id_mismatch && !session->MediaID().empty()) {
    ImGui::Text("Media ID mismatch!");
  }
  ImGui::PopStyleColor();

  ImGui::Spacing();
  ImGui::Spacing();

  // What is player presence session is null?
  ImGui::BeginDisabled(!session->SessionID_UInt() || caller);
  if (ImGui::Button(join_label.c_str(),
                    ImVec2(ImGui::GetContentRegionAvail().x, 25))) {
    X_INVITE_INFO* invite = profile->GetSelfInvite();

    memset(invite, 0, sizeof(X_INVITE_INFO));

    invite->from_game_invite = false;
    invite->title_id = kernel_state()->title_id();
    invite->xuid_invitee = profile->GetOnlineXUID();
    invite->xuid_inviter = session->XUID_UInt();

    kernel_state()->BroadcastNotification(kXNotificationLiveInviteAccepted,
                                          user_index);
  }
  ImGui::EndDisabled();

  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
    if (caller) {
      ImGui::SetTooltip("Cannot join session from the same console.");
    } else {
      ImGui::SetTooltip("Join gaming session");
    }
  }

  return true;
}

bool xeDrawSessionsContent(
    xe::ui::ImGuiDrawer* imgui_drawer, UserProfile* profile,
    ui::SessionsContentArgs& sessions_args,
    std::vector<std::unique_ptr<SessionObjectJSON>>* sessions) {
  ImVec2 center = ImGui::GetMainViewport()->GetCenter();

  ImGui::SetNextWindowSizeConstraints(ImVec2(300, 150), ImVec2(300, 600));
  ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  if (ImGui::BeginPopupModal("Sessions", &sessions_args.sessions_open,
                             ImGuiWindowFlags_NoCollapse |
                                 ImGuiWindowFlags_AlwaysAutoResize |
                                 ImGuiWindowFlags_AlwaysVerticalScrollbar)) {
    ImGui::SetWindowFontScale(1.05f);

    bool in_game = kernel_state()->emulator()->title_id();

    if (in_game) {
      ImGui::Text(
          fmt::format("{}", kernel_state()->emulator()->title_name()).c_str());
    }

    ImGui::Spacing();
    ImGui::Spacing();

    ImGui::Text(
        fmt::format("Available Sessions: {}", sessions->size()).c_str());

    ImGui::Spacing();
    ImGui::Spacing();

    ImGui::Checkbox("Hide My Sessions", &sessions_args.filter_own);

    ImGui::Spacing();

    if (ImGui::Button("Refresh",
                      ImVec2(ImGui::GetContentRegionAvail().x, 25))) {
      sessions->clear();
      sessions_args.refresh_sessions = true;
    }

    ImGui::Separator();

    ImGui::Spacing();
    ImGui::Spacing();

    if (sessions_args.refresh_sessions || sessions_args.refresh_sessions_sync) {
      auto run = [sessions]() { *sessions = XLiveAPI::GetTitleSessions(); };

      if (sessions_args.refresh_sessions_sync) {
        run();

        sessions_args.refresh_sessions_sync = false;
      } else {
        std::thread refresh_sessions_thread(run);
        refresh_sessions_thread.detach();

        sessions_args.refresh_sessions = false;
      }
    }

    for (auto& session : *sessions) {
      bool caller = MacAddress(session->MacAddress()).to_uint64() ==
                    XLiveAPI::mac_address_->to_uint64();

      if (sessions_args.filter_own && caller) {
        continue;
      }

      xeDrawSessionContent(imgui_drawer, profile, session);

      ImGui::Separator();
      ImGui::Spacing();
      ImGui::Spacing();
    }

    ImGui::EndPopup();
  }

  return true;
}

bool xeDrawMyDeletedProfiles(
    xe::ui::ImGuiDrawer* imgui_drawer, ui::MyDeletedProfilesArgs& args,
    std::map<uint64_t, std::string>* deleted_profiles) {
  if (!deleted_profiles) {
    return false;
  }

  ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImVec2 center = viewport->GetCenter();

  float btn_height = 25;
  ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSizeConstraints(ImVec2(250, 115), ImVec2(250, 415));
  if (ImGui::BeginPopupModal("Deleted Profiles", &args.deleted_profiles_open,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    float btn_width = (ImGui::GetContentRegionAvail().x * 0.5f) -
                      (ImGui::GetStyle().ItemSpacing.x * 0.5f);
    ImVec2 btn_size = ImVec2(btn_width, btn_height);

    const std::string desc =
        fmt::format("Deleted Profiles: {}", deleted_profiles->size());

    ImVec2 desc_size = ImGui::CalcTextSize(desc.c_str());

    ImGui::SetCursorPosX((ImGui::GetWindowWidth() - desc_size.x) * 0.5f);
    ImGui::Text(desc.c_str());
    ImGui::Separator();
    ImGui::Spacing();

    for (const auto& [xuid, gamertag] : *deleted_profiles) {
      ImGui::Spacing();
      ImGui::Spacing();

      std::string xuid_str = fmt::format("XUID: {:016X}", xuid);
      std::string gamertag_str = fmt::format("Gamertag: {}", gamertag);

      ImGui::Text(xuid_str.c_str());
      ImGui::Text(gamertag_str.c_str());

      ImGui::Separator();
    }

    ImGui::EndPopup();
  }

  return true;
}

X_RESULT xeXamShowSigninUI(uint32_t user_index, uint32_t users_needed,
                           uint32_t flags) {
  // Mask values vary. Probably matching user types? Local/remote?
  // Games seem to sit and loop until we trigger sign in notification.
  if (users_needed != 1 && users_needed != 2 && users_needed != 4) {
    return X_ERROR_INVALID_PARAMETER;
  }

  if (cvars::headless) {
    return xeXamDispatchHeadlessAsync([users_needed]() {
      std::map<uint8_t, uint64_t> xuids;

      for (uint32_t i = 0; i < XUserMaxUserCount; i++) {
        UserProfile* profile = kernel_state()->xam_state()->GetUserProfile(i);
        if (profile) {
          xuids[i] = profile->xuid();
          if (xuids.size() >= users_needed) break;
        }
      }

      kernel_state()->xam_state()->profile_manager()->LoginMultiple(xuids);
    });
  }

  auto close = [](ui::SigninUI* dialog) -> void {};

  const Emulator* emulator = kernel_state()->emulator();
  xe::ui::ImGuiDrawer* imgui_drawer = emulator->imgui_drawer();
  return xeXamDispatchDialogAsync<ui::SigninUI>(
      new ui::SigninUI(
          imgui_drawer, kernel_state()->xam_state()->profile_manager(),
          emulator->input_system()->GetLastUsedSlot(), users_needed, flags),
      close);
}

X_RESULT xeXamShowCreateProfileUIEx(uint32_t user_index, dword_t unkn,
                                    char* unkn2_ptr) {
  Emulator* emulator = kernel_state()->emulator();
  xe::ui::ImGuiDrawer* imgui_drawer = emulator->imgui_drawer();

  if (cvars::headless) {
    return X_ERROR_SUCCESS;
  }

  auto close = [](ui::CreateProfileUI* dialog) -> void {};

  return xeXamDispatchDialogAsync<ui::CreateProfileUI>(
      new ui::CreateProfileUI(imgui_drawer, emulator), close);
}

dword_result_t XamShowSigninUI_entry(dword_t users_needed, dword_t flags) {
  return xeXamShowSigninUI(XUserIndexAny, users_needed, flags);
}
DECLARE_XAM_EXPORT1(XamShowSigninUI, kUserProfiles, kImplemented);

dword_result_t XamShowSigninUIp_entry(dword_t user_index, dword_t users_needed,
                                      dword_t flags) {
  return xeXamShowSigninUI(user_index, users_needed, flags);
}
DECLARE_XAM_EXPORT1(XamShowSigninUIp, kUserProfiles, kImplemented);

dword_result_t XamShowCreateProfileUIEx_entry(dword_t user_index, dword_t unkn,
                                              lpstring_t unkn2_ptr) {
  return xeXamShowCreateProfileUIEx(user_index, unkn, unkn2_ptr);
}
DECLARE_XAM_EXPORT1(XamShowCreateProfileUIEx, kUserProfiles, kImplemented);

dword_result_t XamShowCreateProfileUI_entry(dword_t user_index, dword_t unkn) {
  return xeXamShowCreateProfileUIEx(user_index, unkn, 0);
}
DECLARE_XAM_EXPORT1(XamShowCreateProfileUI, kUserProfiles, kImplemented);

dword_result_t XamShowAchievementsUI_entry(dword_t user_index,
                                           dword_t title_id) {
  auto user = kernel_state()->xam_state()->GetUserProfile(user_index);
  if (!user) {
    return X_ERROR_NO_SUCH_USER;
  }

  uint32_t proper_title_id =
      title_id ? title_id.value()
               : kernel_state()->xam_state()->spa_info()->title_id();

  const auto info =
      kernel_state()->xam_state()->user_tracker()->GetUserTitleInfo(
          user->xuid(), proper_title_id);

  if (!info) {
    return X_ERROR_NO_SUCH_USER;
  }

  xe::ui::ImGuiDrawer* imgui_drawer =
      kernel_state()->emulator()->imgui_drawer();

  auto close = [](ui::GameAchievementsUI* dialog) -> void {};
  return xeXamDispatchDialogAsync<ui::GameAchievementsUI>(
      new ui::GameAchievementsUI(imgui_drawer, ImVec2(100.f, 100.f),
                                 &info.value(), user),
      close);
}
DECLARE_XAM_EXPORT1(XamShowAchievementsUI, kUserProfiles, kStub);

dword_result_t XamShowGamerCardUI_entry(dword_t user_index) {
  auto user = kernel_state()->xam_state()->GetUserProfile(user_index);
  if (!user) {
    return X_ERROR_ACCESS_DENIED;
  }

  xe::ui::ImGuiDrawer* imgui_drawer =
      kernel_state()->emulator()->imgui_drawer();

  auto close = [](ui::GamercardUI* dialog) -> void {};
  return xeXamDispatchDialogAsync<ui::GamercardUI>(
      new ui::GamercardUI(kernel_state()->emulator()->display_window(),
                          imgui_drawer, kernel_state(), user->xuid()),
      close);
}
DECLARE_XAM_EXPORT1(XamShowGamerCardUI, kUserProfiles, kImplemented);

dword_result_t XamShowEditProfileUI_entry(dword_t user_index) {
  auto user = kernel_state()->xam_state()->GetUserProfile(user_index);
  if (!user) {
    return X_ERROR_ACCESS_DENIED;
  }

  xe::ui::ImGuiDrawer* imgui_drawer =
      kernel_state()->emulator()->imgui_drawer();

  auto close = [](ui::GamercardUI* dialog) -> void {};
  return xeXamDispatchDialogAsync<ui::GamercardUI>(
      new ui::GamercardUI(kernel_state()->emulator()->display_window(),
                          imgui_drawer, kernel_state(), user->xuid()),
      close);
}
DECLARE_XAM_EXPORT1(XamShowEditProfileUI, kUserProfiles, kImplemented);

static std::atomic<bool> gamercard_open = false;

dword_result_t XamShowGamerCardUIForXUID_entry(dword_t user_index,
                                               qword_t xuid_player) {
  // Prevent 584111F7 from opening gamercard multiple times.
  if (gamercard_open) {
    return X_ERROR_INVALID_PARAMETER;
  }

  if (user_index >= XUserMaxUserCount) {
    return X_ERROR_INVALID_PARAMETER;
  }

  if (IsGuestXUID(xuid_player)) {
    return X_ERROR_INVALID_PARAMETER;
  }

  auto user = kernel_state()->xam_state()->GetUserProfile(user_index);
  if (!user) {
    return X_ERROR_INVALID_PARAMETER;
  }

  if (xuid_player || xuid_player == user->xuid() ||
      xuid_player == user->GetOnlineXUID()) {
    auto close = [](ui::GamercardFromXUIDUI* dialog) -> void {
      gamercard_open = false;
    };

    const Emulator* emulator = kernel_state()->emulator();
    xe::ui::ImGuiDrawer* imgui_drawer = emulator->imgui_drawer();

    gamercard_open = true;

    return xeXamDispatchDialogAsync<ui::GamercardFromXUIDUI>(
        new ui::GamercardFromXUIDUI(imgui_drawer, xuid_player, user), close);
  }

  return X_ERROR_INVALID_PARAMETER;
}
DECLARE_XAM_EXPORT1(XamShowGamerCardUIForXUID, kUserProfiles, kStub);

dword_result_t XamShowFriendsUI_entry(dword_t user_index) {
  if (user_index >= XUserMaxUserCount && user_index != XUserIndexAny) {
    return X_ERROR_FUNCTION_FAILED;
  }

  UserProfile* user = nullptr;

  if (user_index == XUserIndexAny) {
    if (kernel_state()
            ->xam_state()
            ->profile_manager()
            ->IsAnyProfileSignedIn()) {
      user =
          kernel_state()->xam_state()->GetUserProfile(static_cast<uint32_t>(0));
    }
  } else {
    user = kernel_state()->xam_state()->GetUserProfile(user_index);
  }

  if (!user) {
    return X_ERROR_FUNCTION_FAILED;
  }

  const Emulator* emulator = kernel_state()->emulator();
  xe::ui::ImGuiDrawer* imgui_drawer = emulator->imgui_drawer();

  auto close = [](ui::FriendsUI* dialog) -> void {};

  return xeXamDispatchDialogAsync<ui::FriendsUI>(
      new ui::FriendsUI(imgui_drawer, user), close);
}
DECLARE_XAM_EXPORT1(XamShowFriendsUI, kUserProfiles, kImplemented);

dword_result_t XamShowCommunitySessionsUI_entry(dword_t user_index,
                                                dword_t social_sessions_flags) {
  if (user_index >= XUserMaxUserCount && user_index != XUserIndexAny) {
    return X_ERROR_FUNCTION_FAILED;
  }

  UserProfile* user = nullptr;

  if (user_index == XUserIndexAny) {
    if (kernel_state()
            ->xam_state()
            ->profile_manager()
            ->IsAnyProfileSignedIn()) {
      user =
          kernel_state()->xam_state()->GetUserProfile(static_cast<uint32_t>(0));
    }
  } else {
    user = kernel_state()->xam_state()->GetUserProfile(user_index);
  }

  if (!user) {
    return X_ERROR_FUNCTION_FAILED;
  }

  const Emulator* emulator = kernel_state()->emulator();
  xe::ui::ImGuiDrawer* imgui_drawer = emulator->imgui_drawer();

  auto close = [](ui::ShowCommunitySessionsUI* dialog) -> void {};

  return xeXamDispatchDialogAsync<ui::ShowCommunitySessionsUI>(
      new ui::ShowCommunitySessionsUI(imgui_drawer, user), close);
}
DECLARE_XAM_EXPORT1(XamShowCommunitySessionsUI, kUserProfiles, kImplemented);

}  // namespace xam
}  // namespace kernel
}  // namespace xe

DECLARE_XAM_EMPTY_REGISTER_EXPORTS(UI);
