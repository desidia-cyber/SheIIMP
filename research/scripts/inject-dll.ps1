<#
  inject-dll.ps1 - inject one x64 DLL into a running x64 process (LoadLibraryW
  through CreateRemoteThread).

  The remote process's own kernel32.dll base is resolved with
  EnumProcessModulesEx/GetModuleFileNameEx and LoadLibraryW is addressed by the
  in-module delta, so per-process ASLR cannot break the call.

  Usage:  .\inject-dll.ps1 -ProcessId 12345 -DllPath 'C:\path\to\Dumper-7.dll'
#>
param(
    [Parameter(Mandatory = $true)][int]$ProcessId,
    [Parameter(Mandatory = $true)][string]$DllPath
)

$ErrorActionPreference = 'Stop'
if (-not (Test-Path $DllPath)) { throw "dll not found: $DllPath" }
$full = (Resolve-Path $DllPath).Path
if (-not $full) { throw "could not resolve $DllPath" }

if (-not ('Injector' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.IO;
using System.Text;
using System.Runtime.InteropServices;

public static class Injector
{
    const uint PROCESS_CREATE_THREAD     = 0x0002;
    const uint PROCESS_QUERY_INFORMATION = 0x0400;
    const uint PROCESS_VM_OPERATION      = 0x0008;
    const uint PROCESS_VM_WRITE          = 0x0020;
    const uint PROCESS_VM_READ           = 0x0010;
    const uint MEM_COMMIT                = 0x1000;
    const uint MEM_RESERVE               = 0x2000;
    const uint MEM_RELEASE               = 0x8000;
    const uint PAGE_READWRITE            = 0x04;

    [DllImport("kernel32.dll", SetLastError = true)]
    static extern IntPtr OpenProcess(uint access, bool inherit, int pid);
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern IntPtr VirtualAllocEx(IntPtr h, IntPtr addr, UIntPtr size, uint type, uint protect);
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool VirtualFreeEx(IntPtr h, IntPtr addr, UIntPtr size, uint type);
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool WriteProcessMemory(IntPtr h, IntPtr addr, byte[] buf, UIntPtr size, out UIntPtr written);
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern IntPtr CreateRemoteThread(IntPtr h, IntPtr sa, UIntPtr stack, IntPtr start, IntPtr param, uint flags, out uint tid);
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern uint WaitForSingleObject(IntPtr h, uint ms);
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool GetExitCodeThread(IntPtr h, out uint code);
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool CloseHandle(IntPtr h);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern IntPtr GetModuleHandleW(string name);
    [DllImport("kernel32.dll", CharSet = CharSet.Ansi, SetLastError = true)]
    static extern IntPtr GetProcAddress(IntPtr mod, string name);
    [DllImport("psapi.dll", SetLastError = true)]
    static extern bool EnumProcessModulesEx(IntPtr h, IntPtr[] mods, int cb, out int needed, uint filter);
    [DllImport("psapi.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern int GetModuleFileNameExW(IntPtr h, IntPtr mod, StringBuilder name, int size);

    static IntPtr RemoteModuleBase(IntPtr hProc, string wantName)
    {
        IntPtr[] mods = new IntPtr[1024];
        int needed;
        if (!EnumProcessModulesEx(hProc, mods, mods.Length * IntPtr.Size, out needed, 0x03))
            throw new Exception("EnumProcessModulesEx failed: " + Marshal.GetLastWin32Error());
        int n = needed / IntPtr.Size;
        for (int i = 0; i < n && i < mods.Length; i++)
        {
            StringBuilder sb = new StringBuilder(260);
            if (GetModuleFileNameExW(hProc, mods[i], sb, sb.Capacity) > 0)
            {
                string leaf = Path.GetFileName(sb.ToString());
                if (string.Equals(leaf, wantName, StringComparison.OrdinalIgnoreCase))
                    return mods[i];
            }
        }
        return IntPtr.Zero;
    }

    public static uint Inject(int pid, string dllPath)
    {
        IntPtr hProc = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION
                                   | PROCESS_VM_WRITE | PROCESS_VM_READ, false, pid);
        if (hProc == IntPtr.Zero) throw new Exception("OpenProcess failed: " + Marshal.GetLastWin32Error());
        try
        {
            IntPtr localK32 = GetModuleHandleW("kernel32.dll");
            IntPtr localLL  = GetProcAddress(localK32, "LoadLibraryW");
            IntPtr remoteK32 = RemoteModuleBase(hProc, "kernel32.dll");
            if (remoteK32 == IntPtr.Zero) throw new Exception("remote kernel32.dll not found");
            IntPtr remoteLL = new IntPtr(remoteK32.ToInt64() + (localLL.ToInt64() - localK32.ToInt64()));

            byte[] pathBytes = Encoding.Unicode.GetBytes(dllPath + "\0");
            IntPtr remoteMem = VirtualAllocEx(hProc, IntPtr.Zero, (UIntPtr)pathBytes.Length, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
            if (remoteMem == IntPtr.Zero) throw new Exception("VirtualAllocEx failed: " + Marshal.GetLastWin32Error());
            UIntPtr written;
            if (!WriteProcessMemory(hProc, remoteMem, pathBytes, (UIntPtr)pathBytes.Length, out written))
                throw new Exception("WriteProcessMemory failed: " + Marshal.GetLastWin32Error());

            uint tid;
            IntPtr hThread = CreateRemoteThread(hProc, IntPtr.Zero, UIntPtr.Zero, remoteLL, remoteMem, 0, out tid);
            if (hThread == IntPtr.Zero) throw new Exception("CreateRemoteThread failed: " + Marshal.GetLastWin32Error());
            uint wait = WaitForSingleObject(hThread, 20000);
            uint exitCode = 0xFFFFFFFF;
            GetExitCodeThread(hThread, out exitCode);
            CloseHandle(hThread);
            VirtualFreeEx(hProc, remoteMem, UIntPtr.Zero, MEM_RELEASE);
            if (wait != 0) return 0xFFFFFFFE;   // timed out; module may still load
            return exitCode;                    // hModule of the loaded dll (0 = LoadLibrary failed)
        }
        finally { CloseHandle(hProc); }
    }
}
'@ -ReferencedAssemblies 'System.dll'
}

$code = [Injector]::Inject($ProcessId, $full)
if ($code -eq 0) { throw "LoadLibraryW returned NULL (injection failed for pid $ProcessId)" }
if ($code -eq 0xFFFFFFFE) { Write-Host "injected (thread still running after 20s, hModule unknown)" }
else { Write-Host ("injected {0} into pid {1}; hModule=0x{2:X8}" -f (Split-Path $full -Leaf), $ProcessId, $code) }
