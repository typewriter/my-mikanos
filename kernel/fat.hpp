#pragma once

#include <cstdint>
#include <cstddef>

/**
 * @see https://download.microsoft.com/download/1/6/1/161ba512-40e2-4cc9-843a-923143f3456c/fatgen103.doc
 *
 */
namespace fat
{
  /** BIOS Parameter Block */
  struct BPB
  {
    // 0x00
    uint8_t bs_jmp_boot[3];
    char bs_oem_name[8];
    uint16_t bpb_bytes_per_sector;
    uint8_t bpb_sectors_per_cluster;
    uint16_t bpb_reserved_sectors_count;
    // 0x10
    uint8_t bpb_number_of_fats;
    uint16_t bpb_root_entry_count;
    uint16_t bpb_total_sectors_16bit;
    uint8_t bpb_media_type;
    uint16_t bpb_fat_size_16bit;
    uint16_t bpb_sectors_per_track;
    uint16_t bpb_number_of_heads;
    uint32_t bpb_hidden_sectors_count;
    // 0x20
    uint32_t bpb_total_sectors_32bit;
    // FAT32 structure (offset 36)
    uint32_t bpb_fat_size_32bit;
    uint16_t bpb_ext_flags;
    uint16_t bpb_file_system_version;
    uint32_t bpb_root_clusters;
    // 0x30
    uint16_t bpb_fsinfo;
    uint16_t bpb_bk_boot_sec;
    uint8_t bpb_reserved[12];
    // 0x40
    uint8_t bs_drive_number;
    uint8_t bs_reserved1;
    uint8_t bs_boot_signature;
    uint32_t bs_volume_serial_number;
    char bs_volume_label[11];
    char bs_file_system_type[8];
  } __attribute__((packed));

  extern BPB *boot_volume_image;
  extern unsigned long bytes_per_cluster;

  enum class Attribute : uint8_t
  {
    kReadOnly = 0x01,
    kHidden = 0x02,
    kSystem = 0x04,
    kVolumeId = 0x08,
    kDirectory = 0x10,
    kArchive = 0x20,
    kLongName = 0x0f
  };

  struct DirectoryEntry
  {
    char dir_name[11];
    Attribute dir_attr;
    uint8_t dir_nt_res;
    uint8_t dir_creation_time_tenth;
    uint16_t dir_creation_time;
    // 0x10
    uint16_t dir_creation_date;
    uint16_t dir_last_access_date;
    uint16_t dir_first_cluster_high_word;
    uint16_t dir_last_write_time;
    uint16_t dir_last_write_date;
    uint16_t dir_first_cluster_low_word;
    uint32_t dir_file_size;

    uint32_t FirstCluster() const
    {
      return dir_first_cluster_low_word |
             (static_cast<uint32_t>(dir_first_cluster_high_word) << 16);
    }
  } __attribute__((packed));

  void Initialize(void *volume_image);
  uintptr_t GetClusterAddr(unsigned long cluster);
  void ReadName(const DirectoryEntry &entry, char *base, char *ext);
  unsigned long NextCluster(unsigned long cluster);
  DirectoryEntry *FindFile(const char *name, unsigned long directory_cluster = 0);
  size_t LoadFile(void *buf, size_t len, const DirectoryEntry &entry);

  static const unsigned long kEndOfClusterchain = 0x0ffffffflu;

  template <class T>
  T *GetSectorByCluster(unsigned long cluster)
  {
    return reinterpret_cast<T *>(GetClusterAddr(cluster));
  }
}