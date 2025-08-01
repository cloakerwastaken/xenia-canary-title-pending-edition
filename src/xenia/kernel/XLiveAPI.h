/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Xenia Emulator. All rights reserved.                        *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XLIVEAPI_H_
#define XENIA_KERNEL_XLIVEAPI_H_

#include <span>
#include <unordered_set>

#include "third_party/libcurl/include/curl/curl.h"

#include "xenia/base/byte_order.h"
#include "xenia/kernel/upnp.h"
#include "xenia/kernel/util/net_utils.h"
#include "xenia/kernel/xsession.h"

#include "xenia/kernel/json/arbitration_object_json.h"
#include "xenia/kernel/json/delete_my_profiles_json.h"
#include "xenia/kernel/json/find_users_object_json.h"
#include "xenia/kernel/json/friend_presence_object_json.h"
#include "xenia/kernel/json/http_response_object_json.h"
#include "xenia/kernel/json/leaderboard_object_json.h"
#include "xenia/kernel/json/player_object_json.h"
#include "xenia/kernel/json/presence_object_json.h"
#include "xenia/kernel/json/properties_object_json.h"
#include "xenia/kernel/json/services_json.h"
#include "xenia/kernel/json/session_object_json.h"
#include "xenia/kernel/json/xstorage_file_info_object_json.h"

#ifdef XE_PLATFORM_WIN32
#include <iphlpapi.h>
#endif  // XE_PLATFORM_WIN32

namespace xe {
namespace kernel {

class XLiveAPI {
 public:
  enum class InitState { Success, Failed, Pending };

  static void IpGetConsoleXnAddr(XNADDR* XnAddr_ptr);

  static InitState GetInitState();

  static std::vector<std::string> ParseDelimitedList(std::string_view csv,
                                                     uint32_t count = 0);

  static std::string BuildCSVFromVector(std::vector<std::string>& data,
                                        uint32_t count = 0);

  static std::vector<std::string> ParseAPIList();

  static std::vector<std::uint64_t> ParseFriendsXUIDs();

  static void AddFriend(uint64_t xuid);

  static void RemoveFriend(uint64_t xuid);

  static void SetAPIAddress(std::string address);

  static void SetNetworkInterfaceByGUID(std::string guid);

  static void SetNetworkMode(uint32_t mode);

  static std::string GetApiAddress();

  static uint32_t GetNatType();

  static bool IsConnectedToServer();

  static bool IsConnectedToLAN();

  static uint16_t GetPlayerPort();

  static int8_t GetVersionStatus();

  static void Init();

  static void clearXnaddrCache();

  static sockaddr_in Getwhoami();

  static void DownloadPortMappings();

  static const uint64_t GetMachineId(const uint64_t macAddress);

  static const uint64_t GetLocalMachineId();

  static std::unique_ptr<HTTPResponseObjectJSON> RegisterPlayer();

  static const std::map<uint64_t, std::string> DeleteMyProfiles();

  static std::unique_ptr<PlayerObjectJSON> FindPlayer(std::string ip);

  static bool UpdateQoSCache(const uint64_t sessionId,
                             const std::vector<uint8_t> qos_payloade);

  static void QoSPost(uint64_t sessionId, uint8_t* qosData, size_t qosLength);

  static response_data QoSGet(uint64_t sessionId);

  static void SessionModify(uint64_t sessionId, XGI_SESSION_MODIFY* data);

  static std::vector<std::unique_ptr<SessionObjectJSON>> GetTitleSessions(
      uint32_t title_id = 0);

  static const std::vector<std::unique_ptr<SessionObjectJSON>> SessionSearch(
      XGI_SESSION_SEARCH* data, uint32_t num_users);

  static void SessionContextSet(uint64_t session_id,
                                std::map<uint32_t, uint32_t> contexts);

  static const std::map<uint32_t, uint32_t> SessionContextGet(
      uint64_t session_id);

  static void SessionPropertiesSet(uint64_t session_id, uint32_t user_index);

  static const std::vector<xam::Property> SessionPropertiesGet(
      uint64_t session_id);

  static const std::unique_ptr<SessionObjectJSON> SessionDetails(
      uint64_t sessionId);

  static std::unique_ptr<SessionObjectJSON> XSessionMigration(
      uint64_t sessionId, XGI_SESSION_MIGRATE* data);

  static std::unique_ptr<ArbitrationObjectJSON> XSessionArbitration(
      uint64_t sessionId);

  static void SessionWriteStats(uint64_t sessionId, XGI_STATS_WRITE stats);

  static std::unique_ptr<HTTPResponseObjectJSON> LeaderboardsFind(
      const uint8_t* data);

  static void DeleteSession(uint64_t sessionId);

  static void DeleteAllSessionsByMac();

  static void DeleteAllSessions();

  static void XSessionCreate(uint64_t sessionId, XGI_SESSION_CREATE* data);

  static std::unique_ptr<SessionObjectJSON> XSessionGet(uint64_t sessionId);

  static std::vector<X_TITLE_SERVER> GetServers();

  static std::unique_ptr<ServicesObjectJSON> GetServices();

  static void SessionJoinRemote(
      uint64_t sessionId, const std::unordered_map<uint64_t, bool> members);

  static void SessionLeaveRemote(uint64_t sessionId,
                                 const std::vector<xe::be<uint64_t>> xuids);

  static void SessionPreJoin(uint64_t sessionId,
                             const std::set<uint64_t>& xuids);

  static std::unique_ptr<FriendsPresenceObjectJSON> GetFriendsPresence(
      const std::vector<uint64_t>& xuids);

  static X_STORAGE_BUILD_SERVER_PATH_RESULT XStorageBuildServerPath(
      std::string server_path);

  static bool XStorageDelete(std::string server_path);

  static std::span<uint8_t> XStorageDownload(std::string server_path);

  static X_STORAGE_UPLOAD_RESULT XStorageUpload(std::string server_path,
                                                std::span<uint8_t> buffer);

  static std::pair<std::unique_ptr<XStorageFilesInfoObjectJSON>, bool>
  XStorageEnumerate(std::string server_path, uint32_t max_items);

  static std::unique_ptr<FindUsersObjectJSON> GetFindUsers(
      const std::vector<FIND_USER_INFO>& find_users_info);

  static void SetPresence();

  static std::unique_ptr<HTTPResponseObjectJSON> PraseResponse(
      response_data response);

  static std::vector<FriendPresenceObjectJSON> GetAllFriendsPresence(
      const uint32_t user_index);

  static std::map<uint64_t, FriendPresenceObjectJSON> GetOfflineFriendsPresence(
      const uint32_t user_index);

  static std::map<uint64_t, FriendPresenceObjectJSON> GetOnlineFriendsPresence(
      const uint32_t user_index);

  static const uint8_t* GenerateMacAddress();

  static const uint8_t* GetMACaddress();

  static std::string GetNetworkFriendlyName(IP_ADAPTER_ADDRESSES adapter);

  static void DiscoverNetworkInterfaces();

  static bool UpdateNetworkInterface(sockaddr_in local_ip,
                                     IP_ADAPTER_ADDRESSES adapter);

  static void SelectNetworkInterface();

  static const sockaddr_in LocalIP() { return local_ip_; };
  static const sockaddr_in OnlineIP() { return online_ip_; };

  static const std::string LocalIP_str() { return ip_to_string(local_ip_); };
  static const std::string OnlineIP_str() { return ip_to_string(online_ip_); };

  inline static UPnP* upnp_handler = nullptr;

  inline static MacAddress* mac_address_ = nullptr;

  inline static bool xlsp_servers_cached = false;
  inline static std::vector<X_TITLE_SERVER> xlsp_servers{};

  inline static std::string interface_name;

  inline static std::vector<uint8_t> adapter_addresses_buf{};

  inline static std::vector<IP_ADAPTER_ADDRESSES> adapter_addresses{};

  inline static bool adapter_has_wan_routing = false;

  inline static std::map<uint32_t, uint64_t> sessionIdCache{};
  inline static std::map<uint32_t, uint64_t> macAddressCache{};
  inline static std::map<uint64_t, std::vector<uint8_t>> qos_payload_cache{};

  inline static xe::be<uint64_t> systemlink_id = 0;

  inline static bool xuid_mismatch = false;

  inline static uint32_t dummy_friends_count = 0;

  inline static int8_t version_status;

 private:
  inline static const std::string default_local_server_ = "192.168.0.1:36000/";

  inline static const std::string default_public_server_ =
      "https://xenia-netplay-2a0298c0e3f4.herokuapp.com/";

  inline static InitState initialized_ = InitState::Pending;

  static std::unique_ptr<HTTPResponseObjectJSON> Get(
      std::string endpoint, const uint32_t timeout = 0);

  static std::unique_ptr<HTTPResponseObjectJSON> Post(std::string endpoint,
                                                      const uint8_t* data,
                                                      size_t data_size = 0);

  static std::unique_ptr<HTTPResponseObjectJSON> Delete(std::string endpoint);

  // https://curl.se/libcurl/c/CURLOPT_WRITEFUNCTION.html
  static size_t callback(void* data, size_t size, size_t nmemb, void* clientp) {
    size_t realsize = size * nmemb;
    struct response_data* mem = (struct response_data*)clientp;

    char* ptr = (char*)realloc(mem->response, mem->size + realsize + 1);
    if (ptr == NULL) return 0; /* out of memory! */

    mem->response = ptr;
    memcpy(&(mem->response[mem->size]), data, realsize);
    mem->size += realsize;
    mem->response[mem->size] = 0;

    return realsize;
  };

  inline static sockaddr_in online_ip_{};

  inline static sockaddr_in local_ip_{};
};
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XLIVEAPI_H_