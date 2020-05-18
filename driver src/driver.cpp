#include <ntifs.h>

constexpr ULONG read_code  = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x776, METHOD_BUFFERED, FILE_SPECIAL_ACCESS); //custom io control codes for reading memory
constexpr ULONG write_code = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x777, METHOD_BUFFERED, FILE_SPECIAL_ACCESS); //custom io control codes for writing memory

extern "C" { //undocumented windows internal functions (exported by ntoskrnl)
	NTKERNELAPI NTSTATUS IoCreateDriver(PUNICODE_STRING DriverName, PDRIVER_INITIALIZE InitializationFunction);
	NTKERNELAPI NTSTATUS MmCopyVirtualMemory(PEPROCESS SourceProcess, PVOID SourceAddress, PEPROCESS TargetProcess, PVOID TargetAddress, SIZE_T BufferSize, KPROCESSOR_MODE PreviousMode, PSIZE_T ReturnSize);
}

struct info_t { //message type that will be passed between user program and driver
	UINT32 target_pid = 0; //process id of process we want to read from / write to
	UINT64 target_address = 0x0; //address in the target proces we want to read from / write to
	UINT64 buffer_address = 0x0; //address in our usermode process to copy to (read mode) / read from (write mode)
	UINT64 size = 0; //size of memory to copy between our usermode process and target process
};

bool read_mem(int pid, void* addr, void* value, size_t size) {
	PEPROCESS pe;
	SIZE_T bytes;
	PsLookupProcessByProcessId((HANDLE)pid, &pe);
	ProbeForRead(addr, size, 1);
	MmCopyVirtualMemory(pe, addr, PsGetCurrentProcess(), value, size, KernelMode, &bytes);
	ObfDereferenceObject(pe);
	return bytes == size;
}

bool write_mem(int pid, void* addr, void* value, size_t size) {
	PEPROCESS pe;
	SIZE_T bytes;
	PsLookupProcessByProcessId((HANDLE)pid, &pe);
	MmCopyVirtualMemory(PsGetCurrentProcess(), value, pe, addr, size, KernelMode, &bytes);
	ObfDereferenceObject(pe);
	return bytes == size;
}

NTSTATUS ctl_io(PDEVICE_OBJECT device_obj, PIRP irp) {
	UNREFERENCED_PARAMETER(device_obj);
	
	irp->IoStatus.Information = sizeof(info_t);
	auto stack = IoGetCurrentIrpStackLocation(irp);
	auto buffer = (info_t*)irp->AssociatedIrp.SystemBuffer;

	if (stack) { //add error checking
		if (buffer && sizeof(*buffer) >= sizeof(info_t)) {
			if (stack->Parameters.DeviceIoControl.IoControlCode == read_code) { //if control code is read, copy target process memory to our process
				if (buffer->target_address < 0x7FFFFFFFFFFF)
					read_mem(buffer->target_pid, (void*)buffer->target_address, (void*)buffer->buffer_address, buffer->size);
			}
			else if (stack->Parameters.DeviceIoControl.IoControlCode == write_code) { //if control code is write, copy our process memory to target process memory
				if (buffer->target_address < 0x7FFFFFFFFFFF)
					write_mem(buffer->target_pid, (void*)buffer->target_address, (void*)buffer->buffer_address, buffer->size);
			}
		}
	}

	IoCompleteRequest(irp, IO_NO_INCREMENT);
	return STATUS_SUCCESS;
}

NTSTATUS unsupported_io(PDEVICE_OBJECT device_obj, PIRP irp) {
	UNREFERENCED_PARAMETER(device_obj);
	
	irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
	IoCompleteRequest(irp, IO_NO_INCREMENT);
	return irp->IoStatus.Status;
}

NTSTATUS create_io(PDEVICE_OBJECT device_obj, PIRP irp) {
	UNREFERENCED_PARAMETER(device_obj);

	IoCompleteRequest(irp, IO_NO_INCREMENT);
	return irp->IoStatus.Status;
}

NTSTATUS close_io(PDEVICE_OBJECT device_obj, PIRP irp) {
	UNREFERENCED_PARAMETER(device_obj);
	
	IoCompleteRequest(irp, IO_NO_INCREMENT);
	return irp->IoStatus.Status;
}

NTSTATUS real_main(PDRIVER_OBJECT driver_obj, PUNICODE_STRING registery_path) {
	UNREFERENCED_PARAMETER(registery_path);
	
	UNICODE_STRING dev_name, sym_link;
	PDEVICE_OBJECT dev_obj;

	RtlInitUnicodeString(&dev_name, L"\\Device\\cartidriver");
	auto status = IoCreateDevice(driver_obj, 0, &dev_name, FILE_DEVICE_UNKNOWN, FILE_DEVICE_SECURE_OPEN, FALSE, &dev_obj);
	if (status != STATUS_SUCCESS) return status;

	RtlInitUnicodeString(&sym_link, L"\\DosDevices\\cartidriver");
	status = IoCreateSymbolicLink(&sym_link, &dev_name);
	if (status != STATUS_SUCCESS) return status;

	SetFlag(dev_obj->Flags, DO_BUFFERED_IO); //set DO_BUFFERED_IO bit to 1

	for (int t = 0; t <= IRP_MJ_MAXIMUM_FUNCTION; t++) //set all MajorFunction's to unsupported
		driver_obj->MajorFunction[t] = unsupported_io;

	//then set supported functions to appropriate handlers
	driver_obj->MajorFunction[IRP_MJ_CREATE] = create_io; //link our io create function
	driver_obj->MajorFunction[IRP_MJ_CLOSE] = close_io; //link our io close function
	driver_obj->MajorFunction[IRP_MJ_DEVICE_CONTROL] = ctl_io; //link our control code handler
	driver_obj->DriverUnload = NULL; //add later

	ClearFlag(dev_obj->Flags, DO_DEVICE_INITIALIZING); //set DO_DEVICE_INITIALIZING bit to 0 (we are done initializing)
	return status;
}

extern "C" NTSTATUS DriverEntry(PDRIVER_OBJECT driver_obj, PUNICODE_STRING registery_path) {
	UNREFERENCED_PARAMETER(driver_obj);
	UNREFERENCED_PARAMETER(registery_path);

	UNICODE_STRING  drv_name;
	RtlInitUnicodeString(&drv_name, L"\\Driver\\cartidriver");
	IoCreateDriver(&drv_name, &real_main); //so it's kdmapper-able

	return STATUS_SUCCESS;
}
