#include <hal/debug.h>
#include <hal/video.h>
#include <windows.h>
#include <xboxkrnl/xboxkrnl.h>
#include <stdlib.h>
#include <stdbool.h>

#define BYTES_PER_KB 1024
#define KB_PER_MB 1024
#define TEST_READ_BYTES (BYTES_PER_KB * KB_PER_MB * 512)

#define TEST_BUFFER_BYTES (BYTES_PER_KB * KB_PER_MB * 1)

// SystemTime is a count of 100ns intervals
#define SECS_TO_SYS_TIME (10000000)

#define TRIALS 2
#define PROFILE_HDD 1
#define PROFILE_DVD 1

inline uint64_t min(uint64_t a, uint64_t b)
{
    return a < b ? a : b;
}

bool profile_drive_handle(uint64_t *speed, HANDLE *handle)
{
    DISK_GEOMETRY dg;
    NTSTATUS status;
    IO_STATUS_BLOCK io_status;

    status = NtDeviceIoControlFile(*handle, NULL, NULL, NULL, &io_status, IOCTL_DISK_GET_DRIVE_GEOMETRY, NULL, 0, &dg, sizeof(dg));
    if (!NT_SUCCESS(status)) return false;

    uint64_t drive_bytes = dg.BytesPerSector * dg.SectorsPerTrack * dg.TracksPerCylinder * dg.Cylinders.QuadPart;
    uint64_t to_read = min(drive_bytes, TEST_READ_BYTES);

    uint8_t *buffer = malloc(TEST_BUFFER_BYTES);
    uint64_t currently_read = 0;

    //debugPrint("Going to read %llu bytes\n", to_read);

    LARGE_INTEGER start_time;
    KeQuerySystemTime(&start_time);

    while (to_read > currently_read) {
        status = NtReadFile(*handle, NULL, NULL, NULL, &io_status, buffer, TEST_BUFFER_BYTES, NULL);
        if (!NT_SUCCESS(status) || io_status.Information == 0) {
            debugPrint("Error! %lu\n", io_status.Information);
            free(buffer);
            return false;
        }

        currently_read += io_status.Information;
    }

    LARGE_INTEGER end_time;
    KeQuerySystemTime(&end_time);

    uint64_t elapsed_time = end_time.QuadPart - start_time.QuadPart;
    if (elapsed_time == 0) {
        debugPrint("Delta time is zero?\n");
        free(buffer);
        return false;
    }

    //debugPrint("%llu ns elapsed\n", elapsed_time * 100);

    *speed = (currently_read * SECS_TO_SYS_TIME) / elapsed_time;

    free(buffer);
    return true;
}

bool profile_drive(uint64_t *speed, const char *volume)
{
    ANSI_STRING volume_str;
    OBJECT_ATTRIBUTES obj_attrs;
    IO_STATUS_BLOCK io_status;
    HANDLE handle;
    NTSTATUS status;

    RtlInitAnsiString(&volume_str, volume);
    InitializeObjectAttributes(&obj_attrs, &volume_str, OBJ_CASE_INSENSITIVE, NULL, NULL);

    status = NtOpenFile(&handle, SYNCHRONIZE | FILE_READ_DATA, &obj_attrs, &io_status, 0, FILE_SYNCHRONOUS_IO_ALERT | FILE_NO_INTERMEDIATE_BUFFERING);
    if (!NT_SUCCESS(status)) return false;

    bool success = profile_drive_handle(speed, &handle);
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
    { 'E', "\\Device\\HardDisk0\\Partition1" },
    { 'D', "\\Device\\CdRom0" },
};

void run_trials(const struct drive *dr)
{
    debugPrint("Profiling drive %c:\n", dr->letter);
    for (int trial = 1; trial <= TRIALS; ++trial) {
        uint64_t speed = 0;
        bool worked = profile_drive(&speed, dr->path);
        if (!worked) debugPrint("Trial %d/%d: Error reading drive\n", trial, TRIALS);
        else debugPrint("Trial %d/%d: %llu bytes/sec\n", trial, TRIALS, speed);
    }

    debugPrint("\n");
}

int main()
{
    XVideoSetMode(640, 480, 32, REFRESH_DEFAULT);
    debugClearScreen();

#if PROFILE_HDD
    run_trials(drives + 0);
    run_trials(drives + 1);
    run_trials(drives + 2);
    run_trials(drives + 3);
#endif

#if PROFILE_DVD
    run_trials(drives + 4);
#endif

    debugPrint("Drive test complete\n");
    while (1) {
        Sleep(1000);
    }

    return 0;
}
