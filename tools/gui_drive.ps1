# Drive the installer window without touching the user's screen: set the folder, press Install,
# capture the window (PrintWindow) at points in time.  powershell -File gui_drive.ps1 <exe> <gamedir> <outdir>
param([string]$Exe, [string]$GameDir, [string]$Out)
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System; using System.Text; using System.Collections.Generic; using System.Runtime.InteropServices;
public class G {
  public delegate bool P(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern IntPtr FindWindow(string c, string n);
  [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr h, P p, IntPtr l);
  [DllImport("user32.dll")] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr SendMessage(IntPtr h, int m, IntPtr w, string l);
  [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h, int m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, int m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint f);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr a, int x, int y, int cx, int cy, uint f);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  public static List<IntPtr> Kids(IntPtr w) { var r = new List<IntPtr>(); EnumChildWindows(w, (h, l) => { r.Add(h); return true; }, IntPtr.Zero); return r; }
  public static string Cls(IntPtr h) { var s = new StringBuilder(64); GetClassName(h, s, 64); return s.ToString(); }
  public static string Txt(IntPtr h) { var s = new StringBuilder(512); GetWindowText(h, s, 512); return s.ToString(); }
}
'@
function Shot($w, $name) {
  $r = New-Object G+RECT; [G]::GetWindowRect($w, [ref]$r) | Out-Null
  $bmp = New-Object Drawing.Bitmap ($r.R - $r.L), ($r.B - $r.T)
  $g = [Drawing.Graphics]::FromImage($bmp); $dc = $g.GetHdc()
  [G]::PrintWindow($w, $dc, 2) | Out-Null; $g.ReleaseHdc($dc); $g.Dispose()
  $bmp.Save("$Out\$name.png"); $bmp.Dispose()
}
New-Item -ItemType Directory -Force $Out | Out-Null
$env:F3R_TEST = "1"
$proc = Start-Process $Exe -PassThru
$w = [IntPtr]::Zero
for ($i = 0; $i -lt 150 -and $w -eq [IntPtr]::Zero; $i++) { Start-Sleep -Milliseconds 200; $proc.Refresh(); $w = $proc.MainWindowHandle }
if ($w -eq [IntPtr]::Zero) { "window not found"; Stop-Process $proc -Force; exit 1 }
# keep it off the visible desktop
[G]::SetWindowPos($w, [IntPtr]::Zero, -4000, 100, 0, 0, 0x0001 -bor 0x0004 -bor 0x0010) | Out-Null
Start-Sleep -Milliseconds 500
$kids = [G]::Kids($w)
"detected folder: " + ([G]::Txt(($kids | Where-Object { [G]::Cls($_) -eq "Edit" })[0]))
Shot $w "1_start"
$edit = ($kids | Where-Object { [G]::Cls($_) -eq "Edit" })[0]
[G]::SendMessage($edit, 0x000C, [IntPtr]::Zero, $GameDir) | Out-Null      # WM_SETTEXT
Start-Sleep -Milliseconds 300
"found label: " + ([G]::Txt(($kids | Where-Object { [G]::Txt($_) -like "*found*" -or [G]::Txt($_) -like "*not in this*" })[0]))
Shot $w "2_folder_set"
$btn = $kids | Where-Object { [G]::Txt($_) -eq "Install" }
[G]::PostMessage($btn, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null  # BM_CLICK
for ($t = 1; $t -le 60; $t++) {
  Start-Sleep -Seconds 1
  if ($t -eq 2 -or $t -eq 6) { Shot $w ("3_working_" + $t) }
  if ([G]::Txt($btn) -eq "Close") { break }
}
Shot $w "4_done"
"status: " + (($kids | ForEach-Object { [G]::Txt($_) }) -join " | ")
[G]::PostMessage($btn, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null  # Close
Start-Sleep -Seconds 1
"exited: " + $proc.HasExited
