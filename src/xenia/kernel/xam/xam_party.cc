/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xam/xam_private.h"
#include "xenia/xbox.h"

namespace xe {
namespace kernel {
namespace xam {

dword_result_t XamPartyGetUserList_entry(
    dword_t player_count, pointer_t<X_PARTY_USER_LIST> party_list_ptr) {
  if (party_list_ptr) {
    party_list_ptr.Zero();
  }

  // 5345085D wants specifically this code to skip loading party data.
  // This code is not documented in NT_STATUS code list
  return X_PARTY_E_NOT_IN_PARTY;
}
DECLARE_XAM_EXPORT1(XamPartyGetUserList, kNone, kStub);

dword_result_t XamPartySendGameInvites_entry(dword_t r3, dword_t r4,
                                             dword_t r5) {
  return X_ERROR_FUNCTION_FAILED;
}
DECLARE_XAM_EXPORT1(XamPartySendGameInvites, kNone, kStub);

dword_result_t XamPartySetCustomData_entry(dword_t r3, dword_t r4, dword_t r5) {
  return X_ERROR_FUNCTION_FAILED;
}
DECLARE_XAM_EXPORT1(XamPartySetCustomData, kNone, kStub);

dword_result_t XamPartyGetBandwidth_entry(dword_t r3, dword_t r4) {
  return X_ERROR_FUNCTION_FAILED;
}
DECLARE_XAM_EXPORT1(XamPartyGetBandwidth, kNone, kStub);

dword_result_t XamPartyGetUserListInternal_entry(
    pointer_t<X_PARTY_USER_LIST_INTERNAL> party_list_ptr) {
  if (party_list_ptr) {
    party_list_ptr.Zero();
  }

  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamPartyGetUserListInternal, kNone, kStub);

}  // namespace xam
}  // namespace kernel
}  // namespace xe

DECLARE_XAM_EMPTY_REGISTER_EXPORTS(Party);
