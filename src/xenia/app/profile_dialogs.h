/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_APP_PROFILE_DIALOGS_H_
#define XENIA_APP_PROFILE_DIALOGS_H_

#include "xenia/app/updater.h"
#include "xenia/kernel/json/friend_presence_object_json.h"
#include "xenia/kernel/json/session_object_json.h"
#include "xenia/kernel/xam/ui/netplay_manager_util.h"
#include "xenia/ui/imgui_dialog.h"
#include "xenia/ui/imgui_drawer.h"
#include "xenia/xbox.h"

namespace xe {
namespace app {

class EmulatorWindow;

class NoProfileDialog final : public ui::ImGuiDialog {
 public:
  NoProfileDialog(ui::ImGuiDrawer* imgui_drawer,
                  EmulatorWindow* emulator_window)
      : ui::ImGuiDialog(imgui_drawer), emulator_window_(emulator_window) {}

 protected:
  void OnDraw(ImGuiIO& io) override;

  EmulatorWindow* emulator_window_;
};

class ProfileConfigDialog final : public ui::ImGuiDialog {
 public:
  ProfileConfigDialog(ui::ImGuiDrawer* imgui_drawer,
                      EmulatorWindow* emulator_window)
      : ui::ImGuiDialog(imgui_drawer), emulator_window_(emulator_window) {
    LoadProfileIcon();
  }

 protected:
  void OnDraw(ImGuiIO& io) override;

 private:
  void LoadProfileIcon();
  void LoadProfileIcon(const uint64_t xuid);

  std::map<uint64_t, std::unique_ptr<ui::ImmediateTexture>> profile_icon_;

  uint64_t selected_xuid_ = 0;
  EmulatorWindow* emulator_window_;
};

class ManagerDialog final : public ui::ImGuiDialog {
 public:
  ManagerDialog(ui::ImGuiDrawer* imgui_drawer, EmulatorWindow* emulator_window)
      : ui::ImGuiDialog(imgui_drawer), emulator_window_(emulator_window) {}

 protected:
  void OnDraw(ImGuiIO& io) override;

 private:
  bool manager_opened_ = false;
  uint64_t selected_xuid_ = 0;
  uint64_t removed_xuid_ = 0;
  xe::kernel::xam::ui::FriendsContentArgs friends_args = {};
  xe::kernel::xam::ui::SessionsContentArgs sessions_args = {};
  xe::kernel::xam::ui::MyDeletedProfilesArgs deletion_args = {};
  std::vector<xe::kernel::FriendPresenceObjectJSON> presences;
  std::vector<std::unique_ptr<xe::kernel::SessionObjectJSON>> sessions;
  std::map<uint64_t, std::string> deleted_profiles;
  EmulatorWindow* emulator_window_;
};

class UpdaterDialog final : public ui::ImGuiDialog {
 public:
  UpdaterDialog(Updater* updater, ui::ImGuiDrawer* imgui_drawer,
                EmulatorWindow* emulator_window)
      : ui::ImGuiDialog(imgui_drawer), emulator_window_(emulator_window) {
    updater_ = updater;
  }

 protected:
  void OnDraw(ImGuiIO& io) override;

 private:
  bool updater_opened_ = false;
  Updater* updater_ = nullptr;
  uint32_t response_code_ = 0;
  bool update_available_ = false;
  bool checked_for_updates_ = false;
  bool downloading_ = false;
  bool downloaded_ = false;
  bool downloaded_failed_ = false;
  bool hide_download_button_ = false;
  bool show_replace_dialog_ = false;
  bool replace_file_ = false;
  std::filesystem::path downloaded_file_path_;
  const std::string windows_artifact_name_ = "xenia_canary_netplay_windows.zip";
  std::string latest_commit_hash_ = "";
  std::string latest_commit_date_ = "";
  std::vector<std::string> commit_messages_ = {};
  std::string changelog_ = "";
  EmulatorWindow* emulator_window_;
};

}  // namespace app
}  // namespace xe

#endif
