using System;
using System.Diagnostics;
using System.Globalization;
using System.Runtime.InteropServices;
using LibreHardwareMonitor.Hardware;

internal static class HardwareSensors
{
    [StructLayout(LayoutKind.Sequential)]
    private struct MemoryStatus
    {
        public uint Length, Load;
        public ulong TotalPhysical, AvailablePhysical, TotalPageFile, AvailablePageFile,
            TotalVirtual, AvailableVirtual, AvailableExtendedVirtual;
    }

    [DllImport("kernel32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool GlobalMemoryStatusEx(ref MemoryStatus status);

    private static float UsedMemoryMiB()
    {
        var status = new MemoryStatus { Length = (uint)Marshal.SizeOf<MemoryStatus>() };
        return GlobalMemoryStatusEx(ref status) && status.TotalPhysical >= status.AvailablePhysical
            ? (status.TotalPhysical - status.AvailablePhysical) / 1048576f
            : float.NaN;
    }

    private static float Get(ISensor[] sensors, SensorType type, string name)
    {
        if (sensors != null)
            foreach (var sensor in sensors)
                if (sensor.SensorType == type && sensor.Name == name && sensor.Value is float value)
                    return value;
        return float.NaN;
    }

    private static float ReadVcore(IHardware board)
    {
        if (board != null)
            foreach (var chip in board.SubHardware)
            {
                if (!chip.Name.Equals("ITE IT8689E", StringComparison.OrdinalIgnoreCase))
                    continue;
                chip.Update();
                foreach (var sensor in chip.Sensors)
                    if (sensor.SensorType == SensorType.Voltage &&
                        sensor.Identifier.ToString() == "/lpc/it8689e/0/voltage/0" &&
                        sensor.Value is float value && value > 0 && value <= 3)
                        return value;
            }
        return float.NaN;
    }

    private static int Main(string[] args)
    {
        if ((args.Length != 1 && args.Length != 2) || !int.TryParse(args[0], out int pid))
            return 2;

        bool full = args.Length == 1;
        using (var owner = Process.GetProcessById(pid))
        {
            var computer = new Computer { IsMotherboardEnabled = true, IsCpuEnabled = full, IsGpuEnabled = full };
            try
            {
                computer.Open();
                IHardware cpu = null, gpu = null, board = null;
                foreach (var device in computer.Hardware)
                {
                    switch (device.HardwareType)
                    {
                        case HardwareType.Motherboard: if (device.Name.Contains("B650M AORUS ELITE AX ICE", StringComparison.OrdinalIgnoreCase)) board = device; break;
                        case HardwareType.Cpu: cpu ??= device; break;
                        case HardwareType.GpuNvidia:
                        case HardwareType.GpuAmd:
                        case HardwareType.GpuIntel:
                            device.Update();
                            float capacity = Get(device.Sensors, SensorType.SmallData, "GPU Memory Total");
                            if (gpu == null || capacity > Get(gpu?.Sensors, SensorType.SmallData, "GPU Memory Total") ||
                                (device.HardwareType == HardwareType.GpuNvidia && gpu.HardwareType != HardwareType.GpuNvidia &&
                                 !float.IsFinite(Get(gpu?.Sensors, SensorType.SmallData, "GPU Memory Total"))))
                                gpu = device;
                            break;
                    }
                }

                var values = new float[14];
                Span<char> line = stackalloc char[240];
                do
                {
                    cpu?.Update();
                    gpu?.Update();
                    var gpuSensors = gpu?.Sensors;
                    var cpuSensors = cpu?.Sensors;
                    values[0] = Get(gpuSensors, SensorType.Temperature, "GPU Core");
                    values[1] = Get(gpuSensors, SensorType.Load, "GPU Core");
                    values[2] = Get(gpuSensors, SensorType.Load, "GPU Video Engine");
                    values[3] = Get(gpuSensors, SensorType.SmallData, "GPU Memory Used");
                    values[4] = Get(gpuSensors, SensorType.Clock, "GPU Core");
                    values[5] = Get(gpuSensors, SensorType.Clock, "GPU Memory");
                    values[6] = Get(gpuSensors, SensorType.Power, "GPU Package");
                    values[7] = Get(gpuSensors, SensorType.Voltage, "GPU Core Voltage");
                    if (!float.IsFinite(values[7])) values[7] = Get(gpuSensors, SensorType.Voltage, "GPU Core");
                    values[8] = Get(cpuSensors, SensorType.Temperature, "Core (Tctl/Tdie)");
                    if (!float.IsFinite(values[8])) values[8] = Get(cpuSensors, SensorType.Temperature, "CPU Package");
                    values[9] = Get(cpuSensors, SensorType.Load, "CPU Total");
                    values[10] = Get(cpuSensors, SensorType.Clock, "Cores (Average)");
                    values[11] = Get(cpuSensors, SensorType.Power, "Package");
                    if (!float.IsFinite(values[11])) values[11] = Get(cpuSensors, SensorType.Power, "CPU Package");
                    values[12] = full ? UsedMemoryMiB() : float.NaN;
                    values[13] = ReadVcore(board);
                    "HW".AsSpan().CopyTo(line);
                    int length = 2;
                    foreach (float value in values)
                    {
                        line[length++] = ',';
                        if (float.IsFinite(value) &&
                            value.TryFormat(line[length..], out int written, "G9", CultureInfo.InvariantCulture))
                            length += written;
                        else
                        {
                            "nan".AsSpan().CopyTo(line[length..]);
                            length += 3;
                        }
                    }
                    Console.Out.WriteLine(line[..length]);
                } while (!owner.WaitForExit(2000));
            }
            finally
            {
                computer.Close();
            }
        }
        return 0;
    }
}
