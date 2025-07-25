/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/vfs/devices/host_path_entry.h"

#include "xenia/base/filesystem.h"
#include "xenia/base/logging.h"
#include "xenia/base/mapped_memory.h"
#include "xenia/base/math.h"
#include "xenia/base/string.h"
#include "xenia/vfs/device.h"
#include "xenia/vfs/devices/host_path_device.h"
#include "xenia/vfs/devices/host_path_file.h"

namespace xe {
namespace vfs {

HostPathEntry::HostPathEntry(Device* device, Entry* parent,
                             const std::string_view path,
                             const std::filesystem::path& host_path)
    : Entry(device, parent, path), host_path_(host_path) {}

HostPathEntry::~HostPathEntry() = default;

HostPathEntry* HostPathEntry::Create(Device* device, Entry* parent,
                                     const std::filesystem::path& full_path,
                                     xe::filesystem::FileInfo file_info) {
  auto path = xe::utf8::join_guest_paths(parent->path(),
                                         xe::path_to_utf8(file_info.name));
  auto entry = new HostPathEntry(device, parent, path, full_path);

  entry->create_timestamp_ = file_info.create_timestamp;
  entry->access_timestamp_ = file_info.access_timestamp;
  entry->write_timestamp_ = file_info.write_timestamp;
  if (file_info.type == xe::filesystem::FileInfo::Type::kDirectory) {
    entry->attributes_ = kFileAttributeDirectory;
  } else {
    entry->attributes_ = kFileAttributeNormal;
    if (device->is_read_only()) {
      entry->attributes_ |= kFileAttributeReadOnly;
    }
    entry->size_ = file_info.total_size;
    entry->allocation_size_ =
        xe::round_up(file_info.total_size, device->bytes_per_sector());
  }
  return entry;
}

X_STATUS HostPathEntry::Open(uint32_t desired_access, File** out_file) {
  if (is_read_only() && (desired_access & (FileAccess::kFileWriteData |
                                           FileAccess::kFileAppendData))) {
    XELOGE("Attempting to open file for write access on read-only device");
    return X_STATUS_ACCESS_DENIED;
  }
  auto file_handle =
      xe::filesystem::FileHandle::OpenExisting(host_path_, desired_access);
  if (!file_handle) {
    // TODO(benvanik): pick correct response.
    return X_STATUS_NO_SUCH_FILE;
  }
  *out_file = new HostPathFile(desired_access, this, std::move(file_handle));
  return X_STATUS_SUCCESS;
}

std::unique_ptr<MappedMemory> HostPathEntry::OpenMapped(MappedMemory::Mode mode,
                                                        size_t offset,
                                                        size_t length) {
  return MappedMemory::Open(host_path_, mode, offset, length);
}

std::unique_ptr<Entry> HostPathEntry::CreateEntryInternal(
    const std::string_view name, uint32_t attributes) {
  auto full_path = host_path_ / xe::to_path(name);
  if (attributes & kFileAttributeDirectory) {
    if (!std::filesystem::create_directories(full_path)) {
      return nullptr;
    }
  } else {
    auto file = xe::filesystem::OpenFile(full_path, "wb");
    if (!file) {
      return nullptr;
    }
    fclose(file);
  }
  auto file_info = xe::filesystem::GetInfo(full_path);
  if (!file_info) {
    return nullptr;
  }
  return std::unique_ptr<Entry>(
      HostPathEntry::Create(device_, this, full_path, file_info.value()));
}

bool HostPathEntry::DeleteEntryInternal(Entry* entry) {
  auto full_path = host_path_ / xe::to_path(entry->name());
  std::error_code ec;  // avoid exception on remove/remove_all failure
  if (entry->attributes() & kFileAttributeDirectory) {
    // Delete entire directory and contents.
    auto removed = std::filesystem::remove_all(full_path, ec);
    return removed >= 1 && removed != static_cast<std::uintmax_t>(-1);
  } else {
    // Skip directories, they we're handled above.
    if (std::filesystem::is_directory(full_path)) {
      return false;
    }

    if (std::filesystem::exists(full_path)) {
      const auto result = std::filesystem::remove(full_path, ec);
      if (ec) {
        XELOGE("{}: Cannot remove file entry. File: {} Error: {}", __func__,
               full_path, ec.message());
        return false;
      }
    }
    return true;
  }
}

void HostPathEntry::RenameEntryInternal(const std::filesystem::path file_path) {
  const std::string new_host_path_ = xe::utf8::join_paths(
      xe::path_to_utf8(((HostPathDevice*)device_)->host_path()),
      xe::path_to_utf8(file_path));

  std::filesystem::rename(host_path_, new_host_path_);
  host_path_ = new_host_path_;
}

void HostPathEntry::update() {
  auto file_info = xe::filesystem::GetInfo(host_path_);
  if (!file_info) {
    return;
  }
  if (file_info->type == xe::filesystem::FileInfo::Type::kFile) {
    size_ = file_info->total_size;
    allocation_size_ =
        xe::round_up(file_info->total_size, device()->bytes_per_sector());
  }
}

bool HostPathEntry::SetAttributes(uint64_t attributes) {
  if (device_->is_read_only()) {
    return false;
  }
  return xe::filesystem::SetAttributes(host_path_, attributes);
}

bool HostPathEntry::SetCreateTimestamp(uint64_t timestamp) {
  if (device_->is_read_only()) {
    XELOGW(
        "{} - Tried to change read-only creation timestamp for file: {} to: {}",
        __FUNCTION__, name_, timestamp);
    return false;
  }
  XELOGI("{} - Tried to change creation timestamp for file: {} to: {}",
         __FUNCTION__, name_, timestamp);
  return true;
}

bool HostPathEntry::SetAccessTimestamp(uint64_t timestamp) {
  if (device_->is_read_only()) {
    XELOGW(
        "{} - Tried to change read-only access timestamp for file: {} to: {}",
        __FUNCTION__, name_, timestamp);
    return false;
  }
  XELOGI("{} - Tried to change access timestamp for file: {} to: {}",
         __FUNCTION__, name_, timestamp);
  return true;
}

bool HostPathEntry::SetWriteTimestamp(uint64_t timestamp) {
  if (device_->is_read_only()) {
    XELOGW("{} - Tried to change read-only write timestamp for file: {} to: {}",
           __FUNCTION__, name_, timestamp);
    return false;
  }
  XELOGI("{} - Tried to change write timestamp for file: {} to: {}",
         __FUNCTION__, name_, timestamp);
  return true;
}

}  // namespace vfs
}  // namespace xe
