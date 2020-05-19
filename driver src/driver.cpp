#include <ntifs.h>

extern "C" { //undocumented windows internal functions (exported by ntoskrnl)
	NTKERNELAPI NTSTATUS IoCreateDriver(PUNICODE_STRING DriverName, PDRIVER_INITIALIZE InitializationFunction);
	NTKERNELAPI NTSTATUS MmCopyVirtualMemory(PEPROCESS SourceProcess, PVOID SourceAddress, PEPROCESS TargetProcess, PVOID TargetAddress, SIZE_T BufferSize, KPROCESSOR_MODE PreviousMode, PSIZE_T ReturnSize);
}

constexpr ULONG init_code  = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x775, METHOD_BUFFERED, FILE_SPECIAL_ACCESS); //custom io control code for setting g_target_process by target process id
constexpr ULONG read_code  = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x776, METHOD_BUFFERED, FILE_SPECIAL_ACCESS); //custom io control code for reading memory
constexpr ULONG write_code = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x777, METHOD_BUFFERED, FILE_SPECIAL_ACCESS); //custom io control code for writing memory

struct info_t { //message type that will be passed between user program and driver
	HANDLE target_pid = 0; //process id of process we want to read from / write to
	void*  target_address = 0x0; //address in the target proces we want to read from / write to
	void*  buffer_address = 0x0; //address in our usermode process to copy to (read mode) / read from (write mode)
	SIZE_T size = 0; //size of memory to copy between our usermode process and target process
	SIZE_T return_size = 0; //number of bytes successfully read / written
};

NTSTATUS ctl_io(PDEVICE_OBJECT device_obj, PIRP irp) {
	UNREFERENCED_PARAMETER(device_obj);
	
	static PEPROCESS s_target_process;

	irp->IoStatus.Information = sizeof(info_t);
	auto stack = IoGetCurrentIrpStackLocation(irp);
	auto buffer = (info_t*)irp->AssociatedIrp.SystemBuffer;

	if (stack) { //add error checking
		if (buffer && sizeof(*buffer) >= sizeof(info_t)) {
			const auto ctl_code = stack->Parameters.DeviceIoControl.IoControlCode;

			if (ctl_code == init_code) //if control code is find process, find the target process by its id and put it into g_target_process
				PsLookupProcessByProcessId(buffer->target_pid, &s_target_process);

			else if (ctl_code == read_code) //if control code is read, copy target process memory to our process
				MmCopyVirtualMemory(s_target_process, buffer->target_address, PsGetCurrentProcess(), buffer->buffer_address, buffer->size, KernelMode, &buffer->return_size);

			else if (ctl_code == write_code) //if control code is write, copy our process memory to target process memory
				MmCopyVirtualMemory(PsGetCurrentProcess(), buffer->buffer_address, s_target_process, buffer->target_address, buffer->size, KernelMode, &buffer->return_size);
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

	RtlInitUnicodeString(&dev_name, L"\\Device\\cartidriver"); //die lit
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
