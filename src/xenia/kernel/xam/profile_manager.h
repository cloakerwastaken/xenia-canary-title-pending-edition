/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XAM_PROFILE_MANAGER_H_
#define XENIA_KERNEL_XAM_PROFILE_MANAGER_H_

#include <bitset>
#include <random>
#include <string>
#include <vector>

#include "third_party/fmt/include/fmt/format.h"
#include "xenia/base/string.h"
#include "xenia/kernel/title_id_utils.h"
#include "xenia/kernel/xam/user_profile.h"
#include "xenia/xbox.h"

namespace xe {
namespace kernel {
class KernelState;
}  // namespace kernel
}  // namespace xe

namespace xe {
namespace kernel {
namespace xam {
class UserTracker;
}  // namespace xam
}  // namespace kernel
}  // namespace xe

namespace xe {
namespace kernel {
namespace xam {

inline const std::string kDashboardStringID =
    fmt::format("{:08X}", kDashboardID);

constexpr std::string_view kDefaultMountFormat = "User_{:016X}";

class ProfileManager {
 public:
  static bool DecryptAccountFile(const uint8_t* data, X_XAMACCOUNTINFO* output,
                                 bool devkit = false);

  static void EncryptAccountFile(const X_XAMACCOUNTINFO* input, uint8_t* output,
                                 bool devkit = false);

  // Profile:
  //  - Account
  //  - GPDs (Dashboard, titles)

  // Loading Profile means load everything
  // Loading Account means load basic data
  ProfileManager(KernelState* kernel_state, UserTracker* user_tracker);

  ~ProfileManager() = default;

  bool CreateProfile(const std::string gamertag, bool autologin,
                     bool default_xuid = false);
  bool CreateProfile(const X_XAMACCOUNTINFO* account_info, uint64_t xuid);

  bool DeleteProfile(const uint64_t xuid);

  bool MountProfile(const uint64_t xuid, std::string mount_path = "");
  bool DismountProfile(const uint64_t xuid);

  void Login(const uint64_t xuid, const uint8_t user_index = XUserIndexAny,
             bool notify = true);
  void Logout(const uint8_t user_index, bool notify = true);
  void LoginMultiple(const std::map<uint8_t, uint64_t>& profiles);

  bool LoadAccount(const uint64_t xuid);

  void ReloadProfiles();
  void ReloadProfile(const uint64_t xuid);

  UserProfile* GetProfile(const uint64_t xuid) const;
  UserProfile* GetProfile(const uint8_t user_index) const;
  uint8_t GetUserIndexAssignedToProfile(const uint64_t xuid) const;

  const std::map<uint64_t, X_XAMACCOUNTINFO>* GetAccounts() {
    return &accounts_;
  }
  const X_XAMACCOUNTINFO* GetAccount(const uint64_t xuid);

  uint32_t GetAccountCount() const {
    return static_cast<uint32_t>(accounts_.size());
  }
  bool IsAnyProfileSignedIn() const { return !logged_profiles_.empty(); }

  std::filesystem::path GetProfileContentPath(
      const uint64_t xuid, const uint32_t title_id = -1,
      const XContentType content_type = XContentType::kInvalid) const;

  bool UpdateAccount(const uint64_t xuid, const X_XAMACCOUNTINFO* account);

  static bool IsGamertagValid(const std::string gamertag);

 private:
  void UpdateConfig(const uint64_t xuid, const uint8_t slot);
  bool CreateAccount(const uint64_t xuid, const std::string gamertag);
  bool CreateAccount(const uint64_t xuid, const X_XAMACCOUNTINFO* account);

  std::filesystem::path GetProfilePath(const uint64_t xuid) const;
  std::filesystem::path GetProfilePath(const std::string xuid) const;

  std::vector<uint64_t> FindProfiles() const;

  uint8_t FindFirstFreeProfileSlot() const;
  std::bitset<XUserMaxUserCount> GetUsedUserSlots() const;

  uint64_t GenerateXuid() const {
    std::random_device rd;
    std::mt19937 gen(rd());

    return ((uint64_t)0xE03 << 52) + (gen() % (1 << 31));
  }

  std::map<uint64_t, X_XAMACCOUNTINFO> accounts_;
  std::map<uint8_t, std::unique_ptr<UserProfile>> logged_profiles_;

  KernelState* kernel_state_;
  UserTracker* user_tracker_;
};

}  // namespace xam
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XAM_PROFILE_MANAGER_H_
