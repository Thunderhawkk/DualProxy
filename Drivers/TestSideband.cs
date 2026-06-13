using System;
using System.Runtime.InteropServices;

class TestSideband
{
	[DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Auto)]
	static extern IntPtr CreateFile(
		string lpFileName,
		uint dwDesiredAccess,
		uint dwShareMode,
		IntPtr lpSecurityAttributes,
		uint dwCreationDisposition,
		uint dwFlagsAndAttributes,
		IntPtr hTemplateFile);

	[DllImport("kernel32.dll", SetLastError = true)]
	static extern bool DeviceIoControl(
		IntPtr hDevice,
		uint dwIoControlCode,
		byte[] lpInBuffer,
		uint nInBufferSize,
		byte[] lpOutBuffer,
		uint nOutBufferSize,
		out uint lpBytesReturned,
		IntPtr lpOverlapped);

	[DllImport("kernel32.dll", SetLastError = true)]
	static extern bool CloseHandle(IntPtr hObject);

	const uint GENERIC_READ = 0x80000000;
	const uint GENERIC_WRITE = 0x40000000;
	const uint FILE_SHARE_READ = 0x00000001;
	const uint FILE_SHARE_WRITE = 0x00000002;
	const uint OPEN_EXISTING = 3;

	// IOCTL codes (must match Driver.h CTL_CODE macro)
	const uint FILE_DEVICE_VIRTUAL_DUALSENSE = 0x8601;
	const uint IOCTL_VDS_ACTIVATE = (FILE_DEVICE_VIRTUAL_DUALSENSE << 16) | (0 << 14) | (0x800 << 2) | 0;
	const uint IOCTL_VDS_SUBMIT_INPUT_REPORT = (FILE_DEVICE_VIRTUAL_DUALSENSE << 16) | (0 << 14) | (0x801 << 2) | 0;
	const uint IOCTL_VDS_READ_OUTPUT_REPORT = (FILE_DEVICE_VIRTUAL_DUALSENSE << 16) | (0 << 14) | (0x802 << 2) | 0;
	const uint IOCTL_VDS_GET_OUTPUT_REPORT_COUNT = (FILE_DEVICE_VIRTUAL_DUALSENSE << 16) | (0 << 14) | (0x803 << 2) | 0;

	static void Main()
	{
		Console.WriteLine("=== VirtualDualSense Sideband Test (v1.4 - Full DualSense) ===\n");

		IntPtr handle = CreateFile(
			@"\\.\VirtualDualSense0",
			GENERIC_READ | GENERIC_WRITE,
			FILE_SHARE_READ | FILE_SHARE_WRITE,
			IntPtr.Zero,
			OPEN_EXISTING,
			0,
			IntPtr.Zero);

		if (handle == new IntPtr(-1))
		{
			Console.WriteLine("FAILED: Could not open device. Error: " + Marshal.GetLastWin32Error());
			return;
		}

		Console.WriteLine("SUCCESS: Device opened!\n");

		uint bytesReturned;

		// Test 1: Activate the virtual DualSense (VHF already started in DeviceAdd)
		Console.WriteLine("Test 1: Activating VHF (IOCTL_VDS_ACTIVATE)...");
		bool ok = DeviceIoControl(handle, IOCTL_VDS_ACTIVATE,
			null, 0, null, 0, out bytesReturned, IntPtr.Zero);

		if (ok)
		{
			Console.WriteLine("Test 1 PASS: VHF activated!");
		}
		else
		{
			Console.WriteLine("Test 1 FAIL: Activate error " + Marshal.GetLastWin32Error());
			CloseHandle(handle);
			return;
		}

		// Give PnP a moment to enumerate the child device
		Console.WriteLine("\nWaiting 3 seconds for PnP...\n");
		System.Threading.Thread.Sleep(3000);

		// Test 2: Get output report count (should be 0)
		byte[] countBuf = new byte[4];
		ok = DeviceIoControl(handle, IOCTL_VDS_GET_OUTPUT_REPORT_COUNT,
			null, 0, countBuf, 4, out bytesReturned, IntPtr.Zero);

		if (ok && bytesReturned == 4)
		{
			int count = BitConverter.ToInt32(countBuf, 0);
			Console.WriteLine("Test 2 PASS: Output report count = " + count);
		}
		else
		{
			Console.WriteLine("Test 2 FAIL: DeviceIoControl error " + Marshal.GetLastWin32Error());
		}

		// Test 3: Submit a full 64-byte DualSense USB input report (Report ID 0x01)
		// This matches the real DualSense USB input report format
		byte[] inputReport = new byte[64];

		// Byte 0: Report ID = 0x01
		inputReport[0] = 0x01;

		// Bytes 1-4: Sequence number / timestamp (leave zero)
		// Byte 5: D-pad (bits 0-3) + Square(4) + Cross(5) + Circle(6) + Triangle(7)
		inputReport[5] = 0x08; // D-pad centered (8 = neutral)

		// Byte 6: L1(0)+R1(1)+L2(2)+R2(3)+Create(4)+Options(5)+L3(6)+R3(7) = all 0
		// Byte 7: PS(0)+Touchpad(1)+Mic(2)+reserved = all 0
		// Byte 8: Reserved
		// Byte 9: L2 trigger analog (0 = not pressed)
		// Byte 10: R2 trigger analog (0 = not pressed)

		// Bytes 11-12: Left stick X (16-bit LE, center 0x8000)
		inputReport[11] = 0x00;
		inputReport[12] = 0x80;

		// Bytes 13-14: Left stick Y (16-bit LE, center 0x8000)
		inputReport[13] = 0x00;
		inputReport[14] = 0x80;

		// Bytes 15-16: Right stick X (16-bit LE, center 0x8000)
		inputReport[15] = 0x00;
		inputReport[16] = 0x80;

		// Bytes 17-18: Right stick Y (16-bit LE, center 0x8000)
		inputReport[17] = 0x00;
		inputReport[18] = 0x80;

		// Bytes 19-63: Touchpad, IMU, battery, etc. - leave zero (neutral)

		ok = DeviceIoControl(handle, IOCTL_VDS_SUBMIT_INPUT_REPORT,
			inputReport, 64, null, 0, out bytesReturned, IntPtr.Zero);

		if (ok)
		{
			Console.WriteLine("Test 3 PASS: Full DualSense input report submitted (64 bytes, Report ID 0x01)");
		}
		else
		{
			Console.WriteLine("Test 3 FAIL: Submit error " + Marshal.GetLastWin32Error());
		}

		// Test 3b: Submit a report with some buttons pressed (Cross + R1)
		byte[] inputReport2 = new byte[64];
		inputReport2[0] = 0x01; // Report ID
		inputReport2[5] = 0x08 | 0x20; // D-pad center + Cross (bit 5)
		inputReport2[6] = 0x02; // R1 (bit 1)
		inputReport2[9] = 0xFF; // L2 trigger fully pressed
		inputReport2[10] = 0x80; // R2 trigger half pressed
		// Sticks centered
		inputReport2[11] = 0x00; inputReport2[12] = 0x80;
		inputReport2[13] = 0x00; inputReport2[14] = 0x80;
		inputReport2[15] = 0x00; inputReport2[16] = 0x80;
		inputReport2[17] = 0x00; inputReport2[18] = 0x80;

		ok = DeviceIoControl(handle, IOCTL_VDS_SUBMIT_INPUT_REPORT,
			inputReport2, 64, null, 0, out bytesReturned, IntPtr.Zero);

		if (ok)
		{
			Console.WriteLine("Test 3b PASS: Input report with Cross+R1+L2 full+R2 half submitted");
		}
		else
		{
			Console.WriteLine("Test 3b FAIL: Submit error " + Marshal.GetLastWin32Error());
		}

		// Test 4: Read output report (non-blocking, should return STATUS_DEVICE_NOT_READY
		// unless a game is actively sending haptics/rumble to the virtual controller)
		byte[] outputBuf = new byte[48];
		ok = DeviceIoControl(handle, IOCTL_VDS_READ_OUTPUT_REPORT,
			null, 0, outputBuf, 48, out bytesReturned, IntPtr.Zero);

		if (ok && bytesReturned > 0)
		{
			Console.WriteLine("Test 4: Got output report (" + bytesReturned + " bytes) - a game is sending data!");
			Console.WriteLine("  Report ID: 0x" + outputBuf[0].ToString("X2"));
		}
		else
		{
			int err = Marshal.GetLastWin32Error();
			Console.WriteLine("Test 4 PASS: No output report available (expected, no game connected). Win32 error: " + err);
		}

		// Test 5: Submit a neutral report (revert to idle)
		byte[] inputReportIdle = new byte[64];
		inputReportIdle[0] = 0x01;
		inputReportIdle[5] = 0x08; // D-pad center only
		inputReportIdle[11] = 0x00; inputReportIdle[12] = 0x80;
		inputReportIdle[13] = 0x00; inputReportIdle[14] = 0x80;
		inputReportIdle[15] = 0x00; inputReportIdle[16] = 0x80;
		inputReportIdle[17] = 0x00; inputReportIdle[18] = 0x80;

		ok = DeviceIoControl(handle, IOCTL_VDS_SUBMIT_INPUT_REPORT,
			inputReportIdle, 64, null, 0, out bytesReturned, IntPtr.Zero);

		if (ok)
		{
			Console.WriteLine("Test 5 PASS: Idle/neutral input report submitted");
		}
		else
		{
			Console.WriteLine("Test 5 FAIL: Submit error " + Marshal.GetLastWin32Error());
		}

		Console.WriteLine("\n=== All tests complete ===");
		Console.WriteLine("Check joy.cpl now - the virtual DualSense should appear!");
		Console.WriteLine("Also check Device Manager > HIDClass for 'HID-compliant game controller'");
		Console.WriteLine("\nIf a game sends output reports (rumble/haptics), they'll be readable via IOCTL_VDS_READ_OUTPUT_REPORT");
		CloseHandle(handle);
	}
}
