param([Parameter(Mandatory = $true)][string]$Path)

$ErrorActionPreference = 'Stop'
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

public static class RestartManagerLock {
    const int MoreData = 234;
    const int MaxAppName = 255;
    const int MaxServiceName = 63;

    [StructLayout(LayoutKind.Sequential)]
    struct UniqueProcess {
        public int ProcessId;
        public System.Runtime.InteropServices.ComTypes.FILETIME ProcessStartTime;
    }

    enum AppType { Unknown, MainWindow, OtherWindow, Service, Explorer, Console, Critical = 1000 }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    struct ProcessInfo {
        public UniqueProcess Process;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = MaxAppName + 1)] public string AppName;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = MaxServiceName + 1)] public string ServiceShortName;
        public AppType ApplicationType;
        public uint AppStatus;
        public uint TerminalSessionId;
        [MarshalAs(UnmanagedType.Bool)] public bool Restartable;
    }

    [DllImport("rstrtmgr.dll", CharSet = CharSet.Unicode)]
    static extern int RmStartSession(out uint handle, int flags, string sessionKey);
    [DllImport("rstrtmgr.dll")]
    static extern int RmEndSession(uint handle);
    [DllImport("rstrtmgr.dll", CharSet = CharSet.Unicode)]
    static extern int RmRegisterResources(uint handle, uint fileCount, string[] files,
        uint applicationCount, IntPtr applications, uint serviceCount, string[] services);
    [DllImport("rstrtmgr.dll")]
    static extern int RmGetList(uint handle, out uint needed, ref uint count,
        [In, Out] ProcessInfo[] affected, ref uint rebootReasons);

    public static string[] Find(string path) {
        uint handle;
        var key = Guid.NewGuid().ToString("N");
        var result = RmStartSession(out handle, 0, key);
        if (result != 0) throw new InvalidOperationException("RmStartSession=" + result);
        try {
            result = RmRegisterResources(handle, 1, new[] { path }, 0, IntPtr.Zero, 0, null);
            if (result != 0) throw new InvalidOperationException("RmRegisterResources=" + result);
            uint needed = 0, count = 0, reasons = 0;
            result = RmGetList(handle, out needed, ref count, null, ref reasons);
            if (result == 0) return new string[0];
            if (result != MoreData) throw new InvalidOperationException("RmGetList=" + result);
            var processes = new ProcessInfo[needed];
            count = needed;
            result = RmGetList(handle, out needed, ref count, processes, ref reasons);
            if (result != 0) throw new InvalidOperationException("RmGetList=" + result);
            var output = new string[count];
            for (var i = 0; i < count; ++i)
                output[i] = processes[i].Process.ProcessId + "|" + processes[i].AppName + "|" +
                    processes[i].ApplicationType + "|restartable=" + processes[i].Restartable;
            return output;
        } finally { RmEndSession(handle); }
    }
}
'@

$resolved = (Resolve-Path -LiteralPath $Path).Path
[RestartManagerLock]::Find($resolved)
