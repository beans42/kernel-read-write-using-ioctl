#pragma once
#include <Windows.h>

constexpr DWORD init_code  = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x775, METHOD_BUFFERED, FILE_SPECIAL_ACCESS); //custom io control code for setting g_target_process by target process id
constexpr DWORD read_code  = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x776, METHOD_BUFFERED, FILE_SPECIAL_ACCESS); //custom io control code for reading memory
constexpr DWORD write_code = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x777, METHOD_BUFFERED, FILE_SPECIAL_ACCESS); //custom io control code for writing memory

class driver_manager {
	HANDLE m_driver_handle = nullptr; //handle to our driver

    struct info_t { //message type that will be passed between user program and driver
        UINT64 target_pid = 0; //process id of process we want to read from / write to
        UINT64 target_address = 0x0; //address in the target proces we want to read from / write to
        UINT64 buffer_address = 0x0; //address in our usermode process to copy to (read mode) / read from (write mode)
        UINT64 size = 0; //size of memory to copy between our usermode process and target process
        UINT64 return_size = 0; //number of bytes successfully read / written
    };

public:
    driver_manager(const char* driver_name, DWORD target_process_id) {
		m_driver_handle = CreateFileA(driver_name, GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr); //get a handle to our driver
        attach_to_process(target_process_id); //tell driver to find the process with this id and put it into g_target_process
	}

    void attach_to_process(DWORD process_id) {
        info_t io_info;
        
        io_info.target_pid = process_id;

        DeviceIoControl(m_driver_handle, init_code, &io_info, sizeof(io_info), &io_info, sizeof(io_info), nullptr, nullptr);
    }

    template<typename T> T RPM(const UINT64 address) {
        info_t io_info;
        T read_data;

        io_info.target_address = address;
        io_info.buffer_address = (UINT64)&read_data;
        io_info.size = sizeof(T);

        DeviceIoControl(m_driver_handle, read_code, &io_info, sizeof(io_info), &io_info, sizeof(io_info), nullptr, nullptr);
        if (io_info.return_size != sizeof(T))
            throw 0xBEEF; //partial read
        return read_data;
    }

    template<typename T> bool WPM(const UINT64 address, const T buffer) {
        info_t io_info;

        io_info.target_address = address;
        io_info.buffer_address = (UINT64)&buffer;
        io_info.size = sizeof(T);

        DeviceIoControl(m_driver_handle, write_code, &io_info, sizeof(io_info), &io_info, sizeof(io_info), nullptr, nullptr);
        return io_info.return_size == sizeof(T);
    }
};
