/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XAM_USER_PROFILE_H_
#define XENIA_KERNEL_XAM_USER_PROFILE_H_

#include <map>
#include <memory>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

#include "xenia/base/cvar.h"
#include "xenia/kernel/xam/user_property.h"
#include "xenia/kernel/xam/xdbf/gpd_info_profile.h"
#include "xenia/kernel/xam/xdbf/gpd_info_title.h"
#include "xenia/kernel/xnet.h"
#include "xenia/xbox.h"

DECLARE_int32(network_mode);

namespace xe {
namespace kernel {
namespace xam {

enum class X_USER_SIGNIN_STATE : uint32_t {
  NotSignedIn,
  SignedInLocally,
  SignedInToLive
};

enum class X_USER_PROFILE_SETTING_SOURCE : uint32_t {
  NO_VALUE = 0,
  DEFAULT = 1,  // Default value taken from default OS values.
  TITLE = 2,    // Value written by title or OS.
  PERMISSION_DENIED = 3,
};

struct X_USER_PROFILE_SETTING {
  xe::be<X_USER_PROFILE_SETTING_SOURCE> source;
  union {
    xe::be<uint32_t> user_index;
    xe::be<uint64_t> xuid;
  };
  xe::be<uint32_t> setting_id;
  union {
    uint8_t data_bytes[sizeof(X_USER_DATA)];
    X_USER_DATA data;
  };
};
static_assert_size(X_USER_PROFILE_SETTING, 40);

enum class X_USER_PROFILE_GAMERCARD_ZONE_OPTIONS {
  GAMERCARD_ZONE_NONE,
  GAMERCARD_ZONE_RR,
  GAMERCARD_ZONE_PRO,
  GAMERCARD_ZONE_FAMILY,
  GAMERCARD_ZONE_UNDERGROUND
};

enum class XTileType {
  kAchievement,
  kGameIcon,
  kGamerTile,
  kGamerTileSmall,
  kLocalGamerTile,
  kLocalGamerTileSmall,
  kBkgnd,
  kAwardedGamerTile,
  kAwardedGamerTileSmall,
  kGamerTileByImageId,
  kPersonalGamerTile,
  kPersonalGamerTileSmall,
  kGamerTileByKey,
  kAvatarGamerTile,
  kAvatarGamerTileSmall,
  kAvatarFullBody
};

// TODO: find filenames of other tile types that are stored in profile
inline const std::map<XTileType, std::string> kTileFileNames = {
    {XTileType::kGamerTile, "tile_64.png"},
    {XTileType::kGamerTileSmall, "tile_32.png"},
    {XTileType::kPersonalGamerTile, "tile_64.png"},
    {XTileType::kPersonalGamerTileSmall, "tile_32.png"},
    {XTileType::kAvatarGamerTile, "avtr_64.png"},
    {XTileType::kAvatarGamerTileSmall, "avtr_32.png"},
};

static constexpr std::pair<uint16_t, uint16_t> kProfileIconSize = {64, 64};
static constexpr std::pair<uint16_t, uint16_t> kProfileIconSizeSmall = {32, 32};

class UserProfile {
 public:
  UserProfile(const uint64_t xuid, const X_XAMACCOUNTINFO* account_info);

  uint64_t xuid() const { return xuid_; }
  uint64_t GetOnlineXUID() const {
    return IsLiveEnabled() ? static_cast<uint64_t>(account_info_.xuid_online)
                           : 0;
  }
  uint64_t GetLogonXUID() const {
    return IsLiveEnabled() &&
                   signin_state() == X_USER_SIGNIN_STATE::SignedInToLive
               ? static_cast<uint64_t>(account_info_.xuid_online)
               : xuid();
  }

  std::string name() const { return account_info_.GetGamertagString(); }
  X_USER_SIGNIN_STATE signin_state() const {
    return IsLiveEnabled() && cvars::network_mode == NETWORK_MODE::XBOXLIVE
               ? X_USER_SIGNIN_STATE::SignedInToLive
               : X_USER_SIGNIN_STATE::SignedInLocally;
  }

  uint32_t GetReservedFlags() const {
    return account_info_.GetReservedFlags();
  };
  uint32_t GetCachedFlags() const { return account_info_.GetCachedFlags(); };
  uint32_t GetCountry() const {
    return static_cast<uint32_t>(account_info_.GetCountry());
  };
  uint32_t GetSubscriptionTier() const {
    return account_info_.GetSubscriptionTier();
  }
  uint32_t GetLanguage() const {
    return static_cast<uint32_t>(account_info_.GetLanguage());
  };

  bool IsParentalControlled() const {
    return account_info_.IsParentalControlled();
  };
  bool IsLiveEnabled() const { return account_info_.IsLiveEnabled(); }

  std::span<const uint8_t> GetProfileIcon(XTileType icon_type) {
    // Overwrite same types?
    if (icon_type == XTileType::kPersonalGamerTile ||
        icon_type == XTileType::kLocalGamerTile) {
      icon_type = XTileType::kGamerTile;
    }

    if (icon_type == XTileType::kPersonalGamerTileSmall ||
        icon_type == XTileType::kLocalGamerTileSmall) {
      icon_type = XTileType::kGamerTileSmall;
    }

    if (profile_images_.find(icon_type) == profile_images_.cend()) {
      return {};
    }

    return {profile_images_[icon_type].data(),
            profile_images_[icon_type].size()};
  }

  void GetPasscode(uint16_t* passcode) const {
    std::memcpy(passcode, account_info_.passcode,
                sizeof(account_info_.passcode));
  };

  friend class UserTracker;
  friend class GpdAchievementBackend;

  static X_ONLINE_FRIEND GenerateDummyFriend();

  void AddDummyFriends(const uint32_t friends_count);

  bool GetFriendPresenceFromXUID(const uint64_t xuid,
                                 X_ONLINE_PRESENCE* presence);

  bool SetFriend(const X_ONLINE_FRIEND& update_peer);
  bool AddFriendFromXUID(const uint64_t xuid);
  bool AddFriend(X_ONLINE_FRIEND* add_friend);
  bool RemoveFriend(const X_ONLINE_FRIEND& peer);
  bool RemoveFriend(const uint64_t xuid);
  void RemoveAllFriends();

  bool GetFriendFromIndex(const uint32_t index, X_ONLINE_FRIEND* peer);
  bool GetFriendFromXUID(const uint64_t xuid, X_ONLINE_FRIEND* peer);
  bool IsFriend(const uint64_t xuid, X_ONLINE_FRIEND* peer = nullptr);

  const std::vector<X_ONLINE_FRIEND> GetFriends() const { return friends_; }
  const std::vector<uint64_t> GetFriendsXUIDs() const;

  const uint32_t GetFriendsCount() const;

  bool SetSubscriptionFromXUID(const uint64_t xuid, X_ONLINE_PRESENCE* peer);
  bool GetSubscriptionFromXUID(const uint64_t xuid, X_ONLINE_PRESENCE* peer);
  bool SubscribeFromXUID(const uint64_t xuid);
  bool UnsubscribeFromXUID(const uint64_t xuid);
  bool IsSubscribed(const uint64_t xuid);

  void SetSelfInvite(X_INVITE_INFO* invite_info);
  X_INVITE_INFO* GetSelfInvite() { return &self_invite; };

  const std::vector<uint64_t> GetSubscribedXUIDs() const;

  bool MutePlayer(uint64_t xuid);
  bool UnmutePlayer(uint64_t xuid);
  bool IsPlayerMuted(uint64_t xuid) const;

  std::u16string GetPresenceString() const;
  bool UpdatePresence();
  bool BuildPresenceString();

 private:
  uint64_t xuid_;
  X_XAMACCOUNTINFO account_info_;
  X_INVITE_INFO self_invite;

  GpdInfoProfile dashboard_gpd_;
  std::map<uint32_t, GpdInfoTitle> games_gpd_;
  std::vector<Property> properties_;  // Includes contexts!
  std::vector<X_ONLINE_FRIEND> friends_;
  std::map<uint64_t, X_ONLINE_PRESENCE> subscriptions_;
  std::vector<uint64_t> muted_players_;

  std::map<XTileType, std::vector<uint8_t>> profile_images_;
  std::u16string online_presence_desc_ = u"";

  GpdInfo* GetGpd(const uint32_t title_id);
  const GpdInfo* GetGpd(const uint32_t title_id) const;

  void LoadProfileGpds();
  void LoadProfileIcon(XTileType tile_type);
  void WriteProfileIcon(XTileType tile_type,
                        std::span<const uint8_t> icon_data);
  std::vector<uint8_t> LoadGpd(const uint32_t title_id);
  bool WriteGpd(const uint32_t title_id);
};

}  // namespace xam
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XAM_USER_PROFILE_H_
