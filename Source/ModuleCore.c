#include "ModuleCore.h"



extern PDRIVER_OBJECT  g_DriverObject;
extern DYNAMIC_DATA    g_DynamicData;


POBJECT_TYPE g_DirectoryObjectType = NULL;


PLDR_DATA_TABLE_ENTRY
GetKernelLdrDataTableEntry(IN PDRIVER_OBJECT DriverObject)
{
	PLDR_DATA_TABLE_ENTRY TravelEntry = NULL, FirstEntry = NULL;

	if (DriverObject)
	{	
		WCHAR wzNtoskrnl[] = L"ntoskrnl.exe";
		int   iLength = wcslen(wzNtoskrnl) * sizeof(WCHAR);

		FirstEntry = (PLDR_DATA_TABLE_ENTRY)DriverObject->DriverSection;

		for (TravelEntry = (PLDR_DATA_TABLE_ENTRY)FirstEntry->InLoadOrderLinks.Flink;
			TravelEntry != FirstEntry;
			TravelEntry = (PLDR_DATA_TABLE_ENTRY)TravelEntry->InLoadOrderLinks.Flink)
		{
			if (TravelEntry->BaseDllName.Buffer &&
				TravelEntry->BaseDllName.Length == iLength &&
				MmIsAddressValid((PVOID)TravelEntry->BaseDllName.Buffer) &&
					!_wcsnicmp(wzNtoskrnl, (WCHAR*)TravelEntry->BaseDllName.Buffer, iLength / sizeof(WCHAR)))
			{
				return TravelEntry;
			}
		}

		// 没找到
		return FirstEntry;
	}
	return NULL;
}


// 通过遍历Ldr枚举内核模块
VOID
EnumKernelModuleByLdrDataTableEntry(IN PLDR_DATA_TABLE_ENTRY KernelLdrEntry, OUT PKERNEL_MODULE_INFORMATION kmi, IN UINT32 NumberOfDrivers)
{
	PLDR_DATA_TABLE_ENTRY TravelEntry = KernelLdrEntry;

	if (kmi && TravelEntry)
	{
		KIRQL OldIrql;

		OldIrql = KeRaiseIrqlToDpcLevel();

		__try
		{
			UINT32 MaxSize = PAGE_SIZE;
			INT32  i = 0;

			do 
			{
				if ((UINT_PTR)TravelEntry->DllBase > g_DynamicData.KernelStartAddress && TravelEntry->SizeOfImage > 0)
				{
					UINT_PTR CurrentCount = kmi->NumberOfDrivers;
					if (NumberOfDrivers > CurrentCount)
					{

						kmi->Drivers[CurrentCount].LoadOrder = ++i;
						kmi->Drivers[CurrentCount].BaseAddress = (UINT_PTR)TravelEntry->DllBase;
						kmi->Drivers[CurrentCount].Size = TravelEntry->SizeOfImage;


						if (IsUnicodeStringValid(&(TravelEntry->FullDllName)))
						{
							memcpy(kmi->Drivers[CurrentCount].wzDriverPath, (WCHAR*)TravelEntry->FullDllName.Buffer, TravelEntry->FullDllName.Length);
						}
						else if (IsUnicodeStringValid(&(TravelEntry->BaseDllName)))
						{
							memcpy(kmi->Drivers[CurrentCount].wzDriverPath, (WCHAR*)TravelEntry->BaseDllName.Buffer, TravelEntry->BaseDllName.Length);
						}
					}
					kmi->NumberOfDrivers++;
				}
				TravelEntry = (PLDR_DATA_TABLE_ENTRY)TravelEntry->InLoadOrderLinks.Flink;

			} while (TravelEntry && TravelEntry != KernelLdrEntry && MaxSize--);
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{ }

		KeLowerIrql(OldIrql);
	}
}


// 查看传入的对象是否已经存在结构体中，如果在 则继续完善信息，如果不在，则返回false，留给母程序处理
BOOLEAN 
IsDriverInList(IN PKERNEL_MODULE_INFORMATION kmi, IN PDRIVER_OBJECT DriverObject, IN UINT32 NumberOfDrivers)
{
	BOOLEAN bOk = TRUE, bFind = FALSE;

	if (!kmi || !DriverObject || !MmIsAddressValid(DriverObject))
	{
		return bOk;
	}

	__try
	{
		if (MmIsAddressValid(DriverObject))
		{
			PLDR_DATA_TABLE_ENTRY LdrDataTableEntry = (PLDR_DATA_TABLE_ENTRY)DriverObject->DriverSection;

			if (LdrDataTableEntry &&
				MmIsAddressValid(LdrDataTableEntry) &&
				MmIsAddressValid((PVOID)LdrDataTableEntry->DllBase) &&
				(UINT_PTR)LdrDataTableEntry->DllBase > g_DynamicData.KernelStartAddress)
			{
				UINT32 i = 0;
				UINT32 Count = NumberOfDrivers > kmi->NumberOfDrivers ? kmi->NumberOfDrivers : NumberOfDrivers;

				for (i = 0; i < Count; i++)
				{
					if (kmi->Drivers[i].BaseAddress == (UINT_PTR)LdrDataTableEntry->DllBase)
					{
						if (kmi->Drivers[i].DriverObject == 0)
						{
							// 获得驱动对象
							kmi->Drivers[i].DriverObject = (UINT_PTR)DriverObject;

							// 获得驱动入口
							kmi->Drivers[i].DirverStartAddress = (UINT_PTR)LdrDataTableEntry->EntryPoint;

							// 获得服务名
							wcsncpy(kmi->Drivers[i].wzKeyName, DriverObject->DriverExtension->ServiceKeyName.Buffer, DriverObject->DriverExtension->ServiceKeyName.Length);
						}

						bFind = TRUE;
						break;
					}
				}

				if (bFind == FALSE)
				{
					bOk = FALSE;
				}
			}
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		bOk = TRUE;
	}

	return bOk;
}


VOID 
InsertDriver(OUT PKERNEL_MODULE_INFORMATION kmi, IN PDRIVER_OBJECT DriverObject, IN UINT32 NumberOfDrivers)
{
	if (!kmi || !DriverObject || !MmIsAddressValid(DriverObject))
	{
		return;
	}
	else
	{
		PLDR_DATA_TABLE_ENTRY LdrDataTableEntry = (PLDR_DATA_TABLE_ENTRY)DriverObject->DriverSection;

		if (LdrDataTableEntry &&
			MmIsAddressValid(LdrDataTableEntry) &&
			MmIsAddressValid((PVOID)LdrDataTableEntry->DllBase) &&
			(UINT_PTR)LdrDataTableEntry->DllBase > g_DynamicData.KernelStartAddress)
		{
			UINT32 Count = kmi->NumberOfDrivers;

			if (NumberOfDrivers > Count)
			{
				kmi->Drivers[Count].BaseAddress = (UINT_PTR)LdrDataTableEntry->DllBase;
				kmi->Drivers[Count].Size = LdrDataTableEntry->SizeOfImage;
				kmi->Drivers[Count].DriverObject = (UINT_PTR)DriverObject;

				if (IsUnicodeStringValid(&(LdrDataTableEntry->FullDllName)))
				{
					wcsncpy(kmi->Drivers[Count].wzDriverPath, (WCHAR*)(LdrDataTableEntry->FullDllName.Buffer), LdrDataTableEntry->FullDllName.Length);
				}
				else if (IsUnicodeStringValid(&(LdrDataTableEntry->BaseDllName)))
				{
					wcsncpy(kmi->Drivers[Count].wzDriverPath, (WCHAR*)(LdrDataTableEntry->BaseDllName.Buffer), LdrDataTableEntry->BaseDllName.Length);
				}
			}
			kmi->NumberOfDrivers++;
		}
	}
}

// 
VOID
TravelDirectoryObject(IN PVOID DirectoryObject, OUT PKERNEL_MODULE_INFORMATION kmi, IN UINT32 NumberOfDrivers)
{

	if (kmi	&& DirectoryObject && MmIsAddressValid(DirectoryObject))
	{
		ULONG i = 0;
		POBJECT_DIRECTORY ObjectDirectory = (POBJECT_DIRECTORY)DirectoryObject;
		KIRQL OldIrql = KeRaiseIrqlToDpcLevel();	// 提高中断级别

		__try
		{
			// 哈希表
			for (i = 0; i < NUMBER_HASH_BUCKETS; i++)	 // 遍历数组结构 每个数组成员都有一条链表
			{
				POBJECT_DIRECTORY_ENTRY ObjectDirectoryEntry = ObjectDirectory->HashBuckets[i];

				// 所以此处再次遍历链表结构
				for (; (UINT_PTR)ObjectDirectoryEntry > g_DynamicData.KernelStartAddress && MmIsAddressValid(ObjectDirectoryEntry);
					ObjectDirectoryEntry = ObjectDirectoryEntry->ChainLink)	
				{
					if (MmIsAddressValid(ObjectDirectoryEntry->Object))
					{
						POBJECT_TYPE ObjectType = KeGetObjectType(ObjectDirectoryEntry->Object);

						//
						// 如果是目录，那么继续递归遍历
						//
						if (ObjectType == g_DirectoryObjectType)
						{
							TravelDirectoryObject(ObjectDirectoryEntry->Object, kmi, NumberOfDrivers);
						}

						//
						// 如果是驱动对象
						//
						else if (ObjectType == *IoDriverObjectType)
						{
							PDEVICE_OBJECT DeviceObject = NULL;

							if (!IsDriverInList(kmi, (PDRIVER_OBJECT)ObjectDirectoryEntry->Object, NumberOfDrivers))
							{
								InsertDriver(kmi, (PDRIVER_OBJECT)ObjectDirectoryEntry->Object, NumberOfDrivers);
							}

							//
							// 遍历设备栈！！！
							//
							for (DeviceObject = ((PDRIVER_OBJECT)ObjectDirectoryEntry->Object)->DeviceObject;
								DeviceObject && MmIsAddressValid(DeviceObject);
								DeviceObject = DeviceObject->AttachedDevice)
							{
								if (!IsDriverInList(kmi, DeviceObject->DriverObject, NumberOfDrivers))
								{
									InsertDriver(kmi, DeviceObject->DriverObject, NumberOfDrivers);
								}
							}
						}

						//
						// 如果是设备对象
						//
						else if (ObjectType == *IoDeviceObjectType)
						{
							PDEVICE_OBJECT DeviceObject = NULL;

							if (!IsDriverInList(kmi, ((PDEVICE_OBJECT)ObjectDirectoryEntry->Object)->DriverObject, NumberOfDrivers))
							{
								InsertDriver(kmi, ((PDEVICE_OBJECT)ObjectDirectoryEntry->Object)->DriverObject, NumberOfDrivers);
							}

							//
							// 遍历设备栈
							//
							for (DeviceObject = ((PDEVICE_OBJECT)ObjectDirectoryEntry->Object)->AttachedDevice;
								DeviceObject && MmIsAddressValid(DeviceObject);
								DeviceObject = DeviceObject->AttachedDevice)
							{
								if (!IsDriverInList(kmi, DeviceObject->DriverObject, NumberOfDrivers))
								{
									InsertDriver(kmi, DeviceObject->DriverObject, NumberOfDrivers);
								}
							}
						}
					}
				}
			}
		}
		__except (1)
		{
		}

		KeLowerIrql(OldIrql);
	}
}


VOID 
EnumKernelModuleByDirectoryObject(OUT PKERNEL_MODULE_INFORMATION kmi, IN UINT32 NumberOfDrivers)
{
	NTSTATUS Status = STATUS_UNSUCCESSFUL;
	HANDLE   DirectoryHandle = NULL;

	WCHAR             wzDirectory[] = { L'\\', L'\0' };
	UNICODE_STRING    uniDirectory = { 0 };
	OBJECT_ATTRIBUTES oa = { 0 };

	RtlInitUnicodeString(&uniDirectory, wzDirectory);
	InitializeObjectAttributes(&oa, &uniDirectory, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);

	Status = MyZwOpenDirectoryObject(DirectoryHandle, 0, &oa);

	if (NT_SUCCESS(Status))
	{
		PVOID  DirectoryObject = NULL;

		// 将句柄转为对象
		Status = ObReferenceObjectByHandle(DirectoryHandle, GENERIC_ALL, NULL, KernelMode, &DirectoryObject, NULL);
		if (NT_SUCCESS(Status))
		{
			g_DirectoryObjectType = KeGetObjectType(DirectoryObject);		// 全局保存目录对象类型 便于后续比较

			TravelDirectoryObject(DirectoryObject, kmi, NumberOfDrivers);
			ObfDereferenceObject(DirectoryObject);
		}

		Status = NtClose(DirectoryHandle);
	}

}



NTSTATUS
EnumSystemModuleList(OUT PVOID OutputBuffer, IN UINT32 OutputLength)
{
	NTSTATUS Status = STATUS_UNSUCCESSFUL;

	UINT32	NumberOfDrivers = (OutputLength - sizeof(KERNEL_MODULE_INFORMATION)) / sizeof(KERNEL_MODULE_ENTRY_INFORMATION);		// Ring3传递Length

	if (OutputBuffer != NULL)
	{
		PLDR_DATA_TABLE_ENTRY KernelLdrEntry = GetKernelLdrDataTableEntry(g_DriverObject);

		EnumKernelModuleByLdrDataTableEntry(KernelLdrEntry, (PKERNEL_MODULE_INFORMATION)OutputBuffer, NumberOfDrivers);

		EnumKernelModuleByDirectoryObject((PKERNEL_MODULE_INFORMATION)OutputBuffer, NumberOfDrivers);

		if (NumberOfDrivers >= ((PKERNEL_MODULE_INFORMATION)OutputBuffer)->NumberOfDrivers)
		{
			Status = STATUS_SUCCESS;
		}
		else
		{
			Status = STATUS_BUFFER_TOO_SMALL;
		}
	}

	return Status;
}

