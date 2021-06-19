#include <Uefi.h>
#include <Library/UefiLib.h>

EFI_STATUS EFIAPI UefiMain(
  EFI_HANDLE image_handle, EFI_SYSTEM_TABLE *system_table
) {
  Print(L"\n\n\n          Konnichiwa!\n");
  while (1);
  return EFI_SUCCESS;
}


