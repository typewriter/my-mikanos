#include "fat.hpp"
#include <cstring>

namespace fat {
  BPB *boot_volume_image;

  void Initialize(void *volume_image)
  {
    boot_volume_image = reinterpret_cast<fat::BPB *>(volume_image);
  }

  uintptr_t GetClusterAddr(unsigned long cluster)
  {
    unsigned long sector_num =
      boot_volume_image->bpb_reserved_sectors_count +
      boot_volume_image->bpb_number_of_fats * boot_volume_image->bpb_fat_size_32bit +
      (cluster - 2) * boot_volume_image->bpb_sectors_per_cluster;
    uintptr_t offset = sector_num * boot_volume_image->bpb_bytes_per_sector;
    return reinterpret_cast<uintptr_t>(boot_volume_image) + offset;
  }

  void ReadName(const DirectoryEntry& entry, char* base, char* ext)
  {
    memcpy(base, &entry.dir_name[0], 8);
    base[8] = 0;
    for (int i = 7; i >= 0 && base[i] == 0x20; --i)
    {
      base[i] = 0;
    }
    memcpy(ext, &entry.dir_name[8], 3);
    ext[3] = 0;
    for (int i = 2; i >= 0 && ext[i] == 0x20; --i)
    {
      ext[i] = 0;
    }
  }
}