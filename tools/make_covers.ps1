param([string]$List, [string]$One, [string]$Out, [switch]$Crop, [switch]$Preview)
# Cassette covers -> .pm4 for the menu: a PMODE 4 screen (256x192, black and buff with
# CSS 1), 6144 bytes, top line first, 8 pixels a byte, leftmost in bit 7. Floyd-Steinberg
# dithered from grey.
#
# Needs Windows PowerShell 5.1 (powershell.exe), which has System.Drawing:
#   powershell -ExecutionPolicy Bypass -File make_covers.ps1 -List covers.txt
#       covers.txt (from organise_sd.py) has lines "crop|picture|out.pm4". crop 1 means the
#       picture is a whole inlay and the front (the right hand panel) is cut out of it.
#   powershell -ExecutionPolicy Bypass -File make_covers.ps1 -One cover.jpg -Out Game.pm4 [-Crop] [-Preview]
#       for one cover, eg. to put in a better picture by hand. -Preview also writes Game.png.
Add-Type -AssemblyName System.Drawing
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @"
using System; using System.Drawing; using System.Drawing.Drawing2D;
public static class Pm4 {
  // The front of a cassette inlay is about 65mm wide and 100mm high
  const double FrontAspect = 0.66;
  public static byte[] Convert(string path, bool crop, string png) {
    using (var src = new Bitmap(path)) using (var dst = new Bitmap(256, 192)) {
      Rectangle r = new Rectangle(0, 0, src.Width, src.Height);
      if (crop && src.Width > src.Height * 0.8) {
        int w = (int)(src.Height * FrontAspect);
        r = new Rectangle(src.Width - w, 0, w, src.Height);
      }
      using (var g = Graphics.FromImage(dst)) {
        g.Clear(Color.Black);
        g.InterpolationMode = InterpolationMode.HighQualityBicubic;
        double s = Math.Min(256.0 / r.Width, 192.0 / r.Height);
        int dw = (int)(r.Width * s), dh = (int)(r.Height * s);
        g.DrawImage(src, new Rectangle((256 - dw) / 2, (192 - dh) / 2, dw, dh), r, GraphicsUnit.Pixel);
      }
      var a = new float[192, 256];
      for (int y = 0; y < 192; y++) for (int x = 0; x < 256; x++) {
        var c = dst.GetPixel(x, y); a[y, x] = 0.3f * c.R + 0.59f * c.G + 0.11f * c.B;
      }
      var b = new byte[6144];
      for (int y = 0; y < 192; y++) for (int x = 0; x < 256; x++) {
        bool on = a[y, x] >= 128;
        float e = a[y, x] - (on ? 255 : 0);
        if (on) b[y * 32 + x / 8] |= (byte)(0x80 >> (x % 8));
        if (x + 1 < 256) a[y, x + 1] += e * 7 / 16;
        if (y + 1 < 192) {
          if (x > 0) a[y + 1, x - 1] += e * 3 / 16;
          a[y + 1, x] += e * 5 / 16;
          if (x + 1 < 256) a[y + 1, x + 1] += e / 16;
        }
      }
      if (!string.IsNullOrEmpty(png)) {		// PowerShell passes "" for `$null
        using (var p = new Bitmap(256, 192)) {
          for (int y = 0; y < 192; y++) for (int x = 0; x < 256; x++)
            p.SetPixel(x, y, (b[y * 32 + x / 8] & (0x80 >> (x % 8))) != 0 ? Color.FromArgb(255, 255, 240) : Color.Black);
          p.Save(png, System.Drawing.Imaging.ImageFormat.Png);
        }
      }
      return b;
    }
  }
}
"@

if ($One) {
  $png = $null; if ($Preview) { $png = [IO.Path]::ChangeExtension($Out, ".png") }
  [IO.File]::WriteAllBytes($Out, [Pm4]::Convert($One, [bool]$Crop, $png))
  exit 0
}
$ok = 0; $bad = 0
foreach ($line in [IO.File]::ReadAllLines($List)) {
  if (-not $line) { continue }
  $p = $line.Split('|')
  try {
    [IO.File]::WriteAllBytes($p[2], [Pm4]::Convert($p[1], $p[0] -eq '1', $null))
    $ok++
  } catch {
    Write-Output "failed: $($p[1]): $($_.Exception.Message)"
    $bad++
  }
}
Write-Output "covers made $ok, failed $bad"
