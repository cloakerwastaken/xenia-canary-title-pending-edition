/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/logging.h"
#include "xenia/base/string_util.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xam/xam_module.h"
#include "xenia/kernel/xam/xam_private.h"
#include "xenia/kernel/xenumerator.h"
#include "xenia/xbox.h"

#include "xenia/kernel/XLiveAPI.h"

namespace xe {
namespace kernel {
namespace xam {

uint32_t xeXamEnumerate(uint32_t handle, uint32_t flags, lpvoid_t buffer_ptr,
                        uint32_t buffer_size, uint32_t* items_returned,
                        uint32_t overlapped_ptr) {
  auto e = kernel_state()->object_table()->LookupObject<XEnumerator>(handle);
  if (!e) {
    return X_ERROR_INVALID_HANDLE;
  }

  auto run = [e, buffer_ptr, overlapped_ptr](uint32_t& extended_error,
                                             uint32_t& length) -> X_RESULT {
    X_RESULT result;
    uint32_t item_count = 0;
    if (!buffer_ptr) {
      result = X_ERROR_INVALID_PARAMETER;
    } else {
      result = e->WriteItems(buffer_ptr.guest_address(),
                             buffer_ptr.as<uint8_t*>(), &item_count);
    }
    extended_error = X_HRESULT_FROM_WIN32(result);
    length = item_count;
    if (result && overlapped_ptr) {
      result = X_ERROR_FUNCTION_FAILED;
    }
    return result;
  };

  if (items_returned) {
    assert_true(!overlapped_ptr);
    uint32_t extended_error;
    uint32_t item_count;
    X_RESULT result = run(extended_error, item_count);
    *items_returned = result == X_ERROR_SUCCESS ? item_count : 0;
    return result;
  } else if (overlapped_ptr) {
    assert_true(!items_returned);
    kernel_state()->CompleteOverlappedDeferredEx(run, overlapped_ptr);
    return X_ERROR_IO_PENDING;
  } else {
    assert_always();
    return X_ERROR_INVALID_PARAMETER;
  }
}

dword_result_t XamEnumerate_entry(dword_t handle, dword_t flags,
                                  lpvoid_t buffer, dword_t buffer_length,
                                  lpdword_t items_returned,
                                  pointer_t<XAM_OVERLAPPED> overlapped) {
  uint32_t dummy;
  auto result = xeXamEnumerate(handle, flags, buffer, buffer_length,
                               !overlapped ? &dummy : nullptr, overlapped);
  if (!overlapped && items_returned) {
    *items_returned = dummy;
  }
  return result;
}
DECLARE_XAM_EXPORT1(XamEnumerate, kNone, kImplemented);

static uint32_t XTitleServerCreateEnumerator(
    uint32_t user_index, uint32_t app_id, uint32_t open_message,
    uint32_t close_message, uint32_t extra_size, uint32_t item_count,
    uint32_t flags, uint32_t* out_handle) {
  auto e = make_object<XStaticEnumerator<X_TITLE_SERVER>>(kernel_state(),
                                                          item_count);

  auto result = e->Initialize(user_index, app_id, open_message, close_message,
                              flags, extra_size, nullptr);

  if (XFAILED(result)) {
    return result;
  }

  const auto servers = XLiveAPI::GetServers();

  for (const auto& server : servers) {
    X_TITLE_SERVER* item = e->AppendItem();

    *item = server;
  }

  XELOGI("{}: added {} items to enumerator", __func__, e->item_count());

  *out_handle = e->handle();
  return X_ERROR_SUCCESS;
}

static uint32_t XMarketplaceCreateOfferEnumerator(
    uint32_t user_index, uint32_t app_id, uint32_t open_message,
    uint32_t close_message, uint32_t extra_size, uint32_t item_count,
    uint32_t flags, uint32_t* out_handle) {
  auto e = make_object<XStaticEnumerator<X_MARKETPLACE_CONTENTOFFER_INFO>>(
      kernel_state(), item_count);

  auto result = e->Initialize(user_index, app_id, open_message, close_message,
                              flags, extra_size, nullptr);

  if (XFAILED(result)) {
    return result;
  }

  std::vector<X_MARKETPLACE_CONTENTOFFER_INFO> content_offers = {};

  for (const auto& content : content_offers) {
    X_MARKETPLACE_CONTENTOFFER_INFO* item = e->AppendItem();

    *item = content;
  }

  XELOGI("{}: added {} items to enumerator", __func__, e->item_count());

  *out_handle = e->handle();
  return X_ERROR_SUCCESS;
}

static uint32_t XMarketplaceCreateAssetEnumerator(
    uint32_t user_index, uint32_t app_id, uint32_t open_message,
    uint32_t close_message, uint32_t extra_size, uint32_t item_count,
    uint32_t flags, uint32_t* out_handle) {
  auto e = make_object<XStaticEnumerator<X_MARKETPLACE_ASSET_ENUMERATE_REPLY>>(
      kernel_state(), item_count);

  auto result = e->Initialize(user_index, app_id, open_message, close_message,
                              flags, extra_size, nullptr);

  if (XFAILED(result)) {
    return result;
  }

  std::vector<X_MARKETPLACE_ASSET_ENUMERATE_REPLY> marketplace_assets = {};

  for (const auto& asset : marketplace_assets) {
    X_MARKETPLACE_ASSET_ENUMERATE_REPLY* item = e->AppendItem();

    *item = asset;
  }

  XELOGI("{}: added {} items to enumerator", __func__, e->item_count());

  *out_handle = e->handle();
  return X_ERROR_SUCCESS;
}

// XMarketplaceCreateOfferEnumeratorByOffering ->
// XMarketplaceCreateOfferEnumeratorEx

constexpr uint32_t XTitleServerMessage = 0x58039;
constexpr uint32_t XMarketplaceCreateOfferEnumeratorMessage = 0x58040;
constexpr uint32_t XMarketplaceCreateOfferEnumeratorExMessage = 0x58040;
constexpr uint32_t XMarketplaceCreateAssetEnumeratorMessage = 0x58042;

dword_result_t XamCreateEnumeratorHandle_entry(
    dword_t user_index, dword_t app_id, dword_t open_message,
    dword_t close_message, dword_t extra_size, dword_t item_count,
    dword_t flags, lpdword_t out_handle) {
  uint32_t out_handle_ptr = 0;

  switch (open_message) {
    case XTitleServerMessage: {
      auto result = XTitleServerCreateEnumerator(
          user_index, app_id, open_message, close_message, extra_size,
          item_count, flags, &out_handle_ptr);

      if (XFAILED(result)) {
        return result;
      }

      *out_handle = out_handle_ptr;
    } break;
    case XMarketplaceCreateOfferEnumeratorMessage: {
      auto result = XMarketplaceCreateOfferEnumerator(
          user_index, app_id, open_message, close_message, extra_size,
          item_count, flags, &out_handle_ptr);

      if (XFAILED(result)) {
        return result;
      }

      *out_handle = out_handle_ptr;
    } break;
    case XMarketplaceCreateAssetEnumeratorMessage: {
      auto result = XMarketplaceCreateAssetEnumerator(
          user_index, app_id, open_message, close_message, extra_size,
          item_count, flags, &out_handle_ptr);

      if (XFAILED(result)) {
        return result;
      }

      *out_handle = out_handle_ptr;
    } break;
    default: {
      std::string enumerator_log = fmt::format(
          "Unimplemented XamCreateEnumeratorHandle app={:04X}, "
          "open_message={:04X}, close_message={:04X}, flags={:04X}",
          app_id.value(), open_message.value(), close_message.value(),
          flags.value());

      XELOGI(enumerator_log);

      auto e = object_ref<XStaticUntypedEnumerator>(
          new XStaticUntypedEnumerator(kernel_state(), item_count, extra_size));

      auto result =
          e->Initialize(user_index, app_id, open_message, close_message, flags);

      if (XFAILED(result)) {
        return result;
      }

      *out_handle = e->handle();
    } break;
  }

  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamCreateEnumeratorHandle, kNone, kImplemented);

dword_result_t XamGetPrivateEnumStructureFromHandle_entry(
    dword_t handle, lpdword_t out_object_ptr) {
  auto e = kernel_state()->object_table()->LookupObject<XEnumerator>(handle);
  if (!e) {
    return X_STATUS_INVALID_HANDLE;
  }

  // Caller takes the reference.
  // It's released in ObDereferenceObject.
  e->RetainHandle();

  if (out_object_ptr.guest_address()) {
    *out_object_ptr = e->guest_object();
  }

  return X_STATUS_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamGetPrivateEnumStructureFromHandle, kNone, kStub);

dword_result_t XamProfileCreateEnumerator_entry(dword_t device_id,
                                                lpdword_t handle_ptr) {
  if (!handle_ptr) {
    return X_ERROR_INVALID_PARAMETER;
  }

  auto e = new XStaticEnumerator<X_PROFILEENUMRESULT>(kernel_state(), 1);

  auto result = e->Initialize(XUserIndexAny, 0xFE, 0x23001, 0x23003, 0);

  if (XFAILED(result)) {
    return result;
  }

  const auto& accounts =
      kernel_state()->xam_state()->profile_manager()->GetAccounts();

  for (const auto& [xuid, account] : *accounts) {
    X_PROFILEENUMRESULT* profile = e->AppendItem();

    profile->xuid_offline = xuid;
    profile->device_id = 1;
    memcpy(&profile->account, &account, sizeof(X_XAMACCOUNTINFO));

    xe::string_util::copy_and_swap_truncating(
        profile->account.gamertag, account.gamertag, sizeof(account.gamertag));
  }

  *handle_ptr = e->handle();
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamProfileCreateEnumerator, kNone, kImplemented);

dword_result_t XamProfileEnumerate_entry(dword_t handle, dword_t flags,
                                         lpvoid_t buffer,
                                         pointer_t<XAM_OVERLAPPED> overlapped) {
  uint32_t dummy = 0;
  auto result = xeXamEnumerate(handle, flags, buffer, 0,
                               !overlapped ? &dummy : nullptr, overlapped);

  return result;
}
DECLARE_XAM_EXPORT1(XamProfileEnumerate, kNone, kImplemented);

dword_result_t EnumerateMediaObjects_entry() { return X_E_NOT_IMPLEMENTED; }
DECLARE_XAM_EXPORT1(EnumerateMediaObjects, kNone, kStub);

dword_result_t EnumerateMediaObjects__entry() { return X_E_NOT_IMPLEMENTED; }
DECLARE_XAM_EXPORT1(EnumerateMediaObjects_, kNone, kStub);

dword_result_t EnumerateMediaObjects_0_entry() { return X_E_NOT_IMPLEMENTED; }
DECLARE_XAM_EXPORT1(EnumerateMediaObjects_0, kNone, kStub);

dword_result_t EnumerateMediaObjects_1_entry() { return X_E_NOT_IMPLEMENTED; }
DECLARE_XAM_EXPORT1(EnumerateMediaObjects_1, kNone, kStub);

}  // namespace xam
}  // namespace kernel
}  // namespace xe

DECLARE_XAM_EMPTY_REGISTER_EXPORTS(Enum);