#include "acpi.hpp"
#include "console.hpp"
#include "logger.hpp"
#include "asmfunc.h"
#include <cstdlib>
#include <cstring>

namespace {
  template<typename T>
  uint8_t SumBytes(const T* data, size_t bytes) {
    return SumBytes(reinterpret_cast<const uint8_t*>(data), bytes);
  }

  template <>
  uint8_t SumBytes<uint8_t>(const uint8_t* data, size_t bytes) {
    uint8_t sum = 0;
    for (size_t i = 0; i < bytes; ++i) {
      sum += data[i];
    }
    return sum;
  }

}

namespace acpi
{
  bool RSDP::IsValid() const
  {
    if (strncmp(this->signature, "RSD PTR ", 8) != 0)
    {
      Log(kDebug, "Invalid signature: %.8s\n", this->signature);
      return false;
    }
    if (this->revision != 2)
    {
      Log(kDebug, "ACPI revision must be 2: %d\n", this->revision);
      return false;
    }
    if (auto sum = SumBytes(this, 20); sum != 0)
    {
      Log(kDebug, "Sum of 20 bytes must be 0: %d\n", sum);
      return false;
    }
    if (auto sum = SumBytes(this, 36); sum != 0)
    {
      Log(kDebug, "Sum of 36 bytes must be 0: %d \n", sum);
      return false;
    }
    return true;
  }

  bool DescriptionHeader::IsValid(const char* expected_signature) const {
    if (strncmp(this->signature, expected_signature, 4) != 0) {
      Log(kDebug, "Invalid signature: %.4s\n", this->signature);
      return false;
    }
    if (auto sum = SumBytes(this, this->length); sum != 0) {
      Log(kDebug, "Sum of %u bytes must be 0: %d\n", this->length, sum);
      return false;
    }
    return true;
  }

  const DescriptionHeader& XSDT::operator[](size_t i) const {
    auto entries = reinterpret_cast<const uint64_t*>(&this->header + 1);
    return *reinterpret_cast<const DescriptionHeader*>(entries[i]);
  }

  size_t XSDT::Count() const {
    return (this->header.length - sizeof(DescriptionHeader)) / sizeof(uint64_t);
  }

  const FADT* fadt;

  void Initialize(const acpi::RSDP &rsdp)
  {
    if (!rsdp.IsValid())
    {
      Log(kError, "RSDP is not valid\n");
      exit(1);
    }

    const XSDT& xsdt = *reinterpret_cast<const XSDT*>(rsdp.xsdt_address);
    if (!xsdt.header.IsValid("XSDT")) {
      Log(kError, "XSDT is not valid\n");
      exit(1);
    }

    fadt = nullptr;
    for (int i = 0; i < xsdt.Count(); ++i) {
      const auto& entry = xsdt[i];
      if (entry.IsValid("FACP")) { // FADT のシグネチャは FACP
        fadt = reinterpret_cast<const FADT*>(&entry);
        break;
      }
    }

    if (fadt == nullptr) {
      Log(kError, "FADT is not found\n");
      exit(1);
    }
  }

  void WaitMilliseconds(unsigned long msec) {
    // TMR_VAL_EXT (https://uefi.org/specs/ACPI/6.4/05_ACPI_Software_Programming_Model/ACPI_Software_Programming_Model.html#fixed-acpi-description-table-fixed-feature-flags)
    const bool pm_timer_32 = (fadt->flags >> 8) & 1;
    const uint32_t start = IoIn32(fadt->pm_tmr_blk);
    // (オーバーフローしても勝手に切り捨てられる)
    uint32_t end = start + kPMTimerFreq * msec / 1000;
    if (!pm_timer_32) {
      // タイマーは 24bit なので上位 8 ビットは捨てる
      end &= 0x00ffffffu;
    }

    printk("PM_TMR_BLK address: %lu\n", fadt->pm_tmr_blk);
    printk("Start Wait while loop... from %lu to %lu\n", start, end);
    
    for (int i = 0; i < 20; ++i) {
      printk("PM_TMR_BLK value: %lu\n", IoIn32(fadt->pm_tmr_blk));
    }

    if (end < start) { // overflow
      while (IoIn32(fadt->pm_tmr_blk) >= start);
    }
    while (IoIn32(fadt->pm_tmr_blk) < end);
  }
}