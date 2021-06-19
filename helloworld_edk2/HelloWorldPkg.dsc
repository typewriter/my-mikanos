#@range_begin(defines)
[Defines]
  PLATFORM_NAME           = HelloWorldPkg
  PLATFORM_GUID           = 807d955b-c310-406c-bf6d-8bf76c368aea
  PLATFORM_VERSION        = 0.01
  DSC_SPECIFICATION       = 0x00010005
  OUTPUT_DIRECTORY        = Build/HelloWorld$(ARCH)
  SUPPORTED_ARCHITECTURES = IA32
  BUILD_TARGETS           = DEBUG|RELEASE|NOOPT
#@range_end(defines)

#@range_begin(library_classes)
[LibraryClasses]
  UefiApplicationEntryPoint|MdePkg/Library/UefiApplicationEntryPoint/UefiApplicationEntryPoint.inf
  UefiLib|MdePkg/Library/UefiLib/UefiLib.inf
#@range_end(library_classes)

  BaseLib|MdePkg/Library/BaseLib/BaseLib.inf
  BaseMemoryLib|MdePkg/Library/BaseMemoryLib/BaseMemoryLib.inf
  DebugLib|MdePkg/Library/BaseDebugLibNull/BaseDebugLibNull.inf
  DevicePathLib|MdePkg/Library/UefiDevicePathLib/UefiDevicePathLib.inf
  MemoryAllocationLib|MdePkg/Library/UefiMemoryAllocationLib/UefiMemoryAllocationLib.inf
  PcdLib|MdePkg/Library/BasePcdLibNull/BasePcdLibNull.inf
  PrintLib|MdePkg/Library/BasePrintLib/BasePrintLib.inf
  UefiBootServicesTableLib|MdePkg/Library/UefiBootServicesTableLib/UefiBootServicesTableLib.inf
  UefiRuntimeServicesTableLib|MdePkg/Library/UefiRuntimeServicesTableLib/UefiRuntimeServicesTableLib.inf

#@range_begin(components)
[Components]
  helloworld_edk2/Loader.inf
#@range_end(components)

