#include "xboxkrnl/xboxdef.h"
#include <hal/debug.h>
#include <hal/video.h>
#include <windows.h>
#include <xboxkrnl/xboxkrnl.h>
#include <stdlib.h>
#include <stdbool.h>

#define BYTES_PER_KB 1024
#define KB_PER_MB 1024
#define TEST_READ_BYTES (BYTES_PER_KB * KB_PER_MB * 10)
#define TEST_DRIVE_OFFSET (BYTES_PER_KB * KB_PER_MB * 10)

#define TEST_BUFFER_BYTES (BYTES_PER_KB * KB_PER_MB * 10)

// SystemTime is a count of 100ns intervals
#define SECS_TO_SYS_TIME (10000000)

#define TRIALS 2
#define PROFILE_HDD 1
#define PROFILE_DVD 0

inline uint64_t min(uint64_t a, uint64_t b)
{
    return a < b ? a : b;
}

inline uint64_t max(uint64_t a, uint64_t b)
{
    return a >= b ? a : b;
}

bool profile_read(uint64_t *read_speed, uint64_t to_read, HANDLE *handle, uint8_t *buffer)
{
    NTSTATUS status;
    IO_STATUS_BLOCK io_status;

    uint64_t currently_read = 0;

    LARGE_INTEGER start_time;
    KeQuerySystemTime(&start_time);

    //while (to_read > currently_read) {
    {
        LARGE_INTEGER offset = { .QuadPart = TEST_DRIVE_OFFSET + currently_read };

        status = NtReadFile(*handle, NULL, NULL, NULL, &io_status, buffer, min(to_read - currently_read, TEST_BUFFER_BYTES), &offset);
        if (!NT_SUCCESS(status) || io_status.Information == 0) {
            debugPrint("Read Error! %lu %lu\n", io_status.Information, status);
            return false;
        }

        currently_read += io_status.Information;
    }

    LARGE_INTEGER end_time;
    KeQuerySystemTime(&end_time);

    // Safeguard so we don't write garbage over the drive
    if (currently_read != TEST_BUFFER_BYTES) {
        return false;
    }

    uint64_t elapsed_time = end_time.QuadPart - start_time.QuadPart;
    if (elapsed_time == 0) {
        debugPrint("Delta time is zero?\n");
        return false;
    }

    *read_speed = (currently_read * SECS_TO_SYS_TIME) / elapsed_time;
    return true;
}

bool profile_write(uint64_t *write_speed, uint64_t to_write, HANDLE *handle, uint8_t *buffer)
{
    NTSTATUS status;
    IO_STATUS_BLOCK io_status;

    uint64_t currently_read = 0;

    LARGE_INTEGER start_time;
    KeQuerySystemTime(&start_time);

    //while (to_write > currently_read) {
    {
        LARGE_INTEGER offset = { .QuadPart = TEST_DRIVE_OFFSET + currently_read };

        status = NtWriteFile(*handle, NULL, NULL, NULL, &io_status, buffer, min(to_write - currently_read, TEST_BUFFER_BYTES), &offset);
        if (!NT_SUCCESS(status) || io_status.Information == 0) {
            debugPrint("Write Error! %lu %lu\n", io_status.Information, status);
            return false;
        }

        currently_read += io_status.Information;
    }

    LARGE_INTEGER end_time;
    KeQuerySystemTime(&end_time);

    uint64_t elapsed_time = end_time.QuadPart - start_time.QuadPart;
    if (elapsed_time == 0) {
        debugPrint("Delta time is zero?\n");
        return false;
    }

    *write_speed = (currently_read * SECS_TO_SYS_TIME) / elapsed_time;
    return true;
}

bool profile_drive_handle(uint64_t *read_speed, uint64_t *write_speed, HANDLE *handle, uint8_t *buffer)
{
    NTSTATUS status;
    IO_STATUS_BLOCK io_status;

    uint64_t to_read = TEST_READ_BYTES;
    bool test_status;

    {
        DISK_GEOMETRY dg;
        status = NtDeviceIoControlFile(*handle, NULL, NULL, NULL, &io_status,
                IOCTL_DISK_GET_DRIVE_GEOMETRY, NULL, 0, &dg, sizeof(dg));
        if (NT_SUCCESS(status)) {
            uint64_t drive_bytes = dg.BytesPerSector * dg.SectorsPerTrack * dg.TracksPerCylinder * dg.Cylinders.QuadPart;
            to_read = min(drive_bytes, TEST_READ_BYTES);
        }
    }

    test_status = profile_read(read_speed, to_read, handle, buffer);
    if (!test_status) return false;

    if (!write_speed) {
        return true;
    }

    test_status = profile_write(write_speed, to_read, handle, buffer);
    return test_status;
}

bool profile_drive(uint64_t *read_speed, uint64_t *write_speed, const char *volume)
{
    ANSI_STRING volume_str;
    OBJECT_ATTRIBUTES obj_attrs;
    IO_STATUS_BLOCK io_status;
    HANDLE handle;
    NTSTATUS status;

    RtlInitAnsiString(&volume_str, volume);
    InitializeObjectAttributes(&obj_attrs, &volume_str, OBJ_CASE_INSENSITIVE, NULL, NULL);

    uint64_t access_mask = SYNCHRONIZE | FILE_READ_DATA;
    if (write_speed) {
        access_mask |= FILE_WRITE_DATA;
    }

    status = NtOpenFile(&handle, access_mask, &obj_attrs, &io_status, 0, FILE_SYNCHRONOUS_IO_ALERT | FILE_NO_INTERMEDIATE_BUFFERING);
    if (!NT_SUCCESS(status)) {
        debugPrint("Failed to open device\n");
        return false;
    }

    uint8_t *buffer = malloc(TEST_BUFFER_BYTES);
    bool success = profile_drive_handle(read_speed, write_speed, &handle, buffer);

    free(buffer);
    NtClose(handle);
    return success;
}

struct drive {
    char letter;
    const char *path;
};

const struct drive drives[] = {
    { 'X', "\\Device\\HardDisk0\\Partition3" },
    { 'Y', "\\Device\\HardDisk0\\Partition4" },
    { 'Z', "\\Device\\HardDisk0\\Partition5" },
    { 'D', "\\Device\\CdRom0" },
};

void run_trials(const struct drive *dr)
{
    uint64_t read_speed = 0;
    uint64_t write_speed = 0;

    uint64_t *write_ptr = (dr->letter == 'D') ? NULL : &write_speed;

    debugPrint("Profiling drive %c:\n", dr->letter);
    for (int trial = 1; trial <= TRIALS; ++trial) {

        bool worked = profile_drive(&read_speed, write_ptr, dr->path);
        if (worked) {
            debugPrint("Trial %d/%d: Read %llu Write %llu bytes/sec\n", trial, TRIALS, read_speed, write_speed);
        } else {
            debugPrint("Trial %d/%d: Error during test, aborting\n", trial, TRIALS);
            break;
        }
    }

    debugPrint("\n");
}

int main()
{
    XVideoSetMode(640, 480, 32, REFRESH_DEFAULT);
    debugClearScreen();
    debugPrint("Xbox Drive Profiler\n\n");

#if PROFILE_HDD
    run_trials(drives + 0);
    run_trials(drives + 1);
    run_trials(drives + 2);
#endif

#if PROFILE_DVD
    run_trials(drives + 3);
#endif

    debugPrint("Drive test complete\n");
    while (1) {
        Sleep(1000);
    }

    return 0;
}
