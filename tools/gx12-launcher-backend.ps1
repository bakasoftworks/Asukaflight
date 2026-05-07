param(
    [switch]$SelfTest
)

Set-StrictMode -Version 3.0
$ErrorActionPreference = 'Stop'

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

if (-not ('Gx12Launcher.StickShapeEditor' -as [type])) {
Add-Type -ReferencedAssemblies @('System.Windows.Forms', 'System.Drawing') -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Globalization;
using System.Windows.Forms;

namespace Gx12Launcher {
    public class StickShapeEditor : Panel {
        public class ShapeNode {
            public double X;
            public double Y;
            public double Width;
            public ShapeNode(double x, double y, double width) {
                X = x;
                Y = y;
                Width = width;
            }
        }

        public const double MinWidth = 0.05;
        public const double MaxWidth = 1.0;
        public const double DefaultWidth = 0.25;

        private readonly List<ShapeNode> nodes = new List<ShapeNode>();
        private bool shapingEnabled = true;
        private bool useDarkTheme = false;
        private Color curveColor = Color.FromArgb(26, 115, 232);
        private Color disabledColor = Color.FromArgb(180, 188, 200);
        private int draggingIndex = -1;
        private int hoverIndex = -1;
        private bool isDragging;
        private string hint = "Click to add a node. Drag to move. Scroll to widen.\nRight-click to remove.";

        public event EventHandler NodesChanged;

        public StickShapeEditor() {
            SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.UserPaint | ControlStyles.OptimizedDoubleBuffer | ControlStyles.ResizeRedraw, true);
            BackColor = Color.White;
            BorderStyle = BorderStyle.FixedSingle;
            DoubleBuffered = true;
            TabStop = false;
        }

        public bool ShapingEnabled {
            get { return shapingEnabled; }
            set { shapingEnabled = value; Invalidate(); }
        }

        public bool UseDarkTheme {
            get { return useDarkTheme; }
            set {
                useDarkTheme = value;
                BackColor = useDarkTheme ? Color.FromArgb(17, 21, 29) : Color.White;
                Invalidate();
            }
        }

        public Color CurveColor {
            get { return curveColor; }
            set { curveColor = value; Invalidate(); }
        }

        public string Hint {
            get { return hint; }
            set { hint = value ?? string.Empty; Invalidate(); }
        }

        public int NodeCount { get { return nodes.Count; } }

        private static double Clamp01(double v) {
            if (v < 0.0) return 0.0;
            if (v > 1.0) return 1.0;
            return v;
        }

        private static double ClampWidth(double v) {
            if (v < MinWidth) return MinWidth;
            if (v > MaxWidth) return MaxWidth;
            return v;
        }

        public void ClearNodes() {
            nodes.Clear();
            draggingIndex = -1;
            hoverIndex = -1;
            Invalidate();
            RaiseNodesChanged();
        }

        public void SetNodes(double[] xs, double[] ys, double[] widths) {
            nodes.Clear();
            int count = 0;
            if (xs != null && ys != null && widths != null) {
                count = Math.Min(Math.Min(xs.Length, ys.Length), widths.Length);
            }
            for (int i = 0; i < count; i++) {
                nodes.Add(new ShapeNode(Clamp01(xs[i]), Clamp01(ys[i]), ClampWidth(widths[i])));
            }
            draggingIndex = -1;
            hoverIndex = -1;
            Invalidate();
            RaiseNodesChanged();
        }

        public double[] GetXs() {
            double[] r = new double[nodes.Count];
            for (int i = 0; i < nodes.Count; i++) r[i] = nodes[i].X;
            return r;
        }

        public double[] GetYs() {
            double[] r = new double[nodes.Count];
            for (int i = 0; i < nodes.Count; i++) r[i] = nodes[i].Y;
            return r;
        }

        public double[] GetWidths() {
            double[] r = new double[nodes.Count];
            for (int i = 0; i < nodes.Count; i++) r[i] = nodes[i].Width;
            return r;
        }

        public string SaveToTomlValue() {
            if (nodes.Count == 0) return "[]";
            var sb = new System.Text.StringBuilder();
            sb.Append('[');
            for (int i = 0; i < nodes.Count; i++) {
                if (i > 0) sb.Append(", ");
                sb.AppendFormat(CultureInfo.InvariantCulture, "[{0:0.###},{1:0.###},{2:0.###}]",
                    nodes[i].X, nodes[i].Y, nodes[i].Width);
            }
            sb.Append(']');
            return sb.ToString();
        }

        public void LoadFromTomlValue(string text) {
            nodes.Clear();
            if (!string.IsNullOrWhiteSpace(text)) {
                var rx = new System.Text.RegularExpressions.Regex(@"\[\s*([0-9]*\.?[0-9]+)\s*,\s*([0-9]*\.?[0-9]+)\s*,\s*([0-9]*\.?[0-9]+)\s*\]");
                var matches = rx.Matches(text);
                foreach (System.Text.RegularExpressions.Match m in matches) {
                    double x, y, w;
                    if (double.TryParse(m.Groups[1].Value, NumberStyles.Float, CultureInfo.InvariantCulture, out x) &&
                        double.TryParse(m.Groups[2].Value, NumberStyles.Float, CultureInfo.InvariantCulture, out y) &&
                        double.TryParse(m.Groups[3].Value, NumberStyles.Float, CultureInfo.InvariantCulture, out w)) {
                        nodes.Add(new ShapeNode(Clamp01(x), Clamp01(y), ClampWidth(w)));
                    }
                }
            }
            draggingIndex = -1;
            hoverIndex = -1;
            Invalidate();
            RaiseNodesChanged();
        }

        public double EvaluateCurve(double t) {
            if (t < 0.0) t = 0.0;
            if (t > 1.0) t = 1.0;
            if (nodes.Count == 0) return t;
            double sumK = 0.0, sumKy = 0.0, maxK = 0.0;
            for (int i = 0; i < nodes.Count; i++) {
                ShapeNode node = nodes[i];
                double w = ClampWidth(node.Width);
                double dx = t - node.X;
                if (dx < 0.0) dx = -dx;
                if (dx >= w) continue;
                double k = 0.5 * (1.0 + Math.Cos((Math.PI * dx) / w));
                double yc = Clamp01(node.Y);
                sumK += k;
                sumKy += k * yc;
                if (k > maxK) maxK = k;
            }
            if (sumK <= 0.0) return t;
            double weighted = sumKy / sumK;
            double blend = maxK;
            if (blend < 0.0) blend = 0.0;
            if (blend > 1.0) blend = 1.0;
            double v = (blend * weighted) + ((1.0 - blend) * t);
            return Clamp01(v);
        }

        private RectangleF GetPlot() {
            int padLeft = 8;
            int padRight = 8;
            int padTop = 8;
            int padBottom = 8;
            int w = Math.Max(1, Width - padLeft - padRight);
            int h = Math.Max(1, Height - padTop - padBottom);
            return new RectangleF(padLeft, padTop, w, h);
        }

        private PointF NodeToScreen(ShapeNode n, RectangleF plot) {
            float sx = plot.Left + (float)(n.X * plot.Width);
            float sy = plot.Bottom - (float)(n.Y * plot.Height);
            return new PointF(sx, sy);
        }

        private double ScreenXToNorm(int sx, RectangleF plot) {
            double v = (sx - plot.Left) / Math.Max(1.0, plot.Width);
            return Clamp01(v);
        }

        private double ScreenYToNorm(int sy, RectangleF plot) {
            double v = (plot.Bottom - sy) / Math.Max(1.0, plot.Height);
            return Clamp01(v);
        }

        private int FindNodeAt(int sx, int sy, int radiusPx) {
            RectangleF plot = GetPlot();
            for (int i = nodes.Count - 1; i >= 0; i--) {
                PointF p = NodeToScreen(nodes[i], plot);
                float dxp = p.X - sx;
                float dyp = p.Y - sy;
                if ((dxp * dxp) + (dyp * dyp) <= (radiusPx * radiusPx)) return i;
            }
            return -1;
        }

        private void RaiseNodesChanged() {
            EventHandler h = NodesChanged;
            if (h != null) h(this, EventArgs.Empty);
        }

        private Color ThemeColor(Color light, Color dark) {
            return useDarkTheme ? dark : light;
        }

        protected override void OnPaint(PaintEventArgs e) {
            base.OnPaint(e);
            Graphics g = e.Graphics;
            g.SmoothingMode = SmoothingMode.AntiAlias;
            RectangleF plot = GetPlot();

            using (SolidBrush backBrush = new SolidBrush(ThemeColor(Color.White, Color.FromArgb(17, 21, 29)))) {
                g.FillRectangle(backBrush, ClientRectangle);
            }

            using (Pen gridPen = new Pen(ThemeColor(Color.FromArgb(232, 237, 245), Color.FromArgb(38, 45, 58)), 1)) {
                for (int i = 1; i < 4; i++) {
                    float gx = plot.Left + (plot.Width * i / 4f);
                    g.DrawLine(gridPen, gx, plot.Top, gx, plot.Bottom);
                    float gy = plot.Top + (plot.Height * i / 4f);
                    g.DrawLine(gridPen, plot.Left, gy, plot.Right, gy);
                }
            }

            using (Pen axisPen = new Pen(ThemeColor(Color.FromArgb(180, 190, 200), Color.FromArgb(75, 87, 106)), 1)) {
                g.DrawLine(axisPen, plot.Left, plot.Bottom, plot.Right, plot.Bottom);
                g.DrawLine(axisPen, plot.Left, plot.Top, plot.Left, plot.Bottom);
            }

            using (Pen baselinePen = new Pen(ThemeColor(Color.FromArgb(170, 178, 188), Color.FromArgb(97, 111, 132)), 1)) {
                baselinePen.DashStyle = DashStyle.Dash;
                g.DrawLine(baselinePen, plot.Left, plot.Bottom, plot.Right, plot.Top);
            }

            Color drawColor = shapingEnabled ? curveColor : ThemeColor(disabledColor, Color.FromArgb(93, 105, 124));
            using (Pen curvePen = new Pen(drawColor, 2)) {
                int samples = Math.Max(160, Math.Min(1600, (int)(plot.Width * 2)));
                PointF prev = new PointF(plot.Left, plot.Bottom);
                for (int i = 0; i <= samples; i++) {
                    double t = (double)i / samples;
                    double y = EvaluateCurve(t);
                    float sx = plot.Left + (float)(t * plot.Width);
                    float sy = plot.Bottom - (float)(y * plot.Height);
                    PointF cur = new PointF(sx, sy);
                    if (i > 0) g.DrawLine(curvePen, prev, cur);
                    prev = cur;
                }
            }

            for (int i = 0; i < nodes.Count; i++) {
                ShapeNode node = nodes[i];
                PointF center = NodeToScreen(node, plot);
                float wPx = (float)(ClampWidth(node.Width) * plot.Width);
                using (SolidBrush widthBrush = new SolidBrush(Color.FromArgb(useDarkTheme ? 70 : 36, drawColor))) {
                    g.FillRectangle(widthBrush, center.X - wPx, plot.Bottom - 6, wPx * 2, 4);
                }
                bool active = (i == hoverIndex) || (i == draggingIndex);
                float r = active ? 7f : 5f;
                using (SolidBrush dotBrush = new SolidBrush(ThemeColor(Color.White, Color.FromArgb(17, 21, 29))))
                using (Pen dotPen = new Pen(drawColor, 2)) {
                    g.FillEllipse(dotBrush, center.X - r, center.Y - r, r * 2, r * 2);
                    g.DrawEllipse(dotPen, center.X - r, center.Y - r, r * 2, r * 2);
                }
            }

            if (nodes.Count == 0 && !string.IsNullOrEmpty(hint)) {
                using (SolidBrush textBrush = new SolidBrush(ThemeColor(Color.FromArgb(140, 148, 160), Color.FromArgb(123, 139, 162))))
                using (Font f = new Font("Segoe UI", 7.75f)) {
                    g.DrawString(hint, f, textBrush, plot.Left + 4, plot.Top + 2);
                }
            }
        }

        protected override void OnMouseEnter(EventArgs e) {
            base.OnMouseEnter(e);
            if (!Focused) {
                Focus();
            }
        }

        protected override void OnMouseDown(MouseEventArgs e) {
            base.OnMouseDown(e);
            if (e.Button == MouseButtons.Right) {
                int idx = FindNodeAt(e.X, e.Y, 12);
                if (idx >= 0) {
                    nodes.RemoveAt(idx);
                    draggingIndex = -1;
                    hoverIndex = -1;
                    Invalidate();
                    RaiseNodesChanged();
                }
                return;
            }
            if (e.Button == MouseButtons.Left) {
                int idx = FindNodeAt(e.X, e.Y, 12);
                if (idx < 0) {
                    RectangleF plot = GetPlot();
                    double nx = ScreenXToNorm(e.X, plot);
                    double ny = ScreenYToNorm(e.Y, plot);
                    nodes.Add(new ShapeNode(nx, ny, DefaultWidth));
                    idx = nodes.Count - 1;
                }
                draggingIndex = idx;
                hoverIndex = idx;
                isDragging = true;
                Capture = true;
                Invalidate();
                RaiseNodesChanged();
            }
        }

        protected override void OnMouseMove(MouseEventArgs e) {
            base.OnMouseMove(e);
            if (isDragging && draggingIndex >= 0 && draggingIndex < nodes.Count) {
                RectangleF plot = GetPlot();
                ShapeNode n = nodes[draggingIndex];
                n.X = ScreenXToNorm(e.X, plot);
                n.Y = ScreenYToNorm(e.Y, plot);
                Invalidate();
                RaiseNodesChanged();
            } else {
                int idx = FindNodeAt(e.X, e.Y, 10);
                if (idx != hoverIndex) {
                    hoverIndex = idx;
                    Cursor = idx >= 0 ? Cursors.Hand : Cursors.Cross;
                    Invalidate();
                }
            }
        }

        protected override void OnMouseUp(MouseEventArgs e) {
            base.OnMouseUp(e);
            isDragging = false;
            draggingIndex = -1;
            Capture = false;
            int idx = FindNodeAt(e.X, e.Y, 10);
            if (idx != hoverIndex) {
                hoverIndex = idx;
                Invalidate();
            }
        }

        protected override void OnMouseLeave(EventArgs e) {
            base.OnMouseLeave(e);
            if (!isDragging) {
                hoverIndex = -1;
                Cursor = Cursors.Default;
                Invalidate();
            }
        }

        protected override void OnMouseWheel(MouseEventArgs e) {
            int idx = FindNodeAt(e.X, e.Y, 32);
            if (idx < 0) idx = hoverIndex;
            if (idx < 0 && nodes.Count == 1) idx = 0;
            if (idx >= 0 && idx < nodes.Count) {
                double delta = e.Delta > 0 ? 0.02 : -0.02;
                nodes[idx].Width = ClampWidth(nodes[idx].Width + delta);
                Invalidate();
                RaiseNodesChanged();
            }
        }

        protected override bool IsInputKey(Keys keyData) {
            if (keyData == Keys.Delete || keyData == Keys.Back) return true;
            return base.IsInputKey(keyData);
        }

        protected override void OnKeyDown(KeyEventArgs e) {
            base.OnKeyDown(e);
            if ((e.KeyCode == Keys.Delete || e.KeyCode == Keys.Back) && hoverIndex >= 0 && hoverIndex < nodes.Count) {
                nodes.RemoveAt(hoverIndex);
                hoverIndex = -1;
                draggingIndex = -1;
                Invalidate();
                RaiseNodesChanged();
                e.Handled = true;
            }
        }
    }
}
'@
}

$root = Split-Path -Parent (Split-Path -Parent $PSCommandPath)
$exePath = Join-Path $root 'runtime\gx12mouse.exe'
$profileDirectoryPath = Join-Path $root '.gx12-profile-dir'
$defaultProfilePath = Join-Path $root '.gx12-default-profile'
$script:launcherStopEventName = 'Local\GX12MouseLauncherStop'
$script:activeConsoleProcess = $null
$script:loadingProfile = $false
$script:editingProfile = $null
$script:tuningFields = @{}
$script:tuningChecks = @{}
$script:tuningLabels = @{}
$script:toolTip = New-Object System.Windows.Forms.ToolTip
$script:toolTip.AutoPopDelay = 12000
$script:toolTip.InitialDelay = 350
$script:toolTip.ReshowDelay = 100

function Resolve-ProfileDirectoryPath {
    param([string]$Path)

    $value = [Environment]::ExpandEnvironmentVariables($Path.Trim().Trim('"'))
    if ([string]::IsNullOrWhiteSpace($value)) {
        return (Join-Path $root 'profiles')
    }
    if (-not [System.IO.Path]::IsPathRooted($value)) {
        $value = Join-Path $root $value
    }
    return [System.IO.Path]::GetFullPath($value)
}

function Get-ProfileDirectory {
    if (Test-Path -LiteralPath $profileDirectoryPath) {
        $value = (Get-Content -LiteralPath $profileDirectoryPath -ErrorAction SilentlyContinue | Select-Object -First 1)
        if (-not [string]::IsNullOrWhiteSpace($value)) {
            return (Resolve-ProfileDirectoryPath -Path $value)
        }
    }
    return (Resolve-ProfileDirectoryPath -Path (Join-Path $root 'profiles'))
}

function Set-ProfileDirectory {
    param([string]$Path)

    $resolved = Resolve-ProfileDirectoryPath -Path $Path
    if (-not (Test-Path -LiteralPath $resolved)) {
        New-Item -ItemType Directory -Path $resolved -Force | Out-Null
    }
    Set-Content -LiteralPath $profileDirectoryPath -Value $resolved -Encoding ASCII
    Set-Variable -Name profilesDir -Value $resolved -Scope Script
    return $resolved
}

function Open-ProfileDirectory {
    if (Test-Path -LiteralPath $profilesDir) {
        Start-Process -FilePath 'explorer.exe' -ArgumentList ('"{0}"' -f $profilesDir)
    }
}

$profilesDir = Get-ProfileDirectory

function Get-DefaultProfileFileName {
    if (Test-Path -LiteralPath $defaultProfilePath) {
        $value = (Get-Content -LiteralPath $defaultProfilePath -ErrorAction SilentlyContinue | Select-Object -First 1)
        if (-not [string]::IsNullOrWhiteSpace($value)) {
            return [System.IO.Path]::GetFileName($value.Trim())
        }
    }
    return 'whoop-fast.toml'
}

function Set-DefaultProfileFileName {
    param([string]$FileName)
    Set-Content -LiteralPath $defaultProfilePath -Value ([System.IO.Path]::GetFileName($FileName)) -Encoding ASCII
}

function Get-LauncherStopEvent {
    New-Object System.Threading.EventWaitHandle(
        $false,
        [System.Threading.EventResetMode]::ManualReset,
        $script:launcherStopEventName
    )
}

function Get-ManagedGx12Processes {
    @(Get-CimInstance Win32_Process -Filter "Name = 'gx12mouse.exe'" -ErrorAction SilentlyContinue |
        Where-Object {
            $_.CommandLine -and
            $_.CommandLine.Contains($root) -and
            $_.CommandLine.Contains('--trainer-profile')
        })
}

function Stop-ActiveGx12Run {
    param([int]$TimeoutMs = 3000)

    $stopEvent = Get-LauncherStopEvent
    try {
        $stopEvent.Set() | Out-Null

        $deadline = [DateTime]::UtcNow.AddMilliseconds($TimeoutMs)
        do {
            $running = @(Get-ManagedGx12Processes)
            if ($running.Count -eq 0) {
                break
            }
            Start-Sleep -Milliseconds 100
        } while ([DateTime]::UtcNow -lt $deadline)
    } finally {
        $stopEvent.Dispose()
    }

    if ($null -ne $script:activeConsoleProcess) {
        try {
            $script:activeConsoleProcess.Refresh()
            if (-not $script:activeConsoleProcess.HasExited) {
                Stop-Process -Id $script:activeConsoleProcess.Id -Force -ErrorAction SilentlyContinue
            }
        } catch {
        }
        $script:activeConsoleProcess = $null
    }
}

function Reset-LauncherStopEvent {
    $stopEvent = Get-LauncherStopEvent
    try {
        $stopEvent.Reset() | Out-Null
    } finally {
        $stopEvent.Dispose()
    }
}

function Get-TomlValue {
    param(
        [string[]]$Lines,
        [string]$Section,
        [string]$Key,
        [string]$Default = ''
    )

    $inSection = $false
    foreach ($line in $Lines) {
        $trimmed = $line.Trim()
        if ($trimmed -match '^\[(.+)\]$') {
            $inSection = ($Matches[1] -eq $Section)
            continue
        }
        if (-not $inSection) {
            continue
        }
        if ($trimmed -match ('^{0}\s*=\s*(.+)$' -f [regex]::Escape($Key))) {
            return $Matches[1].Trim().Trim('"')
        }
    }

    return $Default
}

function Get-TomlArrayValue {
    param(
        [string[]]$Lines,
        [string]$Section,
        [string]$Key
    )

    $inSection = $false
    $collecting = $false
    $depth = 0
    $accum = New-Object System.Text.StringBuilder
    foreach ($line in $Lines) {
        $trimmed = $line.Trim()
        if (-not $collecting) {
            if ($trimmed -match '^\[(.+)\]$') {
                $inSection = ($Matches[1] -eq $Section)
                continue
            }
            if (-not $inSection) {
                continue
            }
            if ($trimmed -match ('^{0}\s*=\s*(.+)$' -f [regex]::Escape($Key))) {
                $rest = $Matches[1].Trim()
                $idx = $rest.IndexOf('[')
                if ($idx -lt 0) {
                    return ''
                }
                $collecting = $true
                $rest = $rest.Substring($idx)
                foreach ($ch in $rest.ToCharArray()) {
                    [void]$accum.Append($ch)
                    if ($ch -eq '[') { $depth++ }
                    elseif ($ch -eq ']') {
                        $depth--
                        if ($depth -le 0) { return $accum.ToString() }
                    }
                }
                continue
            }
        } else {
            foreach ($ch in $line.ToCharArray()) {
                [void]$accum.Append($ch)
                if ($ch -eq '[') { $depth++ }
                elseif ($ch -eq ']') {
                    $depth--
                    if ($depth -le 0) { return $accum.ToString() }
                }
            }
            [void]$accum.Append("`n")
        }
    }
    return ''
}

function ConvertFrom-StickShapeNodesText {
    param([string]$Text)

    $out = New-Object 'System.Collections.Generic.List[object]'
    if ([string]::IsNullOrWhiteSpace($Text)) { return ,@($out.ToArray()) }
    $rx = [regex]::new('\[\s*(?<x>[0-9]*\.?[0-9]+)\s*,\s*(?<y>[0-9]*\.?[0-9]+)\s*,\s*(?<w>[0-9]*\.?[0-9]+)\s*\]')
    $culture = [System.Globalization.CultureInfo]::InvariantCulture
    foreach ($m in $rx.Matches($Text)) {
        $x = 0.0; $y = 0.0; $w = 0.25
        [void][double]::TryParse($m.Groups['x'].Value, [System.Globalization.NumberStyles]::Float, $culture, [ref]$x)
        [void][double]::TryParse($m.Groups['y'].Value, [System.Globalization.NumberStyles]::Float, $culture, [ref]$y)
        [void][double]::TryParse($m.Groups['w'].Value, [System.Globalization.NumberStyles]::Float, $culture, [ref]$w)
        if ($x -lt 0.0) { $x = 0.0 } elseif ($x -gt 1.0) { $x = 1.0 }
        if ($y -lt 0.0) { $y = 0.0 } elseif ($y -gt 1.0) { $y = 1.0 }
        if ($w -lt 0.05) { $w = 0.05 } elseif ($w -gt 1.0) { $w = 1.0 }
        $out.Add([pscustomobject]@{ X = $x; Y = $y; Width = $w })
    }
    return ,@($out.ToArray())
}

function ConvertTo-StickShapeNodesText {
    param([object[]]$Nodes)

    if ($null -eq $Nodes -or $Nodes.Count -eq 0) { return '[]' }
    $culture = [System.Globalization.CultureInfo]::InvariantCulture
    $parts = New-Object System.Collections.Generic.List[string]
    foreach ($node in $Nodes) {
        $parts.Add(([string]::Format($culture, '[{0:0.###},{1:0.###},{2:0.###}]', [double]$node.X, [double]$node.Y, [double]$node.Width)))
    }
    return '[' + ($parts -join ', ') + ']'
}

function Set-TomlArrayValue {
    param(
        [string[]]$Lines,
        [string]$Section,
        [string]$Key,
        [string]$Value
    )

    $result = New-Object System.Collections.Generic.List[string]
    $inSection = $false
    $sectionSeen = $false
    $keyWritten = $false
    $skipping = $false
    $skipDepth = 0

    foreach ($line in $Lines) {
        if ($skipping) {
            foreach ($ch in $line.ToCharArray()) {
                if ($ch -eq '[') { $skipDepth++ }
                elseif ($ch -eq ']') {
                    $skipDepth--
                    if ($skipDepth -le 0) { $skipping = $false; break }
                }
            }
            continue
        }
        $trimmed = $line.Trim()
        if ($trimmed -match '^\[(.+)\]$') {
            if ($inSection -and -not $keyWritten) {
                $result.Add(('{0} = {1}' -f $Key, $Value))
                $keyWritten = $true
            }
            $inSection = ($Matches[1] -eq $Section)
            if ($inSection) { $sectionSeen = $true }
            $result.Add($line)
            continue
        }
        if ($inSection -and $trimmed -match ('^{0}\s*=\s*(.*)$' -f [regex]::Escape($Key))) {
            $rest = $Matches[1]
            $prefix = ''
            if ($line -match '^(\s*)') { $prefix = $Matches[1] }
            $result.Add(('{0}{1} = {2}' -f $prefix, $Key, $Value))
            $keyWritten = $true
            $idx = $rest.IndexOf('[')
            if ($idx -ge 0) {
                $skipDepth = 0
                foreach ($ch in $rest.Substring($idx).ToCharArray()) {
                    if ($ch -eq '[') { $skipDepth++ }
                    elseif ($ch -eq ']') {
                        $skipDepth--
                        if ($skipDepth -le 0) { break }
                    }
                }
                if ($skipDepth -gt 0) { $skipping = $true }
            }
            continue
        }
        $result.Add($line)
    }

    if (-not $sectionSeen) {
        if ($result.Count -gt 0 -and $result[$result.Count - 1].Trim().Length -ne 0) {
            $result.Add('')
        }
        $result.Add(('[{0}]' -f $Section))
        $result.Add(('{0} = {1}' -f $Key, $Value))
    } elseif ($inSection -and -not $keyWritten) {
        $result.Add(('{0} = {1}' -f $Key, $Value))
    }

    return $result.ToArray()
}

function Get-ProfileInfo {
    param([System.IO.FileInfo]$File)

    $lines = Get-Content -LiteralPath $File.FullName
    $name = Get-TomlValue -Lines $lines -Section 'trainer' -Key 'name' -Default $File.BaseName
    $frameRate = Get-TomlValue -Lines $lines -Section 'trainer' -Key 'frame_rate_hz' -Default '1000'
    $stopKey = Get-TomlValue -Lines $lines -Section 'safety' -Key 'stop_key' -Default 'Esc'
    $freezeKey = Get-TomlValue -Lines $lines -Section 'safety' -Key 'freeze_key' -Default 'F2'
    $rollGain = Get-TomlValue -Lines $lines -Section 'mapper' -Key 'roll_gain' -Default ''
    $pitchGain = Get-TomlValue -Lines $lines -Section 'mapper' -Key 'pitch_gain' -Default ''
    $maxOutput = Get-TomlValue -Lines $lines -Section 'mapper' -Key 'max_output' -Default ''
    $deadband = Get-TomlValue -Lines $lines -Section 'mapper' -Key 'deadband' -Default ''
    $expo = Get-TomlValue -Lines $lines -Section 'mapper' -Key 'expo' -Default ''
    $smoothing = Get-TomlValue -Lines $lines -Section 'mapper' -Key 'smoothing' -Default ''
    $inputFilterDefault = if (($smoothing -as [double]) -gt 0.0) { '"smoothing"' } else { '"off"' }
    $inputFilter = Get-TomlValue -Lines $lines -Section 'mapper' -Key 'input_filter' -Default $inputFilterDefault
    $oneEuroMinCutoffHz = Get-TomlValue -Lines $lines -Section 'mapper' -Key 'one_euro_min_cutoff_hz' -Default '1.0'
    $oneEuroBeta = Get-TomlValue -Lines $lines -Section 'mapper' -Key 'one_euro_beta' -Default '0.05'
    $oneEuroDcutoffHz = Get-TomlValue -Lines $lines -Section 'mapper' -Key 'one_euro_dcutoff_hz' -Default '1.0'
    $despikeEnabled = Get-TomlValue -Lines $lines -Section 'mapper' -Key 'despike_enabled' -Default 'false'
    $despikeCountEnabled = Get-TomlValue -Lines $lines -Section 'mapper' -Key 'despike_count_enabled' -Default 'false'
    $despikeWindow = Get-TomlValue -Lines $lines -Section 'mapper' -Key 'despike_window' -Default '5'
    $despikeThresholdSigma = Get-TomlValue -Lines $lines -Section 'mapper' -Key 'despike_threshold_sigma' -Default '3.0'
    $outputShapingEnabled = Get-TomlValue -Lines $lines -Section 'mapper' -Key 'output_shaping_enabled' -Default 'false'
    $outputCurveDefault = if ($outputShapingEnabled -eq 'true') { '"nodes"' } else { '"expo"' }
    $outputCurve = Get-TomlValue -Lines $lines -Section 'mapper' -Key 'output_curve' -Default $outputCurveDefault
    $actualCenter = Get-TomlValue -Lines $lines -Section 'mapper' -Key 'actual_center' -Default '0.45'
    $actualMax = Get-TomlValue -Lines $lines -Section 'mapper' -Key 'actual_max' -Default '1.0'
    $actualExpo = Get-TomlValue -Lines $lines -Section 'mapper' -Key 'actual_expo' -Default '0.30'
    $positionModel = Get-TomlValue -Lines $lines -Section 'mapper' -Key 'position_model' -Default '"integrator"'
    $gimbalFrequencyHz = Get-TomlValue -Lines $lines -Section 'mapper' -Key 'gimbal_frequency_hz' -Default '5.0'
    $gimbalDampingRatio = Get-TomlValue -Lines $lines -Section 'mapper' -Key 'gimbal_damping_ratio' -Default '1.15'
    $gimbalInputImpulse = Get-TomlValue -Lines $lines -Section 'mapper' -Key 'gimbal_input_impulse' -Default '1.0'
    $gimbalStaticFriction = Get-TomlValue -Lines $lines -Section 'mapper' -Key 'gimbal_static_friction' -Default '0.0'
    $gimbalDynamicFriction = Get-TomlValue -Lines $lines -Section 'mapper' -Key 'gimbal_dynamic_friction' -Default '0.0'
    $gimbalEdgeBumper = Get-TomlValue -Lines $lines -Section 'mapper' -Key 'gimbal_edge_bumper' -Default '0.0'
    $gimbalAntiwindupEnabled = Get-TomlValue -Lines $lines -Section 'mapper' -Key 'gimbal_antiwindup_enabled' -Default 'true'
    $gimbalAntiwindupStart = Get-TomlValue -Lines $lines -Section 'mapper' -Key 'gimbal_antiwindup_start' -Default '0.92'
    $gimbalAntiwindupMinGain = Get-TomlValue -Lines $lines -Section 'mapper' -Key 'gimbal_antiwindup_min_gain' -Default '0.10'
    $inputGainMode = Get-TomlValue -Lines $lines -Section 'mapper' -Key 'input_gain_mode' -Default '"flat"'
    $adaptiveSlowGain = Get-TomlValue -Lines $lines -Section 'mapper' -Key 'adaptive_slow_gain' -Default '0.65'
    $adaptiveFastGain = Get-TomlValue -Lines $lines -Section 'mapper' -Key 'adaptive_fast_gain' -Default '1.60'
    $adaptiveSpeedLow = Get-TomlValue -Lines $lines -Section 'mapper' -Key 'adaptive_speed_low' -Default '120.0'
    $adaptiveSpeedHigh = Get-TomlValue -Lines $lines -Section 'mapper' -Key 'adaptive_speed_high' -Default '1800.0'
    $adaptiveCurve = Get-TomlValue -Lines $lines -Section 'mapper' -Key 'adaptive_curve' -Default '1.0'
    $adaptiveTrackerMs = Get-TomlValue -Lines $lines -Section 'mapper' -Key 'adaptive_tracker_ms' -Default '35.0'
    $gateShape = Get-TomlValue -Lines $lines -Section 'mapper' -Key 'gate_shape' -Default '"axis"'
    $diagonalScale = Get-TomlValue -Lines $lines -Section 'mapper' -Key 'diagonal_scale' -Default '1.0'
    $returnRate = Get-TomlValue -Lines $lines -Section 'mapper' -Key 'return_rate' -Default '0'
    $returnEnabledDefault = if (($returnRate -as [double]) -gt 0.0) { 'true' } else { 'false' }
    $returnEnabled = Get-TomlValue -Lines $lines -Section 'mapper' -Key 'return_enabled' -Default $returnEnabledDefault
    $returnIdle = Get-TomlValue -Lines $lines -Section 'mapper' -Key 'return_idle_ms' -Default '0'
    $constantReturnEnabled = Get-TomlValue -Lines $lines -Section 'mapper' -Key 'constant_return_enabled' -Default 'false'
    $constantReturnRate = Get-TomlValue -Lines $lines -Section 'mapper' -Key 'constant_return_rate' -Default '0'
    $elasticReturnEnabled = Get-TomlValue -Lines $lines -Section 'mapper' -Key 'elastic_return_enabled' -Default 'false'
    $elasticReturnMode = Get-TomlValue -Lines $lines -Section 'mapper' -Key 'elastic_return_mode' -Default 'progressive'
    $elasticReturnCoefficient = Get-TomlValue -Lines $lines -Section 'mapper' -Key 'elastic_return_coefficient' -Default '0'
    $elasticReturnCurve = Get-TomlValue -Lines $lines -Section 'mapper' -Key 'elastic_return_curve' -Default '0'
    $outputShapeNodesText = Get-TomlArrayValue -Lines $lines -Section 'mapper' -Key 'output_shape_nodes'
    $returnShapingEnabled = Get-TomlValue -Lines $lines -Section 'mapper' -Key 'return_shaping_enabled' -Default 'false'
    $returnShapeNodesText = Get-TomlArrayValue -Lines $lines -Section 'mapper' -Key 'return_shape_nodes'
    $invertRoll = Get-TomlValue -Lines $lines -Section 'mapper' -Key 'invert_roll' -Default 'false'
    $invertPitch = Get-TomlValue -Lines $lines -Section 'mapper' -Key 'invert_pitch' -Default 'false'
    $swapAxes = Get-TomlValue -Lines $lines -Section 'mapper' -Key 'swap_axes' -Default 'false'
    $controlMode = Get-TomlValue -Lines $lines -Section 'control' -Key 'mode' -Default '"direct_mouse"'
    $aimSensitivityX = Get-TomlValue -Lines $lines -Section 'mouse_aim' -Key 'sensitivity_x' -Default '1.0'
    $aimSensitivityY = Get-TomlValue -Lines $lines -Section 'mouse_aim' -Key 'sensitivity_y' -Default '1.0'
    $aimReticleLimit = Get-TomlValue -Lines $lines -Section 'mouse_aim' -Key 'reticle_limit' -Default '512'
    $aimDeadband = Get-TomlValue -Lines $lines -Section 'mouse_aim' -Key 'reticle_deadband' -Default '8'
    $aimReturnRate = Get-TomlValue -Lines $lines -Section 'mouse_aim' -Key 'reticle_return_rate' -Default '0'
    $aimSmoothing = Get-TomlValue -Lines $lines -Section 'mouse_aim' -Key 'output_smoothing' -Default '0.10'
    $aimRollGain = Get-TomlValue -Lines $lines -Section 'mouse_aim' -Key 'roll_gain' -Default '0.65'
    $aimYawGain = Get-TomlValue -Lines $lines -Section 'mouse_aim' -Key 'yaw_gain' -Default '0.55'
    $aimPitchGain = Get-TomlValue -Lines $lines -Section 'mouse_aim' -Key 'pitch_gain' -Default '0.85'
    $aimRollMax = Get-TomlValue -Lines $lines -Section 'mouse_aim' -Key 'roll_max' -Default '420'
    $aimYawMax = Get-TomlValue -Lines $lines -Section 'mouse_aim' -Key 'yaw_max' -Default '360'
    $aimPitchMax = Get-TomlValue -Lines $lines -Section 'mouse_aim' -Key 'pitch_max' -Default '420'
    $aimSlewRate = Get-TomlValue -Lines $lines -Section 'mouse_aim' -Key 'slew_rate' -Default '9000'
    $aimInvertX = Get-TomlValue -Lines $lines -Section 'mouse_aim' -Key 'invert_x' -Default 'false'
    $aimInvertY = Get-TomlValue -Lines $lines -Section 'mouse_aim' -Key 'invert_y' -Default 'false'
    $mouseRightStickEnabled = Get-TomlValue -Lines $lines -Section 'mouse_right_stick' -Key 'enabled' -Default 'true'
    $mouseDeviceRight = Get-TomlValue -Lines $lines -Section 'mouse_devices' -Key 'right' -Default 'auto'
    $mouseDeviceLeft = Get-TomlValue -Lines $lines -Section 'mouse_devices' -Key 'left' -Default ''
    $mouseLeftEnabled = Get-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'enabled' -Default 'false'
    $mouseLeftRequireDevice = Get-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'require_device' -Default 'true'
    $mouseLeftThrottleRate = Get-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'throttle_rate' -Default '0.8'
    $mouseLeftThrottleReturnEnabled = Get-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'throttle_return_enabled' -Default 'false'
    $mouseLeftThrottleReturnRate = Get-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'throttle_return_rate' -Default '768.0'
    $mouseLeftYawGain = Get-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_gain' -Default '35.0'
    $mouseLeftYawMax = Get-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_pulse' -Default '512'
    $mouseLeftYawDeadband = Get-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_deadband' -Default '0'
    $mouseLeftYawSmoothing = Get-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_smoothing' -Default '0.0'
    $mouseLeftYawSlewRate = Get-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_slew_rate' -Default '0.0'
    $mouseLeftYawMapperShapingEnabled = Get-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_mapper_shaping_enabled' -Default 'false'
    $legacyLeftYawShapingOn = ((ConvertTo-TomlBool $mouseLeftYawMapperShapingEnabled) -eq 'true')
    $leftYawInputFilterDefault = if ($legacyLeftYawShapingOn) { $inputFilter } elseif (($mouseLeftYawSmoothing -as [double]) -gt 0.0) { '"smoothing"' } else { '"off"' }
    $mouseLeftYawShapingEnabled = Get-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_shaping_enabled' -Default $mouseLeftYawMapperShapingEnabled
    $mouseLeftYawInputFilter = Get-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_input_filter' -Default $leftYawInputFilterDefault
    $mouseLeftYawOneEuroMinCutoffHz = Get-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_one_euro_min_cutoff_hz' -Default $(if ($legacyLeftYawShapingOn) { $oneEuroMinCutoffHz } else { '1.0' })
    $mouseLeftYawOneEuroBeta = Get-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_one_euro_beta' -Default $(if ($legacyLeftYawShapingOn) { $oneEuroBeta } else { '0.05' })
    $mouseLeftYawOneEuroDcutoffHz = Get-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_one_euro_dcutoff_hz' -Default $(if ($legacyLeftYawShapingOn) { $oneEuroDcutoffHz } else { '1.0' })
    $mouseLeftYawDespikeEnabled = Get-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_despike_enabled' -Default $(if ($legacyLeftYawShapingOn) { $despikeEnabled } else { 'false' })
    $mouseLeftYawDespikeCountEnabled = Get-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_despike_count_enabled' -Default $(if ($legacyLeftYawShapingOn) { $despikeCountEnabled } else { 'false' })
    $mouseLeftYawDespikeWindow = Get-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_despike_window' -Default $(if ($legacyLeftYawShapingOn) { $despikeWindow } else { '5' })
    $mouseLeftYawDespikeThresholdSigma = Get-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_despike_threshold_sigma' -Default $(if ($legacyLeftYawShapingOn) { $despikeThresholdSigma } else { '3.0' })
    $mouseLeftYawOutputShapingEnabled = Get-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_output_shaping_enabled' -Default $(if ($legacyLeftYawShapingOn) { $outputShapingEnabled } else { 'false' })
    $mouseLeftYawOutputCurveDefault = if (($mouseLeftYawOutputShapingEnabled -eq 'true') -or $legacyLeftYawShapingOn) { $(if ($legacyLeftYawShapingOn) { $outputCurve } else { '"nodes"' }) } else { '"expo"' }
    $mouseLeftYawOutputCurve = Get-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_output_curve' -Default $mouseLeftYawOutputCurveDefault
    $mouseLeftYawExpo = Get-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_expo' -Default $(if ($legacyLeftYawShapingOn) { $expo } else { '0.0' })
    $mouseLeftYawActualCenter = Get-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_actual_center' -Default $(if ($legacyLeftYawShapingOn) { $actualCenter } else { '0.45' })
    $mouseLeftYawActualMax = Get-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_actual_max' -Default $(if ($legacyLeftYawShapingOn) { $actualMax } else { '1.0' })
    $mouseLeftYawActualExpo = Get-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_actual_expo' -Default $(if ($legacyLeftYawShapingOn) { $actualExpo } else { '0.30' })
    $mouseLeftYawPositionModel = Get-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_position_model' -Default $(if ($legacyLeftYawShapingOn) { $positionModel } else { '"integrator"' })
    $mouseLeftYawGimbalFrequencyHz = Get-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_gimbal_frequency_hz' -Default $(if ($legacyLeftYawShapingOn) { $gimbalFrequencyHz } else { '5.0' })
    $mouseLeftYawGimbalDampingRatio = Get-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_gimbal_damping_ratio' -Default $(if ($legacyLeftYawShapingOn) { $gimbalDampingRatio } else { '1.15' })
    $mouseLeftYawGimbalInputImpulse = Get-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_gimbal_input_impulse' -Default $(if ($legacyLeftYawShapingOn) { $gimbalInputImpulse } else { '1.0' })
    $mouseLeftYawGimbalStaticFriction = Get-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_gimbal_static_friction' -Default $(if ($legacyLeftYawShapingOn) { $gimbalStaticFriction } else { '0.0' })
    $mouseLeftYawGimbalDynamicFriction = Get-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_gimbal_dynamic_friction' -Default $(if ($legacyLeftYawShapingOn) { $gimbalDynamicFriction } else { '0.0' })
    $mouseLeftYawGimbalEdgeBumper = Get-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_gimbal_edge_bumper' -Default $(if ($legacyLeftYawShapingOn) { $gimbalEdgeBumper } else { '0.0' })
    $mouseLeftYawGimbalAntiwindupEnabled = Get-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_gimbal_antiwindup_enabled' -Default $(if ($legacyLeftYawShapingOn) { $gimbalAntiwindupEnabled } else { 'true' })
    $mouseLeftYawGimbalAntiwindupStart = Get-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_gimbal_antiwindup_start' -Default $(if ($legacyLeftYawShapingOn) { $gimbalAntiwindupStart } else { '0.92' })
    $mouseLeftYawGimbalAntiwindupMinGain = Get-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_gimbal_antiwindup_min_gain' -Default $(if ($legacyLeftYawShapingOn) { $gimbalAntiwindupMinGain } else { '0.10' })
    $mouseLeftYawInputGainMode = Get-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_input_gain_mode' -Default $(if ($legacyLeftYawShapingOn) { $inputGainMode } else { '"flat"' })
    $mouseLeftYawAdaptiveSlowGain = Get-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_adaptive_slow_gain' -Default $(if ($legacyLeftYawShapingOn) { $adaptiveSlowGain } else { '0.65' })
    $mouseLeftYawAdaptiveFastGain = Get-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_adaptive_fast_gain' -Default $(if ($legacyLeftYawShapingOn) { $adaptiveFastGain } else { '1.60' })
    $mouseLeftYawAdaptiveSpeedLow = Get-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_adaptive_speed_low' -Default $(if ($legacyLeftYawShapingOn) { $adaptiveSpeedLow } else { '120.0' })
    $mouseLeftYawAdaptiveSpeedHigh = Get-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_adaptive_speed_high' -Default $(if ($legacyLeftYawShapingOn) { $adaptiveSpeedHigh } else { '1800.0' })
    $mouseLeftYawAdaptiveCurve = Get-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_adaptive_curve' -Default $(if ($legacyLeftYawShapingOn) { $adaptiveCurve } else { '1.0' })
    $mouseLeftYawAdaptiveTrackerMs = Get-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_adaptive_tracker_ms' -Default $(if ($legacyLeftYawShapingOn) { $adaptiveTrackerMs } else { '35.0' })
    $mouseLeftYawGateShape = Get-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_gate_shape' -Default $(if ($legacyLeftYawShapingOn) { $gateShape } else { '"axis"' })
    $mouseLeftYawDiagonalScale = Get-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_diagonal_scale' -Default $(if ($legacyLeftYawShapingOn) { $diagonalScale } else { '1.0' })
    $mouseLeftYawReturnEnabled = Get-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_return_enabled' -Default $(if ($legacyLeftYawShapingOn) { $returnEnabled } else { 'false' })
    $mouseLeftYawReturnRate = Get-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_return_rate' -Default $(if ($legacyLeftYawShapingOn) { $returnRate } else { '0' })
    $mouseLeftYawReturnIdle = Get-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_return_idle_ms' -Default $(if ($legacyLeftYawShapingOn) { $returnIdle } else { '0' })
    $mouseLeftYawConstantReturnEnabled = Get-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_constant_return_enabled' -Default $(if ($legacyLeftYawShapingOn) { $constantReturnEnabled } else { 'false' })
    $mouseLeftYawConstantReturnRate = Get-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_constant_return_rate' -Default $(if ($legacyLeftYawShapingOn) { $constantReturnRate } else { '0' })
    $mouseLeftYawElasticReturnEnabled = Get-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_elastic_return_enabled' -Default 'false'
    $mouseLeftYawElasticReturnMode = Get-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_elastic_return_mode' -Default 'progressive'
    $mouseLeftYawElasticReturnCoefficient = Get-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_elastic_return_coefficient' -Default '0'
    $mouseLeftYawElasticReturnCurve = Get-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_elastic_return_curve' -Default '0'
    if ($legacyLeftYawShapingOn) {
        $mouseLeftYawElasticReturnEnabled = Get-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_elastic_return_enabled' -Default $elasticReturnEnabled
        $mouseLeftYawElasticReturnMode = Get-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_elastic_return_mode' -Default $elasticReturnMode
        $mouseLeftYawElasticReturnCoefficient = Get-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_elastic_return_coefficient' -Default $elasticReturnCoefficient
        $mouseLeftYawElasticReturnCurve = Get-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_elastic_return_curve' -Default $elasticReturnCurve
    }
    $mouseLeftYawOutputShapeNodesText = Get-TomlArrayValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_output_shape_nodes'
    if ([string]::IsNullOrWhiteSpace($mouseLeftYawOutputShapeNodesText) -and $legacyLeftYawShapingOn) { $mouseLeftYawOutputShapeNodesText = $outputShapeNodesText }
    $mouseLeftYawReturnShapingEnabled = Get-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_return_shaping_enabled' -Default $(if ($legacyLeftYawShapingOn) { $returnShapingEnabled } else { 'false' })
    $mouseLeftYawReturnShapeNodesText = Get-TomlArrayValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_return_shape_nodes'
    if ([string]::IsNullOrWhiteSpace($mouseLeftYawReturnShapeNodesText) -and $legacyLeftYawShapingOn) { $mouseLeftYawReturnShapeNodesText = $returnShapeNodesText }
    $mouseLeftInvertThrottle = Get-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'invert_throttle' -Default 'false'
    $mouseLeftInvertYaw = Get-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'invert_yaw' -Default 'false'
    $mouseLeftSwapAxes = Get-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'swap_axes' -Default 'false'
    $keyboardEnabled = Get-TomlValue -Lines $lines -Section 'keyboard_left_stick' -Key 'enabled' -Default 'false'
    $keyboardInputSource = Get-TomlValue -Lines $lines -Section 'keyboard_left_stick' -Key 'input_source' -Default '"gameinput"'
    $keyboardRequireAnalog = Get-TomlValue -Lines $lines -Section 'keyboard_left_stick' -Key 'require_analog' -Default 'false'
    $blockSelectedKeys = Get-TomlValue -Lines $lines -Section 'keyboard_left_stick' -Key 'block_selected_keys' -Default 'false'
    $throttleUpKey = Get-TomlValue -Lines $lines -Section 'keyboard_left_stick' -Key 'throttle_up_key' -Default 'W'
    $throttleDownKey = Get-TomlValue -Lines $lines -Section 'keyboard_left_stick' -Key 'throttle_down_key' -Default 'S'
    $yawLeftKey = Get-TomlValue -Lines $lines -Section 'keyboard_left_stick' -Key 'yaw_left_key' -Default 'A'
    $yawRightKey = Get-TomlValue -Lines $lines -Section 'keyboard_left_stick' -Key 'yaw_right_key' -Default 'D'
    $throttleCutKey = Get-TomlValue -Lines $lines -Section 'keyboard_left_stick' -Key 'throttle_cut_key' -Default 'Space'
    $throttleRate = Get-TomlValue -Lines $lines -Section 'keyboard_left_stick' -Key 'throttle_rate' -Default '4096.0'
    $throttleReturnEnabled = Get-TomlValue -Lines $lines -Section 'keyboard_left_stick' -Key 'throttle_return_enabled' -Default 'false'
    $throttleReturnRate = Get-TomlValue -Lines $lines -Section 'keyboard_left_stick' -Key 'throttle_return_rate' -Default '0.0'
    $yawPulse = Get-TomlValue -Lines $lines -Section 'keyboard_left_stick' -Key 'yaw_pulse' -Default '512'
    $yawSlewRate = Get-TomlValue -Lines $lines -Section 'keyboard_left_stick' -Key 'yaw_slew_rate' -Default '4096.0'
    $invertYaw = Get-TomlValue -Lines $lines -Section 'keyboard_left_stick' -Key 'invert_yaw' -Default 'false'
    $keyboardAnalogKeycodeMode = Get-TomlValue -Lines $lines -Section 'keyboard_left_stick' -Key 'analog_keycode_mode' -Default '"virtual_key_translate"'
    $keyboardAnalogDeadzone = Get-TomlValue -Lines $lines -Section 'keyboard_left_stick' -Key 'analog_deadzone' -Default '0.04'
    $keyboardAnalogCurve = Get-TomlValue -Lines $lines -Section 'keyboard_left_stick' -Key 'analog_curve' -Default '1.0'
    $keyboardAnalogMin = Get-TomlValue -Lines $lines -Section 'keyboard_left_stick' -Key 'analog_min' -Default '0.0'
    $keyboardAnalogMax = Get-TomlValue -Lines $lines -Section 'keyboard_left_stick' -Key 'analog_max' -Default '1.0'

    [pscustomobject]@{
        Display = $name
        Name = $name
        FileName = $File.Name
        FullName = $File.FullName
        FrameRate = $frameRate
        StopKey = $stopKey
        FreezeKey = $freezeKey
        RollGain = $rollGain
        PitchGain = $pitchGain
        MaxOutput = $maxOutput
        Deadband = $deadband
        Expo = $expo
        Smoothing = $smoothing
        InputFilter = $inputFilter.Trim('"')
        OneEuroMinCutoffHz = $oneEuroMinCutoffHz
        OneEuroBeta = $oneEuroBeta
        OneEuroDcutoffHz = $oneEuroDcutoffHz
        DespikeEnabled = $despikeEnabled
        DespikeCountEnabled = $despikeCountEnabled
        DespikeWindow = $despikeWindow
        DespikeThresholdSigma = $despikeThresholdSigma
        OutputCurve = $outputCurve.Trim('"')
        ActualCenter = $actualCenter
        ActualMax = $actualMax
        ActualExpo = $actualExpo
        PositionModel = $positionModel.Trim('"')
        GimbalFrequencyHz = $gimbalFrequencyHz
        GimbalDampingRatio = $gimbalDampingRatio
        GimbalInputImpulse = $gimbalInputImpulse
        GimbalStaticFriction = $gimbalStaticFriction
        GimbalDynamicFriction = $gimbalDynamicFriction
        GimbalEdgeBumper = $gimbalEdgeBumper
        GimbalAntiwindupEnabled = $gimbalAntiwindupEnabled
        GimbalAntiwindupStart = $gimbalAntiwindupStart
        GimbalAntiwindupMinGain = $gimbalAntiwindupMinGain
        InputGainMode = $inputGainMode.Trim('"')
        AdaptiveSlowGain = $adaptiveSlowGain
        AdaptiveFastGain = $adaptiveFastGain
        AdaptiveSpeedLow = $adaptiveSpeedLow
        AdaptiveSpeedHigh = $adaptiveSpeedHigh
        AdaptiveCurve = $adaptiveCurve
        AdaptiveTrackerMs = $adaptiveTrackerMs
        GateShape = $gateShape.Trim('"')
        DiagonalScale = $diagonalScale
        ReturnEnabled = $returnEnabled
        ReturnRate = $returnRate
        ReturnIdle = $returnIdle
        ConstantReturnEnabled = $constantReturnEnabled
        ConstantReturnRate = $constantReturnRate
        ElasticReturnEnabled = $elasticReturnEnabled
        ElasticReturnMode = $elasticReturnMode.Trim('"')
        ElasticReturnCoefficient = $elasticReturnCoefficient
        ElasticReturnCurve = $elasticReturnCurve
        OutputShapingEnabled = $outputShapingEnabled
        OutputShapeNodesText = $outputShapeNodesText
        ReturnShapingEnabled = $returnShapingEnabled
        ReturnShapeNodesText = $returnShapeNodesText
        InvertRoll = $invertRoll
        InvertPitch = $invertPitch
        SwapAxes = $swapAxes
        ControlMode = $controlMode.Trim('"')
        AimSensitivityX = $aimSensitivityX
        AimSensitivityY = $aimSensitivityY
        AimReticleLimit = $aimReticleLimit
        AimDeadband = $aimDeadband
        AimReturnRate = $aimReturnRate
        AimSmoothing = $aimSmoothing
        AimRollGain = $aimRollGain
        AimYawGain = $aimYawGain
        AimPitchGain = $aimPitchGain
        AimRollMax = $aimRollMax
        AimYawMax = $aimYawMax
        AimPitchMax = $aimPitchMax
        AimSlewRate = $aimSlewRate
        AimInvertX = $aimInvertX
        AimInvertY = $aimInvertY
        MouseRightStickEnabled = $mouseRightStickEnabled
        MouseDeviceRight = $mouseDeviceRight
        MouseDeviceLeft = $mouseDeviceLeft
        MouseLeftEnabled = $mouseLeftEnabled
        MouseLeftRequireDevice = $mouseLeftRequireDevice
        MouseLeftThrottleRate = $mouseLeftThrottleRate
        MouseLeftThrottleReturnEnabled = $mouseLeftThrottleReturnEnabled
        MouseLeftThrottleReturnRate = $mouseLeftThrottleReturnRate
        MouseLeftYawGain = $mouseLeftYawGain
        MouseLeftYawMax = $mouseLeftYawMax
        MouseLeftYawDeadband = $mouseLeftYawDeadband
        MouseLeftYawSmoothing = $mouseLeftYawSmoothing
        MouseLeftYawSlewRate = $mouseLeftYawSlewRate
        MouseLeftYawShapingEnabled = $mouseLeftYawShapingEnabled
        MouseLeftYawInputFilter = $mouseLeftYawInputFilter.Trim('"')
        MouseLeftYawOneEuroMinCutoffHz = $mouseLeftYawOneEuroMinCutoffHz
        MouseLeftYawOneEuroBeta = $mouseLeftYawOneEuroBeta
        MouseLeftYawOneEuroDcutoffHz = $mouseLeftYawOneEuroDcutoffHz
        MouseLeftYawDespikeEnabled = $mouseLeftYawDespikeEnabled
        MouseLeftYawDespikeCountEnabled = $mouseLeftYawDespikeCountEnabled
        MouseLeftYawDespikeWindow = $mouseLeftYawDespikeWindow
        MouseLeftYawDespikeThresholdSigma = $mouseLeftYawDespikeThresholdSigma
        MouseLeftYawOutputCurve = $mouseLeftYawOutputCurve.Trim('"')
        MouseLeftYawExpo = $mouseLeftYawExpo
        MouseLeftYawActualCenter = $mouseLeftYawActualCenter
        MouseLeftYawActualMax = $mouseLeftYawActualMax
        MouseLeftYawActualExpo = $mouseLeftYawActualExpo
        MouseLeftYawPositionModel = $mouseLeftYawPositionModel.Trim('"')
        MouseLeftYawGimbalFrequencyHz = $mouseLeftYawGimbalFrequencyHz
        MouseLeftYawGimbalDampingRatio = $mouseLeftYawGimbalDampingRatio
        MouseLeftYawGimbalInputImpulse = $mouseLeftYawGimbalInputImpulse
        MouseLeftYawGimbalStaticFriction = $mouseLeftYawGimbalStaticFriction
        MouseLeftYawGimbalDynamicFriction = $mouseLeftYawGimbalDynamicFriction
        MouseLeftYawGimbalEdgeBumper = $mouseLeftYawGimbalEdgeBumper
        MouseLeftYawGimbalAntiwindupEnabled = $mouseLeftYawGimbalAntiwindupEnabled
        MouseLeftYawGimbalAntiwindupStart = $mouseLeftYawGimbalAntiwindupStart
        MouseLeftYawGimbalAntiwindupMinGain = $mouseLeftYawGimbalAntiwindupMinGain
        MouseLeftYawInputGainMode = $mouseLeftYawInputGainMode.Trim('"')
        MouseLeftYawAdaptiveSlowGain = $mouseLeftYawAdaptiveSlowGain
        MouseLeftYawAdaptiveFastGain = $mouseLeftYawAdaptiveFastGain
        MouseLeftYawAdaptiveSpeedLow = $mouseLeftYawAdaptiveSpeedLow
        MouseLeftYawAdaptiveSpeedHigh = $mouseLeftYawAdaptiveSpeedHigh
        MouseLeftYawAdaptiveCurve = $mouseLeftYawAdaptiveCurve
        MouseLeftYawAdaptiveTrackerMs = $mouseLeftYawAdaptiveTrackerMs
        MouseLeftYawGateShape = $mouseLeftYawGateShape.Trim('"')
        MouseLeftYawDiagonalScale = $mouseLeftYawDiagonalScale
        MouseLeftYawReturnEnabled = $mouseLeftYawReturnEnabled
        MouseLeftYawReturnRate = $mouseLeftYawReturnRate
        MouseLeftYawReturnIdle = $mouseLeftYawReturnIdle
        MouseLeftYawConstantReturnEnabled = $mouseLeftYawConstantReturnEnabled
        MouseLeftYawConstantReturnRate = $mouseLeftYawConstantReturnRate
        MouseLeftYawElasticReturnEnabled = $mouseLeftYawElasticReturnEnabled
        MouseLeftYawElasticReturnMode = $mouseLeftYawElasticReturnMode.Trim('"')
        MouseLeftYawElasticReturnCoefficient = $mouseLeftYawElasticReturnCoefficient
        MouseLeftYawElasticReturnCurve = $mouseLeftYawElasticReturnCurve
        MouseLeftYawOutputShapingEnabled = $mouseLeftYawOutputShapingEnabled
        MouseLeftYawOutputShapeNodesText = $mouseLeftYawOutputShapeNodesText
        MouseLeftYawReturnShapingEnabled = $mouseLeftYawReturnShapingEnabled
        MouseLeftYawReturnShapeNodesText = $mouseLeftYawReturnShapeNodesText
        MouseLeftInvertThrottle = $mouseLeftInvertThrottle
        MouseLeftInvertYaw = $mouseLeftInvertYaw
        MouseLeftSwapAxes = $mouseLeftSwapAxes
        KeyboardEnabled = $keyboardEnabled
        KeyboardInputSource = $keyboardInputSource.Trim('"')
        KeyboardRequireAnalog = $keyboardRequireAnalog
        BlockSelectedKeys = $blockSelectedKeys
        ThrottleUpKey = $throttleUpKey
        ThrottleDownKey = $throttleDownKey
        YawLeftKey = $yawLeftKey
        YawRightKey = $yawRightKey
        ThrottleCutKey = $throttleCutKey
        ThrottleRate = $throttleRate
        ThrottleReturnEnabled = $throttleReturnEnabled
        ThrottleReturnRate = $throttleReturnRate
        YawPulse = $yawPulse
        YawSlewRate = $yawSlewRate
        InvertYaw = $invertYaw
        KeyboardAnalogKeycodeMode = $keyboardAnalogKeycodeMode.Trim('"')
        KeyboardAnalogDeadzone = $keyboardAnalogDeadzone
        KeyboardAnalogCurve = $keyboardAnalogCurve
        KeyboardAnalogMin = $keyboardAnalogMin
        KeyboardAnalogMax = $keyboardAnalogMax
    }
}

function Load-Profiles {
    if (-not (Test-Path -LiteralPath $profilesDir)) {
        return @()
    }

    $defaultFileName = Get-DefaultProfileFileName
    @(Get-ChildItem -LiteralPath $profilesDir -Filter '*.toml' -File |
        Sort-Object Name |
        ForEach-Object {
            $profile = Get-ProfileInfo -File $_
            if ($profile.FileName -eq $defaultFileName) {
                $profile.Display = "$($profile.Name) [default]"
            }
            $profile
        })
}

function Start-Gx12Process {
    param(
        [string]$ProfilePath,
        [string]$Mode
    )

    if (-not (Test-Path -LiteralPath $exePath)) {
        [System.Windows.Forms.MessageBox]::Show(
            "Missing executable:`r`n$exePath",
            'GX12 Launcher',
            [System.Windows.Forms.MessageBoxButtons]::OK,
            [System.Windows.Forms.MessageBoxIcon]::Error
        ) | Out-Null
        return
    }

    if (-not (Test-Path -LiteralPath $ProfilePath)) {
        [System.Windows.Forms.MessageBox]::Show(
            "Missing profile:`r`n$ProfilePath",
            'GX12 Launcher',
            [System.Windows.Forms.MessageBoxButtons]::OK,
            [System.Windows.Forms.MessageBoxIcon]::Error
        ) | Out-Null
        return
    }

    Stop-ActiveGx12Run
    Reset-LauncherStopEvent

    if ($Mode -ne 'CompositeTrainer') {
        [System.Windows.Forms.MessageBox]::Show(
            "Legacy sim output has been removed. Use the composite trainer path with the GX12 joystick.",
            'GX12 Launcher',
            [System.Windows.Forms.MessageBoxButtons]::OK,
            [System.Windows.Forms.MessageBoxIcon]::Information
        ) | Out-Null
        return
    }

    $command = '"{0}" --trainer-profile "{1}" live' -f $exePath, $ProfilePath
    $title = 'GX12 Composite Trainer'

    $arguments = '/c title {0} && cd /d "{1}" && echo {0} && echo Profile: "{2}" && echo. && {3} & set GX12_RESULT=%ERRORLEVEL% & echo. & echo gx12mouse exited with code %GX12_RESULT%. & echo. & pause & exit /b %GX12_RESULT%' -f $title, $root, $ProfilePath, $command
    $script:activeConsoleProcess = Start-Process -FilePath 'cmd.exe' -ArgumentList $arguments -WorkingDirectory $root -PassThru
    $status.Text = "Running $title with $([System.IO.Path]::GetFileName($ProfilePath)). Starting another profile will stop this run first."
}

function ConvertTo-TomlBool {
    param([object]$Value)
    if ($Value -is [bool]) {
        return ($(if ($Value) { 'true' } else { 'false' }))
    }
    $text = [string]$Value
    return ($(if ($text.Trim().ToLowerInvariant() -eq 'true') { 'true' } else { 'false' }))
}

function Set-TomlValue {
    param(
        [string[]]$Lines,
        [string]$Section,
        [string]$Key,
        [string]$Value
    )

    $result = New-Object System.Collections.Generic.List[string]
    $inSection = $false
    $sectionSeen = $false
    $keyWritten = $false

    foreach ($line in $Lines) {
        $trimmed = $line.Trim()
        if ($trimmed -match '^\[(.+)\]$') {
            if ($inSection -and -not $keyWritten) {
                $result.Add(("{0} = {1}" -f $Key, $Value))
                $keyWritten = $true
            }
            $inSection = ($Matches[1] -eq $Section)
            if ($inSection) {
                $sectionSeen = $true
            }
            $result.Add($line)
            continue
        }

        if ($inSection -and $trimmed -match ('^{0}\s*=' -f [regex]::Escape($Key))) {
            $prefix = ''
            if ($line -match '^(\s*)') {
                $prefix = $Matches[1]
            }
            $result.Add(("{0}{1} = {2}" -f $prefix, $Key, $Value))
            $keyWritten = $true
            continue
        }

        $result.Add($line)
    }

    if (-not $sectionSeen) {
        if ($result.Count -gt 0 -and $result[$result.Count - 1].Trim().Length -ne 0) {
            $result.Add('')
        }
        $result.Add(("[$Section]"))
        $result.Add(("{0} = {1}" -f $Key, $Value))
    } elseif ($inSection -and -not $keyWritten) {
        $result.Add(("{0} = {1}" -f $Key, $Value))
    }

    return $result.ToArray()
}

function Test-DoubleField {
    param(
        [string]$Text,
        [double]$Min,
        [double]$Max,
        [switch]$MaxExclusive
    )

    $style = [System.Globalization.NumberStyles]::Float
    $culture = [System.Globalization.CultureInfo]::InvariantCulture
    $value = 0.0
    if (-not [double]::TryParse($Text, $style, $culture, [ref]$value)) {
        return $false
    }
    if ($value -lt $Min) {
        return $false
    }
    if ($MaxExclusive) {
        return $value -lt $Max
    }
    return $value -le $Max
}

function Test-IntField {
    param(
        [string]$Text,
        [int]$Min,
        [int]$Max
    )

    $style = [System.Globalization.NumberStyles]::Integer
    $culture = [System.Globalization.CultureInfo]::InvariantCulture
    $value = 0
    if (-not [int]::TryParse($Text, $style, $culture, [ref]$value)) {
        return $false
    }
    return ($value -ge $Min -and $value -le $Max)
}

function Test-KeyField {
    param([string]$Text)

    $key = $Text.Trim().ToLowerInvariant()
    if ($key.Length -eq 1) {
        return ($key -match '^[a-z0-9]$')
    }
    if ($key -in @('space', 'spacebar', 'esc', 'escape', 'shift', 'ctrl', 'control', 'alt', 'tab', 'enter', 'return', 'backspace', 'up', 'down', 'left', 'right')) {
        return $true
    }
    if ($key -match '^f([1-9]|1[0-9]|2[0-4])$') {
        return $true
    }
    if ($key -match '^vk([1-9]|[1-9][0-9]|1[0-9][0-9]|2[0-4][0-9]|25[0-5])$') {
        return $true
    }
    return $false
}

function Test-ControlModeField {
    param([string]$Text)

    $mode = $Text.Trim().Trim('"').ToLowerInvariant()
    return ($mode -eq 'direct_mouse' -or $mode -eq 'direct' -or $mode -eq 'drone_mouse_aim' -or $mode -eq 'mouse_aim' -or $mode -eq 'war_thunder')
}

function Test-ElasticReturnModeField {
    param([string]$Text)

    $mode = $Text.Trim().Trim('"').ToLowerInvariant()
    return ($mode -in @('linear', 'progressive', 'smoothstep', 'expo'))
}

function Test-PositionModelField {
    param([string]$Text)

    $mode = $Text.Trim().Trim('"').ToLowerInvariant()
    return ($mode -in @('integrator', 'dynamic_gimbal'))
}

function Test-InputGainModeField {
    param([string]$Text)

    $mode = $Text.Trim().Trim('"').ToLowerInvariant()
    return ($mode -in @('flat', 'adaptive'))
}

function Test-InputFilterField {
    param([string]$Text)

    $mode = $Text.Trim().Trim('"').ToLowerInvariant()
    return ($mode -in @('off', 'smoothing', 'one_euro'))
}

function Test-OutputCurveField {
    param([string]$Text)

    $mode = $Text.Trim().Trim('"').ToLowerInvariant()
    return ($mode -in @('expo', 'nodes', 'actual'))
}

function Test-GateShapeField {
    param([string]$Text)

    $shape = $Text.Trim().Trim('"').ToLowerInvariant()
    return ($shape -in @('axis', 'circle', 'octagon', 'square'))
}

function Get-StickShapeEditorNodes {
    param([string]$Name)

    if (-not $script:tuningFields.ContainsKey($Name)) { return @() }
    $editor = $script:tuningFields[$Name]
    if ($null -eq $editor) { return @() }
    if ($editor -is [Gx12Launcher.StickShapeEditor]) {
        $count = [int]$editor.NodeCount
        if ($count -le 0) { return @() }
        $xs = $editor.GetXs()
        $ys = $editor.GetYs()
        $widths = $editor.GetWidths()
        $list = New-Object 'System.Collections.Generic.List[object]'
        for ($i = 0; $i -lt $count; $i++) {
            $list.Add([pscustomobject]@{ X = [double]$xs[$i]; Y = [double]$ys[$i]; Width = [double]$widths[$i] })
        }
        return ,@($list.ToArray())
    }
    if ($editor.PSObject.Properties['Text']) {
        return (ConvertFrom-StickShapeNodesText -Text [string]$editor.Text)
    }
    return @()
}

function Convert-KeyCodeToProfileKey {
    param([System.Windows.Forms.Keys]$KeyCode)

    $value = [int]$KeyCode
    if ($value -ge [int][System.Windows.Forms.Keys]::A -and $value -le [int][System.Windows.Forms.Keys]::Z) {
        return [string][char]$value
    }
    if ($value -ge [int][System.Windows.Forms.Keys]::D0 -and $value -le [int][System.Windows.Forms.Keys]::D9) {
        return [string][char]([int][char]'0' + ($value - [int][System.Windows.Forms.Keys]::D0))
    }
    if ($value -ge [int][System.Windows.Forms.Keys]::NumPad0 -and $value -le [int][System.Windows.Forms.Keys]::NumPad9) {
        return [string][char]([int][char]'0' + ($value - [int][System.Windows.Forms.Keys]::NumPad0))
    }
    if ($value -ge [int][System.Windows.Forms.Keys]::F1 -and $value -le [int][System.Windows.Forms.Keys]::F24) {
        return 'F{0}' -f (1 + $value - [int][System.Windows.Forms.Keys]::F1)
    }

    switch ($KeyCode) {
        ([System.Windows.Forms.Keys]::Space) { return 'Space' }
        ([System.Windows.Forms.Keys]::Escape) { return 'Esc' }
        ([System.Windows.Forms.Keys]::ShiftKey) { return 'Shift' }
        ([System.Windows.Forms.Keys]::LShiftKey) { return 'Shift' }
        ([System.Windows.Forms.Keys]::RShiftKey) { return 'Shift' }
        ([System.Windows.Forms.Keys]::ControlKey) { return 'Ctrl' }
        ([System.Windows.Forms.Keys]::LControlKey) { return 'Ctrl' }
        ([System.Windows.Forms.Keys]::RControlKey) { return 'Ctrl' }
        ([System.Windows.Forms.Keys]::Menu) { return 'Alt' }
        ([System.Windows.Forms.Keys]::LMenu) { return 'Alt' }
        ([System.Windows.Forms.Keys]::RMenu) { return 'Alt' }
        ([System.Windows.Forms.Keys]::Tab) { return 'Tab' }
        ([System.Windows.Forms.Keys]::Return) { return 'Enter' }
        ([System.Windows.Forms.Keys]::Back) { return 'Backspace' }
        ([System.Windows.Forms.Keys]::Up) { return 'Up' }
        ([System.Windows.Forms.Keys]::Down) { return 'Down' }
        ([System.Windows.Forms.Keys]::Left) { return 'Left' }
        ([System.Windows.Forms.Keys]::Right) { return 'Right' }
        default { return ('VK{0}' -f $value) }
    }
}

function Mark-TuningField {
    param(
        [string]$Name,
        [bool]$Valid
    )

    $control = $script:tuningFields[$Name]
    if ($null -eq $control) {
        return
    }
    if ($Valid) {
        $control.BackColor = [System.Drawing.SystemColors]::Window
    } else {
        $control.BackColor = [System.Drawing.Color]::MistyRose
    }
}

function Validate-TuningControls {
    $ok = $true
    $checks = @(
        @{ Name = 'FrameRate'; Valid = (Test-IntField -Text $script:tuningFields.FrameRate.Text -Min 1 -Max 8000); Message = 'frame rate must be 1..8000' },
        @{ Name = 'StopKey'; Valid = (Test-KeyField -Text $script:tuningFields.StopKey.Text); Message = 'stop key is not recognized' },
        @{ Name = 'FreezeKey'; Valid = (Test-KeyField -Text $script:tuningFields.FreezeKey.Text); Message = 'freeze key is not recognized' },
        @{ Name = 'RollGain'; Valid = (Test-DoubleField -Text $script:tuningFields.RollGain.Text -Min 0 -Max 5000); Message = 'roll gain must be 0..5000' },
        @{ Name = 'PitchGain'; Valid = (Test-DoubleField -Text $script:tuningFields.PitchGain.Text -Min 0 -Max 5000); Message = 'pitch gain must be 0..5000' },
        @{ Name = 'MaxOutput'; Valid = (Test-IntField -Text $script:tuningFields.MaxOutput.Text -Min 1 -Max 512); Message = 'max output must be 1..512' },
        @{ Name = 'Expo'; Valid = (Test-DoubleField -Text $script:tuningFields.Expo.Text -Min 0 -Max 1); Message = 'expo must be 0.0..1.0' },
        @{ Name = 'Smoothing'; Valid = (Test-DoubleField -Text $script:tuningFields.Smoothing.Text -Min 0 -Max 1 -MaxExclusive); Message = 'smoothing must be >=0.0 and <1.0' },
        @{ Name = 'InputFilter'; Valid = (Test-InputFilterField -Text $script:tuningFields.InputFilter.Text); Message = 'input filter must be off, smoothing, or one_euro' },
        @{ Name = 'OneEuroMinCutoffHz'; Valid = (Test-DoubleField -Text $script:tuningFields.OneEuroMinCutoffHz.Text -Min 0.05 -Max 120); Message = 'one-euro min cutoff must be 0.05..120 Hz' },
        @{ Name = 'OneEuroBeta'; Valid = (Test-DoubleField -Text $script:tuningFields.OneEuroBeta.Text -Min 0 -Max 2); Message = 'one-euro beta must be 0..2' },
        @{ Name = 'OneEuroDcutoffHz'; Valid = (Test-DoubleField -Text $script:tuningFields.OneEuroDcutoffHz.Text -Min 0.1 -Max 120); Message = 'one-euro derivative cutoff must be 0.1..120 Hz' },
        @{ Name = 'DespikeWindow'; Valid = ((Test-IntField -Text $script:tuningFields.DespikeWindow.Text -Min 3 -Max 15) -and (([int]$script:tuningFields.DespikeWindow.Text.Trim() % 2) -eq 1)); Message = 'despike window must be odd and 3..15' },
        @{ Name = 'DespikeThresholdSigma'; Valid = (Test-DoubleField -Text $script:tuningFields.DespikeThresholdSigma.Text -Min 0.0001 -Max 20); Message = 'despike threshold sigma must be >0 and <=20' },
        @{ Name = 'OutputCurve'; Valid = (Test-OutputCurveField -Text $script:tuningFields.OutputCurve.Text); Message = 'output curve must be expo, nodes, or actual' },
        @{ Name = 'ActualCenter'; Valid = (Test-DoubleField -Text $script:tuningFields.ActualCenter.Text -Min 0 -Max 1); Message = 'actual center must be 0..1' },
        @{ Name = 'ActualMax'; Valid = (Test-DoubleField -Text $script:tuningFields.ActualMax.Text -Min 0 -Max 1); Message = 'actual max must be 0..1' },
        @{ Name = 'ActualExpo'; Valid = (Test-DoubleField -Text $script:tuningFields.ActualExpo.Text -Min 0 -Max 0.95); Message = 'actual expo must be 0..0.95' },
        @{ Name = 'PositionModel'; Valid = (Test-PositionModelField -Text $script:tuningFields.PositionModel.Text); Message = 'position model must be integrator or dynamic_gimbal' },
        @{ Name = 'GimbalFrequencyHz'; Valid = (Test-DoubleField -Text $script:tuningFields.GimbalFrequencyHz.Text -Min 0.1 -Max 80); Message = 'gimbal frequency must be 0.1..80 Hz' },
        @{ Name = 'GimbalDampingRatio'; Valid = (Test-DoubleField -Text $script:tuningFields.GimbalDampingRatio.Text -Min 0 -Max 5); Message = 'gimbal damping ratio must be 0..5' },
        @{ Name = 'GimbalInputImpulse'; Valid = (Test-DoubleField -Text $script:tuningFields.GimbalInputImpulse.Text -Min 0 -Max 5); Message = 'gimbal input impulse must be 0..5' },
        @{ Name = 'GimbalStaticFriction'; Valid = (Test-DoubleField -Text $script:tuningFields.GimbalStaticFriction.Text -Min 0 -Max 128); Message = 'gimbal static friction must be 0..128' },
        @{ Name = 'GimbalDynamicFriction'; Valid = (Test-DoubleField -Text $script:tuningFields.GimbalDynamicFriction.Text -Min 0 -Max 100); Message = 'gimbal dynamic friction must be 0..100' },
        @{ Name = 'GimbalEdgeBumper'; Valid = (Test-DoubleField -Text $script:tuningFields.GimbalEdgeBumper.Text -Min 0 -Max 20); Message = 'gimbal edge bumper must be 0..20' },
        @{ Name = 'GimbalAntiwindupStart'; Valid = (Test-DoubleField -Text $script:tuningFields.GimbalAntiwindupStart.Text -Min 0 -Max 1 -MaxExclusive); Message = 'gimbal anti-windup start must be >=0 and <1' },
        @{ Name = 'GimbalAntiwindupMinGain'; Valid = (Test-DoubleField -Text $script:tuningFields.GimbalAntiwindupMinGain.Text -Min 0 -Max 1); Message = 'gimbal anti-windup min gain must be 0..1' },
        @{ Name = 'InputGainMode'; Valid = (Test-InputGainModeField -Text $script:tuningFields.InputGainMode.Text); Message = 'input gain mode must be flat or adaptive' },
        @{ Name = 'AdaptiveSlowGain'; Valid = (Test-DoubleField -Text $script:tuningFields.AdaptiveSlowGain.Text -Min 0 -Max 5); Message = 'adaptive slow gain must be 0..5' },
        @{ Name = 'AdaptiveFastGain'; Valid = (Test-DoubleField -Text $script:tuningFields.AdaptiveFastGain.Text -Min 0 -Max 5); Message = 'adaptive fast gain must be 0..5' },
        @{ Name = 'AdaptiveSpeedLow'; Valid = (Test-DoubleField -Text $script:tuningFields.AdaptiveSpeedLow.Text -Min 0 -Max 100000); Message = 'adaptive speed low must be 0..100000' },
        @{ Name = 'AdaptiveSpeedHigh'; Valid = (Test-DoubleField -Text $script:tuningFields.AdaptiveSpeedHigh.Text -Min 0 -Max 100000); Message = 'adaptive speed high must be 0..100000' },
        @{ Name = 'AdaptiveCurve'; Valid = (Test-DoubleField -Text $script:tuningFields.AdaptiveCurve.Text -Min 0.0001 -Max 5); Message = 'adaptive curve must be >0 and <=5' },
        @{ Name = 'AdaptiveTrackerMs'; Valid = (Test-DoubleField -Text $script:tuningFields.AdaptiveTrackerMs.Text -Min 0 -Max 1000); Message = 'adaptive tracker must be 0..1000 ms' },
        @{ Name = 'GateShape'; Valid = (Test-GateShapeField -Text $script:tuningFields.GateShape.Text); Message = 'gate shape must be axis, circle, octagon, or square' },
        @{ Name = 'DiagonalScale'; Valid = (Test-DoubleField -Text $script:tuningFields.DiagonalScale.Text -Min 0 -Max 1.5); Message = 'diagonal scale must be 0..1.5' },
        @{ Name = 'ReturnRate'; Valid = (Test-DoubleField -Text $script:tuningFields.ReturnRate.Text -Min 0 -Max 20000); Message = 'idle return rate must be 0..20000' },
        @{ Name = 'ReturnIdle'; Valid = (Test-DoubleField -Text $script:tuningFields.ReturnIdle.Text -Min 0 -Max 1000); Message = 'idle return delay must be 0..1000 ms' },
        @{ Name = 'ConstantReturnRate'; Valid = (Test-DoubleField -Text $script:tuningFields.ConstantReturnRate.Text -Min 0 -Max 20000); Message = 'constant return rate must be 0..20000' },
        @{ Name = 'ElasticReturnMode'; Valid = (Test-ElasticReturnModeField -Text $script:tuningFields.ElasticReturnMode.Text); Message = 'elastic return mode must be linear, progressive, smoothstep, or expo' },
        @{ Name = 'ElasticReturnCoefficient'; Valid = (Test-DoubleField -Text $script:tuningFields.ElasticReturnCoefficient.Text -Min 0 -Max 100); Message = 'elastic return coefficient must be 0..100' },
        @{ Name = 'ElasticReturnCurve'; Valid = (Test-DoubleField -Text $script:tuningFields.ElasticReturnCurve.Text -Min 0 -Max 5); Message = 'elastic return curve must be 0..5' },
        @{ Name = 'AimSensitivityX'; Valid = (Test-DoubleField -Text $script:tuningFields.AimSensitivityX.Text -Min 0 -Max 50); Message = 'aim sensitivity X must be 0..50' },
        @{ Name = 'AimSensitivityY'; Valid = (Test-DoubleField -Text $script:tuningFields.AimSensitivityY.Text -Min 0 -Max 50); Message = 'aim sensitivity Y must be 0..50' },
        @{ Name = 'AimReticleLimit'; Valid = (Test-DoubleField -Text $script:tuningFields.AimReticleLimit.Text -Min 1 -Max 4096); Message = 'aim reticle limit must be 1..4096' },
        @{ Name = 'AimDeadband'; Valid = (Test-DoubleField -Text $script:tuningFields.AimDeadband.Text -Min 0 -Max 4095); Message = 'aim deadband must be 0..reticle limit' },
        @{ Name = 'AimReturnRate'; Valid = (Test-DoubleField -Text $script:tuningFields.AimReturnRate.Text -Min 0 -Max 20000); Message = 'aim return rate must be 0..20000' },
        @{ Name = 'AimSmoothing'; Valid = (Test-DoubleField -Text $script:tuningFields.AimSmoothing.Text -Min 0 -Max 1 -MaxExclusive); Message = 'aim smoothing must be >=0.0 and <1.0' },
        @{ Name = 'AimRollGain'; Valid = (Test-DoubleField -Text $script:tuningFields.AimRollGain.Text -Min 0 -Max 5); Message = 'aim roll gain must be 0..5' },
        @{ Name = 'AimYawGain'; Valid = (Test-DoubleField -Text $script:tuningFields.AimYawGain.Text -Min 0 -Max 5); Message = 'aim yaw gain must be 0..5' },
        @{ Name = 'AimPitchGain'; Valid = (Test-DoubleField -Text $script:tuningFields.AimPitchGain.Text -Min 0 -Max 5); Message = 'aim pitch gain must be 0..5' },
        @{ Name = 'AimRollMax'; Valid = (Test-IntField -Text $script:tuningFields.AimRollMax.Text -Min 0 -Max 512); Message = 'aim roll max must be 0..512' },
        @{ Name = 'AimYawMax'; Valid = (Test-IntField -Text $script:tuningFields.AimYawMax.Text -Min 0 -Max 512); Message = 'aim yaw max must be 0..512' },
        @{ Name = 'AimPitchMax'; Valid = (Test-IntField -Text $script:tuningFields.AimPitchMax.Text -Min 0 -Max 512); Message = 'aim pitch max must be 0..512' },
        @{ Name = 'AimSlewRate'; Valid = (Test-DoubleField -Text $script:tuningFields.AimSlewRate.Text -Min 0 -Max 20000); Message = 'aim slew rate must be 0..20000' },
        @{ Name = 'ThrottleUpKey'; Valid = (Test-KeyField -Text $script:tuningFields.ThrottleUpKey.Text); Message = 'throttle up key is not recognized' },
        @{ Name = 'ThrottleDownKey'; Valid = (Test-KeyField -Text $script:tuningFields.ThrottleDownKey.Text); Message = 'throttle down key is not recognized' },
        @{ Name = 'ThrottleCutKey'; Valid = (Test-KeyField -Text $script:tuningFields.ThrottleCutKey.Text); Message = 'throttle cut key is not recognized' },
        @{ Name = 'ThrottleRate'; Valid = (Test-DoubleField -Text $script:tuningFields.ThrottleRate.Text -Min 0 -Max 10000); Message = 'throttle rate must be 0..10000' },
        @{ Name = 'ThrottleReturnRate'; Valid = (Test-DoubleField -Text $script:tuningFields.ThrottleReturnRate.Text -Min 0 -Max 20000); Message = 'throttle return rate must be 0..20000' },
        @{ Name = 'YawLeftKey'; Valid = (Test-KeyField -Text $script:tuningFields.YawLeftKey.Text); Message = 'yaw left key is not recognized' },
        @{ Name = 'YawRightKey'; Valid = (Test-KeyField -Text $script:tuningFields.YawRightKey.Text); Message = 'yaw right key is not recognized' },
        @{ Name = 'YawPulse'; Valid = (Test-IntField -Text $script:tuningFields.YawPulse.Text -Min 0 -Max 512); Message = 'yaw pulse must be 0..512' },
        @{ Name = 'YawSlewRate'; Valid = (Test-DoubleField -Text $script:tuningFields.YawSlewRate.Text -Min 0 -Max 20000); Message = 'yaw slew must be 0..20000' },
        @{ Name = 'MouseLeftThrottleRate'; Valid = (Test-DoubleField -Text $script:tuningFields.MouseLeftThrottleRate.Text -Min 0 -Max 10000); Message = 'mouse throttle sensitivity must be 0..10000' },
        @{ Name = 'MouseLeftThrottleReturnRate'; Valid = (Test-DoubleField -Text $script:tuningFields.MouseLeftThrottleReturnRate.Text -Min 0 -Max 20000); Message = 'mouse throttle return must be 0..20000' },
        @{ Name = 'MouseLeftYawGain'; Valid = (Test-DoubleField -Text $script:tuningFields.MouseLeftYawGain.Text -Min 0 -Max 5000); Message = 'mouse yaw sensitivity must be 0..5000' },
        @{ Name = 'MouseLeftYawMax'; Valid = (Test-IntField -Text $script:tuningFields.MouseLeftYawMax.Text -Min 0 -Max 512); Message = 'mouse max yaw must be 0..512' },
        @{ Name = 'MouseLeftYawDeadband'; Valid = (Test-IntField -Text $script:tuningFields.MouseLeftYawDeadband.Text -Min 0 -Max 511); Message = 'mouse yaw deadband must be 0..511' },
        @{ Name = 'MouseLeftYawSlewRate'; Valid = (Test-DoubleField -Text $script:tuningFields.MouseLeftYawSlewRate.Text -Min 0 -Max 20000); Message = 'mouse yaw response must be 0..20000' },
        @{ Name = 'MouseLeftYawInputFilter'; Valid = (Test-InputFilterField -Text $script:tuningFields.MouseLeftYawInputFilter.Text); Message = 'mouse yaw input filter must be off, smoothing, or one_euro' },
        @{ Name = 'MouseLeftYawOneEuroMinCutoffHz'; Valid = (Test-DoubleField -Text $script:tuningFields.MouseLeftYawOneEuroMinCutoffHz.Text -Min 0.05 -Max 120); Message = 'mouse yaw one-euro min cutoff must be 0.05..120 Hz' },
        @{ Name = 'MouseLeftYawOneEuroBeta'; Valid = (Test-DoubleField -Text $script:tuningFields.MouseLeftYawOneEuroBeta.Text -Min 0 -Max 2); Message = 'mouse yaw one-euro beta must be 0..2' },
        @{ Name = 'MouseLeftYawOneEuroDcutoffHz'; Valid = (Test-DoubleField -Text $script:tuningFields.MouseLeftYawOneEuroDcutoffHz.Text -Min 0.1 -Max 120); Message = 'mouse yaw one-euro derivative cutoff must be 0.1..120 Hz' },
        @{ Name = 'MouseLeftYawDespikeWindow'; Valid = ((Test-IntField -Text $script:tuningFields.MouseLeftYawDespikeWindow.Text -Min 3 -Max 15) -and (([int]$script:tuningFields.MouseLeftYawDespikeWindow.Text.Trim() % 2) -eq 1)); Message = 'mouse yaw despike window must be odd and 3..15' },
        @{ Name = 'MouseLeftYawDespikeThresholdSigma'; Valid = (Test-DoubleField -Text $script:tuningFields.MouseLeftYawDespikeThresholdSigma.Text -Min 0.0001 -Max 20); Message = 'mouse yaw despike sigma must be >0 and <=20' },
        @{ Name = 'MouseLeftYawOutputCurve'; Valid = (Test-OutputCurveField -Text $script:tuningFields.MouseLeftYawOutputCurve.Text); Message = 'mouse yaw output curve must be expo, nodes, or actual' },
        @{ Name = 'MouseLeftYawExpo'; Valid = (Test-DoubleField -Text $script:tuningFields.MouseLeftYawExpo.Text -Min 0 -Max 1); Message = 'mouse yaw expo must be 0.0..1.0' },
        @{ Name = 'MouseLeftYawActualCenter'; Valid = (Test-DoubleField -Text $script:tuningFields.MouseLeftYawActualCenter.Text -Min 0 -Max 1); Message = 'mouse yaw actual center must be 0..1' },
        @{ Name = 'MouseLeftYawActualMax'; Valid = (Test-DoubleField -Text $script:tuningFields.MouseLeftYawActualMax.Text -Min 0 -Max 1); Message = 'mouse yaw actual max must be 0..1' },
        @{ Name = 'MouseLeftYawActualExpo'; Valid = (Test-DoubleField -Text $script:tuningFields.MouseLeftYawActualExpo.Text -Min 0 -Max 0.95); Message = 'mouse yaw actual expo must be 0..0.95' },
        @{ Name = 'MouseLeftYawPositionModel'; Valid = (Test-PositionModelField -Text $script:tuningFields.MouseLeftYawPositionModel.Text); Message = 'mouse yaw position model must be integrator or dynamic_gimbal' },
        @{ Name = 'MouseLeftYawGimbalFrequencyHz'; Valid = (Test-DoubleField -Text $script:tuningFields.MouseLeftYawGimbalFrequencyHz.Text -Min 0.1 -Max 80); Message = 'mouse yaw gimbal frequency must be 0.1..80 Hz' },
        @{ Name = 'MouseLeftYawGimbalDampingRatio'; Valid = (Test-DoubleField -Text $script:tuningFields.MouseLeftYawGimbalDampingRatio.Text -Min 0 -Max 5); Message = 'mouse yaw gimbal damping ratio must be 0..5' },
        @{ Name = 'MouseLeftYawGimbalInputImpulse'; Valid = (Test-DoubleField -Text $script:tuningFields.MouseLeftYawGimbalInputImpulse.Text -Min 0 -Max 5); Message = 'mouse yaw gimbal impulse must be 0..5' },
        @{ Name = 'MouseLeftYawGimbalStaticFriction'; Valid = (Test-DoubleField -Text $script:tuningFields.MouseLeftYawGimbalStaticFriction.Text -Min 0 -Max 128); Message = 'mouse yaw gimbal static friction must be 0..128' },
        @{ Name = 'MouseLeftYawGimbalDynamicFriction'; Valid = (Test-DoubleField -Text $script:tuningFields.MouseLeftYawGimbalDynamicFriction.Text -Min 0 -Max 100); Message = 'mouse yaw gimbal dynamic friction must be 0..100' },
        @{ Name = 'MouseLeftYawGimbalEdgeBumper'; Valid = (Test-DoubleField -Text $script:tuningFields.MouseLeftYawGimbalEdgeBumper.Text -Min 0 -Max 20); Message = 'mouse yaw gimbal edge bumper must be 0..20' },
        @{ Name = 'MouseLeftYawGimbalAntiwindupStart'; Valid = (Test-DoubleField -Text $script:tuningFields.MouseLeftYawGimbalAntiwindupStart.Text -Min 0 -Max 1 -MaxExclusive); Message = 'mouse yaw gimbal anti-windup start must be >=0 and <1' },
        @{ Name = 'MouseLeftYawGimbalAntiwindupMinGain'; Valid = (Test-DoubleField -Text $script:tuningFields.MouseLeftYawGimbalAntiwindupMinGain.Text -Min 0 -Max 1); Message = 'mouse yaw gimbal anti-windup min gain must be 0..1' },
        @{ Name = 'MouseLeftYawInputGainMode'; Valid = (Test-InputGainModeField -Text $script:tuningFields.MouseLeftYawInputGainMode.Text); Message = 'mouse yaw input gain mode must be flat or adaptive' },
        @{ Name = 'MouseLeftYawAdaptiveSlowGain'; Valid = (Test-DoubleField -Text $script:tuningFields.MouseLeftYawAdaptiveSlowGain.Text -Min 0 -Max 5); Message = 'mouse yaw adaptive slow gain must be 0..5' },
        @{ Name = 'MouseLeftYawAdaptiveFastGain'; Valid = (Test-DoubleField -Text $script:tuningFields.MouseLeftYawAdaptiveFastGain.Text -Min 0 -Max 5); Message = 'mouse yaw adaptive fast gain must be 0..5' },
        @{ Name = 'MouseLeftYawAdaptiveSpeedLow'; Valid = (Test-DoubleField -Text $script:tuningFields.MouseLeftYawAdaptiveSpeedLow.Text -Min 0 -Max 100000); Message = 'mouse yaw adaptive speed low must be 0..100000' },
        @{ Name = 'MouseLeftYawAdaptiveSpeedHigh'; Valid = (Test-DoubleField -Text $script:tuningFields.MouseLeftYawAdaptiveSpeedHigh.Text -Min 0 -Max 100000); Message = 'mouse yaw adaptive speed high must be 0..100000' },
        @{ Name = 'MouseLeftYawAdaptiveCurve'; Valid = (Test-DoubleField -Text $script:tuningFields.MouseLeftYawAdaptiveCurve.Text -Min 0.0001 -Max 5); Message = 'mouse yaw adaptive curve must be >0 and <=5' },
        @{ Name = 'MouseLeftYawAdaptiveTrackerMs'; Valid = (Test-DoubleField -Text $script:tuningFields.MouseLeftYawAdaptiveTrackerMs.Text -Min 0 -Max 1000); Message = 'mouse yaw adaptive tracker must be 0..1000 ms' },
        @{ Name = 'MouseLeftYawGateShape'; Valid = (Test-GateShapeField -Text $script:tuningFields.MouseLeftYawGateShape.Text); Message = 'mouse yaw gate shape must be axis, circle, octagon, or square' },
        @{ Name = 'MouseLeftYawDiagonalScale'; Valid = (Test-DoubleField -Text $script:tuningFields.MouseLeftYawDiagonalScale.Text -Min 0 -Max 1.5); Message = 'mouse yaw diagonal scale must be 0..1.5' },
        @{ Name = 'MouseLeftYawReturnRate'; Valid = (Test-DoubleField -Text $script:tuningFields.MouseLeftYawReturnRate.Text -Min 0 -Max 20000); Message = 'mouse yaw idle return must be 0..20000' },
        @{ Name = 'MouseLeftYawReturnIdle'; Valid = (Test-DoubleField -Text $script:tuningFields.MouseLeftYawReturnIdle.Text -Min 0 -Max 60000); Message = 'mouse yaw idle delay must be 0..60000 ms' },
        @{ Name = 'MouseLeftYawConstantReturnRate'; Valid = (Test-DoubleField -Text $script:tuningFields.MouseLeftYawConstantReturnRate.Text -Min 0 -Max 20000); Message = 'mouse yaw constant return must be 0..20000' },
        @{ Name = 'MouseLeftYawElasticReturnMode'; Valid = (Test-ElasticReturnModeField -Text $script:tuningFields.MouseLeftYawElasticReturnMode.Text); Message = 'mouse yaw elastic return mode must be linear, progressive, smoothstep, or expo' },
        @{ Name = 'MouseLeftYawElasticReturnCoefficient'; Valid = (Test-DoubleField -Text $script:tuningFields.MouseLeftYawElasticReturnCoefficient.Text -Min 0 -Max 100); Message = 'mouse yaw elastic return coefficient must be 0..100' },
        @{ Name = 'MouseLeftYawElasticReturnCurve'; Valid = (Test-DoubleField -Text $script:tuningFields.MouseLeftYawElasticReturnCurve.Text -Min 0 -Max 5); Message = 'mouse yaw elastic return curve must be 0..5' }
    )

    foreach ($check in $checks) {
        Mark-TuningField -Name $check.Name -Valid $check.Valid
        if (-not $check.Valid) {
            $status.Text = "Not saved: $($check.Message)."
            $ok = $false
        }
    }

    $reticleLimit = 0.0
    $aimDeadband = 0.0
    if ([double]::TryParse($script:tuningFields.AimReticleLimit.Text, [ref]$reticleLimit) -and
        [double]::TryParse($script:tuningFields.AimDeadband.Text, [ref]$aimDeadband)) {
        $aimDeadbandOk = ($aimDeadband -ge 0 -and $aimDeadband -lt $reticleLimit)
        Mark-TuningField -Name 'AimDeadband' -Valid $aimDeadbandOk
        if (-not $aimDeadbandOk) {
            $status.Text = 'Not saved: aim deadband must be below reticle limit.'
            $ok = $false
        }
    }

    $maxOutput = 0
    $deadband = 0
    if ([int]::TryParse($script:tuningFields.MaxOutput.Text, [ref]$maxOutput) -and
        [int]::TryParse($script:tuningFields.Deadband.Text, [ref]$deadband)) {
        $deadbandOk = ($deadband -ge 0 -and $deadband -lt $maxOutput)
        Mark-TuningField -Name 'Deadband' -Valid $deadbandOk
        if (-not $deadbandOk) {
            $status.Text = 'Not saved: deadband must be 0..max_output-1.'
            $ok = $false
        }
    } else {
        Mark-TuningField -Name 'Deadband' -Valid $false
        $status.Text = 'Not saved: deadband must be an integer.'
        $ok = $false
    }

    $adaptiveLow = 0.0
    $adaptiveHigh = 0.0
    if ([double]::TryParse($script:tuningFields.AdaptiveSpeedLow.Text, [ref]$adaptiveLow) -and
        [double]::TryParse($script:tuningFields.AdaptiveSpeedHigh.Text, [ref]$adaptiveHigh)) {
        $adaptiveSpeedsOk = ($adaptiveLow -ge 0.0 -and $adaptiveHigh -gt $adaptiveLow -and $adaptiveHigh -le 100000.0)
        Mark-TuningField -Name 'AdaptiveSpeedLow' -Valid $adaptiveSpeedsOk
        Mark-TuningField -Name 'AdaptiveSpeedHigh' -Valid $adaptiveSpeedsOk
        if (-not $adaptiveSpeedsOk) {
            $status.Text = 'Not saved: adaptive speed high must be greater than speed low.'
            $ok = $false
        }
    }

    $leftAdaptiveLow = 0.0
    $leftAdaptiveHigh = 0.0
    if ([double]::TryParse($script:tuningFields.MouseLeftYawAdaptiveSpeedLow.Text, [ref]$leftAdaptiveLow) -and
        [double]::TryParse($script:tuningFields.MouseLeftYawAdaptiveSpeedHigh.Text, [ref]$leftAdaptiveHigh)) {
        $leftAdaptiveSpeedsOk = ($leftAdaptiveLow -ge 0.0 -and $leftAdaptiveHigh -gt $leftAdaptiveLow -and $leftAdaptiveHigh -le 100000.0)
        Mark-TuningField -Name 'MouseLeftYawAdaptiveSpeedLow' -Valid $leftAdaptiveSpeedsOk
        Mark-TuningField -Name 'MouseLeftYawAdaptiveSpeedHigh' -Valid $leftAdaptiveSpeedsOk
        if (-not $leftAdaptiveSpeedsOk) {
            $status.Text = 'Not saved: mouse yaw adaptive speed high must be greater than speed low.'
            $ok = $false
        }
    }

    if ($script:tuningChecks.MouseLeftEnabled.Checked -and $script:tuningChecks.KeyboardEnabled.Checked) {
        $status.Text = 'Not saved: use either mouse left stick or keyboard left stick, not both.'
        $ok = $false
    }
    if ($script:tuningChecks.MouseLeftEnabled.Checked -and
        $script:tuningChecks.MouseLeftRequireDevice.Checked -and
        [string]::IsNullOrWhiteSpace($script:tuningFields.MouseDeviceLeft.Text)) {
        Mark-TuningField -Name 'MouseDeviceLeft' -Valid $false
        $status.Text = 'Not saved: mouse left stick requires auto or a left mouse root token.'
        $ok = $false
    } else {
        Mark-TuningField -Name 'MouseDeviceLeft' -Valid $true
    }

    return $ok
}

function Save-SelectedProfile {
    if ($script:loadingProfile) {
        return
    }

    $profile = $script:editingProfile
    if ($null -eq $profile) {
        return
    }

    if (-not (Validate-TuningControls)) {
        return
    }

    $lines = [string[]](Get-Content -LiteralPath $profile.FullName)
    $updates = @(
        @{ Section = 'trainer'; Key = 'frame_rate_hz'; Value = $script:tuningFields.FrameRate.Text.Trim() },
        @{ Section = 'safety'; Key = 'stop_key'; Value = ('"{0}"' -f $script:tuningFields.StopKey.Text.Trim()) },
        @{ Section = 'safety'; Key = 'freeze_key'; Value = ('"{0}"' -f $script:tuningFields.FreezeKey.Text.Trim()) },
        @{ Section = 'mapper'; Key = 'roll_gain'; Value = $script:tuningFields.RollGain.Text.Trim() },
        @{ Section = 'mapper'; Key = 'pitch_gain'; Value = $script:tuningFields.PitchGain.Text.Trim() },
        @{ Section = 'mapper'; Key = 'max_output'; Value = $script:tuningFields.MaxOutput.Text.Trim() },
        @{ Section = 'mapper'; Key = 'deadband'; Value = $script:tuningFields.Deadband.Text.Trim() },
        @{ Section = 'mapper'; Key = 'expo'; Value = $script:tuningFields.Expo.Text.Trim() },
        @{ Section = 'mapper'; Key = 'smoothing'; Value = $script:tuningFields.Smoothing.Text.Trim() },
        @{ Section = 'mapper'; Key = 'input_filter'; Value = ('"{0}"' -f $script:tuningFields.InputFilter.Text.Trim()) },
        @{ Section = 'mapper'; Key = 'one_euro_min_cutoff_hz'; Value = $script:tuningFields.OneEuroMinCutoffHz.Text.Trim() },
        @{ Section = 'mapper'; Key = 'one_euro_beta'; Value = $script:tuningFields.OneEuroBeta.Text.Trim() },
        @{ Section = 'mapper'; Key = 'one_euro_dcutoff_hz'; Value = $script:tuningFields.OneEuroDcutoffHz.Text.Trim() },
        @{ Section = 'mapper'; Key = 'despike_enabled'; Value = (ConvertTo-TomlBool $script:tuningChecks.DespikeEnabled.Checked) },
        @{ Section = 'mapper'; Key = 'despike_count_enabled'; Value = (ConvertTo-TomlBool $script:tuningChecks.DespikeCountEnabled.Checked) },
        @{ Section = 'mapper'; Key = 'despike_window'; Value = $script:tuningFields.DespikeWindow.Text.Trim() },
        @{ Section = 'mapper'; Key = 'despike_threshold_sigma'; Value = $script:tuningFields.DespikeThresholdSigma.Text.Trim() },
        @{ Section = 'mapper'; Key = 'output_curve'; Value = ('"{0}"' -f $script:tuningFields.OutputCurve.Text.Trim()) },
        @{ Section = 'mapper'; Key = 'actual_center'; Value = $script:tuningFields.ActualCenter.Text.Trim() },
        @{ Section = 'mapper'; Key = 'actual_max'; Value = $script:tuningFields.ActualMax.Text.Trim() },
        @{ Section = 'mapper'; Key = 'actual_expo'; Value = $script:tuningFields.ActualExpo.Text.Trim() },
        @{ Section = 'mapper'; Key = 'position_model'; Value = ('"{0}"' -f $script:tuningFields.PositionModel.Text.Trim()) },
        @{ Section = 'mapper'; Key = 'gimbal_frequency_hz'; Value = $script:tuningFields.GimbalFrequencyHz.Text.Trim() },
        @{ Section = 'mapper'; Key = 'gimbal_damping_ratio'; Value = $script:tuningFields.GimbalDampingRatio.Text.Trim() },
        @{ Section = 'mapper'; Key = 'gimbal_input_impulse'; Value = $script:tuningFields.GimbalInputImpulse.Text.Trim() },
        @{ Section = 'mapper'; Key = 'gimbal_static_friction'; Value = $script:tuningFields.GimbalStaticFriction.Text.Trim() },
        @{ Section = 'mapper'; Key = 'gimbal_dynamic_friction'; Value = $script:tuningFields.GimbalDynamicFriction.Text.Trim() },
        @{ Section = 'mapper'; Key = 'gimbal_edge_bumper'; Value = $script:tuningFields.GimbalEdgeBumper.Text.Trim() },
        @{ Section = 'mapper'; Key = 'gimbal_antiwindup_enabled'; Value = (ConvertTo-TomlBool $script:tuningChecks.GimbalAntiwindupEnabled.Checked) },
        @{ Section = 'mapper'; Key = 'gimbal_antiwindup_start'; Value = $script:tuningFields.GimbalAntiwindupStart.Text.Trim() },
        @{ Section = 'mapper'; Key = 'gimbal_antiwindup_min_gain'; Value = $script:tuningFields.GimbalAntiwindupMinGain.Text.Trim() },
        @{ Section = 'mapper'; Key = 'input_gain_mode'; Value = ('"{0}"' -f $script:tuningFields.InputGainMode.Text.Trim()) },
        @{ Section = 'mapper'; Key = 'adaptive_slow_gain'; Value = $script:tuningFields.AdaptiveSlowGain.Text.Trim() },
        @{ Section = 'mapper'; Key = 'adaptive_fast_gain'; Value = $script:tuningFields.AdaptiveFastGain.Text.Trim() },
        @{ Section = 'mapper'; Key = 'adaptive_speed_low'; Value = $script:tuningFields.AdaptiveSpeedLow.Text.Trim() },
        @{ Section = 'mapper'; Key = 'adaptive_speed_high'; Value = $script:tuningFields.AdaptiveSpeedHigh.Text.Trim() },
        @{ Section = 'mapper'; Key = 'adaptive_curve'; Value = $script:tuningFields.AdaptiveCurve.Text.Trim() },
        @{ Section = 'mapper'; Key = 'adaptive_tracker_ms'; Value = $script:tuningFields.AdaptiveTrackerMs.Text.Trim() },
        @{ Section = 'mapper'; Key = 'gate_shape'; Value = ('"{0}"' -f $script:tuningFields.GateShape.Text.Trim()) },
        @{ Section = 'mapper'; Key = 'diagonal_scale'; Value = $script:tuningFields.DiagonalScale.Text.Trim() },
        @{ Section = 'mapper'; Key = 'return_enabled'; Value = (ConvertTo-TomlBool $script:tuningChecks.ReturnEnabled.Checked) },
        @{ Section = 'mapper'; Key = 'return_rate'; Value = $script:tuningFields.ReturnRate.Text.Trim() },
        @{ Section = 'mapper'; Key = 'return_idle_ms'; Value = $script:tuningFields.ReturnIdle.Text.Trim() },
        @{ Section = 'mapper'; Key = 'constant_return_enabled'; Value = (ConvertTo-TomlBool $script:tuningChecks.ConstantReturnEnabled.Checked) },
        @{ Section = 'mapper'; Key = 'constant_return_rate'; Value = $script:tuningFields.ConstantReturnRate.Text.Trim() },
        @{ Section = 'mapper'; Key = 'elastic_return_enabled'; Value = (ConvertTo-TomlBool $script:tuningChecks.ElasticReturnEnabled.Checked) },
        @{ Section = 'mapper'; Key = 'elastic_return_mode'; Value = ('"{0}"' -f $script:tuningFields.ElasticReturnMode.Text.Trim()) },
        @{ Section = 'mapper'; Key = 'elastic_return_coefficient'; Value = $script:tuningFields.ElasticReturnCoefficient.Text.Trim() },
        @{ Section = 'mapper'; Key = 'elastic_return_curve'; Value = $script:tuningFields.ElasticReturnCurve.Text.Trim() },
        @{ Section = 'mapper'; Key = 'output_shaping_enabled'; Value = (ConvertTo-TomlBool $script:tuningChecks.OutputShapingEnabled.Checked) },
        @{ Section = 'mapper'; Key = 'return_shaping_enabled'; Value = (ConvertTo-TomlBool $script:tuningChecks.ReturnShapingEnabled.Checked) },
        @{ Section = 'mapper'; Key = 'invert_roll'; Value = (ConvertTo-TomlBool $script:tuningChecks.InvertRoll.Checked) },
        @{ Section = 'mapper'; Key = 'invert_pitch'; Value = (ConvertTo-TomlBool $script:tuningChecks.InvertPitch.Checked) },
        @{ Section = 'mapper'; Key = 'swap_axes'; Value = (ConvertTo-TomlBool $script:tuningChecks.SwapAxes.Checked) },
        @{ Section = 'mouse_right_stick'; Key = 'enabled'; Value = (ConvertTo-TomlBool $script:tuningChecks.MouseRightStickEnabled.Checked) },
        @{ Section = 'mouse_devices'; Key = 'right'; Value = ('"{0}"' -f $script:tuningFields.MouseDeviceRight.Text.Trim()) },
        @{ Section = 'mouse_devices'; Key = 'left'; Value = ('"{0}"' -f $script:tuningFields.MouseDeviceLeft.Text.Trim()) },
        @{ Section = 'mouse_left_stick'; Key = 'enabled'; Value = (ConvertTo-TomlBool $script:tuningChecks.MouseLeftEnabled.Checked) },
        @{ Section = 'mouse_left_stick'; Key = 'require_device'; Value = (ConvertTo-TomlBool $script:tuningChecks.MouseLeftRequireDevice.Checked) },
        @{ Section = 'mouse_left_stick'; Key = 'throttle_rate'; Value = $script:tuningFields.MouseLeftThrottleRate.Text.Trim() },
        @{ Section = 'mouse_left_stick'; Key = 'throttle_return_enabled'; Value = (ConvertTo-TomlBool $script:tuningChecks.MouseLeftThrottleReturnEnabled.Checked) },
        @{ Section = 'mouse_left_stick'; Key = 'throttle_return_rate'; Value = $script:tuningFields.MouseLeftThrottleReturnRate.Text.Trim() },
        @{ Section = 'mouse_left_stick'; Key = 'yaw_gain'; Value = $script:tuningFields.MouseLeftYawGain.Text.Trim() },
        @{ Section = 'mouse_left_stick'; Key = 'yaw_pulse'; Value = $script:tuningFields.MouseLeftYawMax.Text.Trim() },
        @{ Section = 'mouse_left_stick'; Key = 'yaw_deadband'; Value = $script:tuningFields.MouseLeftYawDeadband.Text.Trim() },
        @{ Section = 'mouse_left_stick'; Key = 'yaw_smoothing'; Value = $script:tuningFields.MouseLeftYawSmoothing.Text.Trim() },
        @{ Section = 'mouse_left_stick'; Key = 'yaw_slew_rate'; Value = $script:tuningFields.MouseLeftYawSlewRate.Text.Trim() },
        @{ Section = 'mouse_left_stick'; Key = 'yaw_shaping_enabled'; Value = (ConvertTo-TomlBool $script:tuningChecks.MouseLeftYawShapingEnabled.Checked) },
        @{ Section = 'mouse_left_stick'; Key = 'yaw_input_filter'; Value = ('"{0}"' -f $script:tuningFields.MouseLeftYawInputFilter.Text.Trim()) },
        @{ Section = 'mouse_left_stick'; Key = 'yaw_one_euro_min_cutoff_hz'; Value = $script:tuningFields.MouseLeftYawOneEuroMinCutoffHz.Text.Trim() },
        @{ Section = 'mouse_left_stick'; Key = 'yaw_one_euro_beta'; Value = $script:tuningFields.MouseLeftYawOneEuroBeta.Text.Trim() },
        @{ Section = 'mouse_left_stick'; Key = 'yaw_one_euro_dcutoff_hz'; Value = $script:tuningFields.MouseLeftYawOneEuroDcutoffHz.Text.Trim() },
        @{ Section = 'mouse_left_stick'; Key = 'yaw_despike_enabled'; Value = (ConvertTo-TomlBool $script:tuningChecks.MouseLeftYawDespikeEnabled.Checked) },
        @{ Section = 'mouse_left_stick'; Key = 'yaw_despike_count_enabled'; Value = (ConvertTo-TomlBool $script:tuningChecks.MouseLeftYawDespikeCountEnabled.Checked) },
        @{ Section = 'mouse_left_stick'; Key = 'yaw_despike_window'; Value = $script:tuningFields.MouseLeftYawDespikeWindow.Text.Trim() },
        @{ Section = 'mouse_left_stick'; Key = 'yaw_despike_threshold_sigma'; Value = $script:tuningFields.MouseLeftYawDespikeThresholdSigma.Text.Trim() },
        @{ Section = 'mouse_left_stick'; Key = 'yaw_output_curve'; Value = ('"{0}"' -f $script:tuningFields.MouseLeftYawOutputCurve.Text.Trim()) },
        @{ Section = 'mouse_left_stick'; Key = 'yaw_expo'; Value = $script:tuningFields.MouseLeftYawExpo.Text.Trim() },
        @{ Section = 'mouse_left_stick'; Key = 'yaw_actual_center'; Value = $script:tuningFields.MouseLeftYawActualCenter.Text.Trim() },
        @{ Section = 'mouse_left_stick'; Key = 'yaw_actual_max'; Value = $script:tuningFields.MouseLeftYawActualMax.Text.Trim() },
        @{ Section = 'mouse_left_stick'; Key = 'yaw_actual_expo'; Value = $script:tuningFields.MouseLeftYawActualExpo.Text.Trim() },
        @{ Section = 'mouse_left_stick'; Key = 'yaw_position_model'; Value = ('"{0}"' -f $script:tuningFields.MouseLeftYawPositionModel.Text.Trim()) },
        @{ Section = 'mouse_left_stick'; Key = 'yaw_gimbal_frequency_hz'; Value = $script:tuningFields.MouseLeftYawGimbalFrequencyHz.Text.Trim() },
        @{ Section = 'mouse_left_stick'; Key = 'yaw_gimbal_damping_ratio'; Value = $script:tuningFields.MouseLeftYawGimbalDampingRatio.Text.Trim() },
        @{ Section = 'mouse_left_stick'; Key = 'yaw_gimbal_input_impulse'; Value = $script:tuningFields.MouseLeftYawGimbalInputImpulse.Text.Trim() },
        @{ Section = 'mouse_left_stick'; Key = 'yaw_gimbal_static_friction'; Value = $script:tuningFields.MouseLeftYawGimbalStaticFriction.Text.Trim() },
        @{ Section = 'mouse_left_stick'; Key = 'yaw_gimbal_dynamic_friction'; Value = $script:tuningFields.MouseLeftYawGimbalDynamicFriction.Text.Trim() },
        @{ Section = 'mouse_left_stick'; Key = 'yaw_gimbal_edge_bumper'; Value = $script:tuningFields.MouseLeftYawGimbalEdgeBumper.Text.Trim() },
        @{ Section = 'mouse_left_stick'; Key = 'yaw_gimbal_antiwindup_enabled'; Value = (ConvertTo-TomlBool $script:tuningChecks.MouseLeftYawGimbalAntiwindupEnabled.Checked) },
        @{ Section = 'mouse_left_stick'; Key = 'yaw_gimbal_antiwindup_start'; Value = $script:tuningFields.MouseLeftYawGimbalAntiwindupStart.Text.Trim() },
        @{ Section = 'mouse_left_stick'; Key = 'yaw_gimbal_antiwindup_min_gain'; Value = $script:tuningFields.MouseLeftYawGimbalAntiwindupMinGain.Text.Trim() },
        @{ Section = 'mouse_left_stick'; Key = 'yaw_input_gain_mode'; Value = ('"{0}"' -f $script:tuningFields.MouseLeftYawInputGainMode.Text.Trim()) },
        @{ Section = 'mouse_left_stick'; Key = 'yaw_adaptive_slow_gain'; Value = $script:tuningFields.MouseLeftYawAdaptiveSlowGain.Text.Trim() },
        @{ Section = 'mouse_left_stick'; Key = 'yaw_adaptive_fast_gain'; Value = $script:tuningFields.MouseLeftYawAdaptiveFastGain.Text.Trim() },
        @{ Section = 'mouse_left_stick'; Key = 'yaw_adaptive_speed_low'; Value = $script:tuningFields.MouseLeftYawAdaptiveSpeedLow.Text.Trim() },
        @{ Section = 'mouse_left_stick'; Key = 'yaw_adaptive_speed_high'; Value = $script:tuningFields.MouseLeftYawAdaptiveSpeedHigh.Text.Trim() },
        @{ Section = 'mouse_left_stick'; Key = 'yaw_adaptive_curve'; Value = $script:tuningFields.MouseLeftYawAdaptiveCurve.Text.Trim() },
        @{ Section = 'mouse_left_stick'; Key = 'yaw_adaptive_tracker_ms'; Value = $script:tuningFields.MouseLeftYawAdaptiveTrackerMs.Text.Trim() },
        @{ Section = 'mouse_left_stick'; Key = 'yaw_gate_shape'; Value = ('"{0}"' -f $script:tuningFields.MouseLeftYawGateShape.Text.Trim()) },
        @{ Section = 'mouse_left_stick'; Key = 'yaw_diagonal_scale'; Value = $script:tuningFields.MouseLeftYawDiagonalScale.Text.Trim() },
        @{ Section = 'mouse_left_stick'; Key = 'yaw_return_enabled'; Value = (ConvertTo-TomlBool $script:tuningChecks.MouseLeftYawReturnEnabled.Checked) },
        @{ Section = 'mouse_left_stick'; Key = 'yaw_return_rate'; Value = $script:tuningFields.MouseLeftYawReturnRate.Text.Trim() },
        @{ Section = 'mouse_left_stick'; Key = 'yaw_return_idle_ms'; Value = $script:tuningFields.MouseLeftYawReturnIdle.Text.Trim() },
        @{ Section = 'mouse_left_stick'; Key = 'yaw_constant_return_enabled'; Value = (ConvertTo-TomlBool $script:tuningChecks.MouseLeftYawConstantReturnEnabled.Checked) },
        @{ Section = 'mouse_left_stick'; Key = 'yaw_constant_return_rate'; Value = $script:tuningFields.MouseLeftYawConstantReturnRate.Text.Trim() },
        @{ Section = 'mouse_left_stick'; Key = 'yaw_elastic_return_enabled'; Value = (ConvertTo-TomlBool $script:tuningChecks.MouseLeftYawElasticReturnEnabled.Checked) },
        @{ Section = 'mouse_left_stick'; Key = 'yaw_elastic_return_mode'; Value = ('"{0}"' -f $script:tuningFields.MouseLeftYawElasticReturnMode.Text.Trim()) },
        @{ Section = 'mouse_left_stick'; Key = 'yaw_elastic_return_coefficient'; Value = $script:tuningFields.MouseLeftYawElasticReturnCoefficient.Text.Trim() },
        @{ Section = 'mouse_left_stick'; Key = 'yaw_elastic_return_curve'; Value = $script:tuningFields.MouseLeftYawElasticReturnCurve.Text.Trim() },
        @{ Section = 'mouse_left_stick'; Key = 'yaw_output_shaping_enabled'; Value = (ConvertTo-TomlBool $script:tuningChecks.MouseLeftYawOutputShapingEnabled.Checked) },
        @{ Section = 'mouse_left_stick'; Key = 'yaw_return_shaping_enabled'; Value = (ConvertTo-TomlBool $script:tuningChecks.MouseLeftYawReturnShapingEnabled.Checked) },
        @{ Section = 'mouse_left_stick'; Key = 'invert_throttle'; Value = (ConvertTo-TomlBool $script:tuningChecks.MouseLeftInvertThrottle.Checked) },
        @{ Section = 'mouse_left_stick'; Key = 'invert_yaw'; Value = (ConvertTo-TomlBool $script:tuningChecks.MouseLeftInvertYaw.Checked) },
        @{ Section = 'mouse_left_stick'; Key = 'swap_axes'; Value = (ConvertTo-TomlBool $script:tuningChecks.MouseLeftSwapAxes.Checked) },
        @{ Section = 'keyboard_left_stick'; Key = 'enabled'; Value = (ConvertTo-TomlBool $script:tuningChecks.KeyboardEnabled.Checked) },
        @{ Section = 'keyboard_left_stick'; Key = 'block_selected_keys'; Value = (ConvertTo-TomlBool $script:tuningChecks.BlockSelectedKeys.Checked) },
        @{ Section = 'keyboard_left_stick'; Key = 'throttle_up_key'; Value = ('"{0}"' -f $script:tuningFields.ThrottleUpKey.Text.Trim()) },
        @{ Section = 'keyboard_left_stick'; Key = 'throttle_down_key'; Value = ('"{0}"' -f $script:tuningFields.ThrottleDownKey.Text.Trim()) },
        @{ Section = 'keyboard_left_stick'; Key = 'yaw_left_key'; Value = ('"{0}"' -f $script:tuningFields.YawLeftKey.Text.Trim()) },
        @{ Section = 'keyboard_left_stick'; Key = 'yaw_right_key'; Value = ('"{0}"' -f $script:tuningFields.YawRightKey.Text.Trim()) },
        @{ Section = 'keyboard_left_stick'; Key = 'throttle_cut_key'; Value = ('"{0}"' -f $script:tuningFields.ThrottleCutKey.Text.Trim()) },
        @{ Section = 'keyboard_left_stick'; Key = 'throttle_rate'; Value = $script:tuningFields.ThrottleRate.Text.Trim() },
        @{ Section = 'keyboard_left_stick'; Key = 'throttle_return_enabled'; Value = (ConvertTo-TomlBool $script:tuningChecks.ThrottleReturnEnabled.Checked) },
        @{ Section = 'keyboard_left_stick'; Key = 'throttle_return_rate'; Value = $script:tuningFields.ThrottleReturnRate.Text.Trim() },
        @{ Section = 'keyboard_left_stick'; Key = 'yaw_pulse'; Value = $script:tuningFields.YawPulse.Text.Trim() },
        @{ Section = 'keyboard_left_stick'; Key = 'yaw_slew_rate'; Value = $script:tuningFields.YawSlewRate.Text.Trim() },
        @{ Section = 'keyboard_left_stick'; Key = 'invert_yaw'; Value = (ConvertTo-TomlBool $script:tuningChecks.InvertYaw.Checked) }
    )

    $warThunderMode = $script:tuningChecks.WarThunderMode.Checked
    $profileHadControlMode = ((Get-TomlValue -Lines $lines -Section 'control' -Key 'mode' -Default '__missing__') -ne '__missing__')

    if ($warThunderMode) {
        $updates += @(
            @{ Section = 'control'; Key = 'mode'; Value = '"drone_mouse_aim"' }
        )
        $updates += @(
            @{ Section = 'mouse_aim'; Key = 'sensitivity_x'; Value = $script:tuningFields.AimSensitivityX.Text.Trim() },
            @{ Section = 'mouse_aim'; Key = 'sensitivity_y'; Value = $script:tuningFields.AimSensitivityY.Text.Trim() },
            @{ Section = 'mouse_aim'; Key = 'reticle_limit'; Value = $script:tuningFields.AimReticleLimit.Text.Trim() },
            @{ Section = 'mouse_aim'; Key = 'reticle_deadband'; Value = $script:tuningFields.AimDeadband.Text.Trim() },
            @{ Section = 'mouse_aim'; Key = 'reticle_return_rate'; Value = $script:tuningFields.AimReturnRate.Text.Trim() },
            @{ Section = 'mouse_aim'; Key = 'output_smoothing'; Value = $script:tuningFields.AimSmoothing.Text.Trim() },
            @{ Section = 'mouse_aim'; Key = 'roll_gain'; Value = $script:tuningFields.AimRollGain.Text.Trim() },
            @{ Section = 'mouse_aim'; Key = 'yaw_gain'; Value = $script:tuningFields.AimYawGain.Text.Trim() },
            @{ Section = 'mouse_aim'; Key = 'pitch_gain'; Value = $script:tuningFields.AimPitchGain.Text.Trim() },
            @{ Section = 'mouse_aim'; Key = 'roll_max'; Value = $script:tuningFields.AimRollMax.Text.Trim() },
            @{ Section = 'mouse_aim'; Key = 'yaw_max'; Value = $script:tuningFields.AimYawMax.Text.Trim() },
            @{ Section = 'mouse_aim'; Key = 'pitch_max'; Value = $script:tuningFields.AimPitchMax.Text.Trim() },
            @{ Section = 'mouse_aim'; Key = 'slew_rate'; Value = $script:tuningFields.AimSlewRate.Text.Trim() },
            @{ Section = 'mouse_aim'; Key = 'invert_x'; Value = (ConvertTo-TomlBool $script:tuningChecks.AimInvertX.Checked) },
            @{ Section = 'mouse_aim'; Key = 'invert_y'; Value = (ConvertTo-TomlBool $script:tuningChecks.AimInvertY.Checked) }
        )
    } elseif ($profileHadControlMode) {
        $updates += @(
            @{ Section = 'control'; Key = 'mode'; Value = '"direct_mouse"' }
        )
    }

    foreach ($update in $updates) {
        $lines = Set-TomlValue -Lines $lines -Section $update.Section -Key $update.Key -Value $update.Value
    }

    $outputNodes = Get-StickShapeEditorNodes -Name 'OutputShape'
    $returnNodes = Get-StickShapeEditorNodes -Name 'ReturnShape'
    $mouseLeftYawOutputNodes = Get-StickShapeEditorNodes -Name 'MouseLeftYawOutputShape'
    $mouseLeftYawReturnNodes = Get-StickShapeEditorNodes -Name 'MouseLeftYawReturnShape'
    $outputNodesText = ConvertTo-StickShapeNodesText -Nodes $outputNodes
    $returnNodesText = ConvertTo-StickShapeNodesText -Nodes $returnNodes
    $mouseLeftYawOutputNodesText = ConvertTo-StickShapeNodesText -Nodes $mouseLeftYawOutputNodes
    $mouseLeftYawReturnNodesText = ConvertTo-StickShapeNodesText -Nodes $mouseLeftYawReturnNodes
    $lines = Set-TomlArrayValue -Lines $lines -Section 'mapper' -Key 'output_shape_nodes' -Value $outputNodesText
    $lines = Set-TomlArrayValue -Lines $lines -Section 'mapper' -Key 'return_shape_nodes' -Value $returnNodesText
    $lines = Set-TomlArrayValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_output_shape_nodes' -Value $mouseLeftYawOutputNodesText
    $lines = Set-TomlArrayValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_return_shape_nodes' -Value $mouseLeftYawReturnNodesText

    Set-Content -LiteralPath $profile.FullName -Value $lines -Encoding ASCII

    $profile.FrameRate = $script:tuningFields.FrameRate.Text.Trim()
    $profile.StopKey = $script:tuningFields.StopKey.Text.Trim()
    $profile.FreezeKey = $script:tuningFields.FreezeKey.Text.Trim()
    $profile.RollGain = $script:tuningFields.RollGain.Text.Trim()
    $profile.PitchGain = $script:tuningFields.PitchGain.Text.Trim()
    $profile.MaxOutput = $script:tuningFields.MaxOutput.Text.Trim()
    $profile.Deadband = $script:tuningFields.Deadband.Text.Trim()
    $profile.Expo = $script:tuningFields.Expo.Text.Trim()
    $profile.Smoothing = $script:tuningFields.Smoothing.Text.Trim()
    $profile.InputFilter = $script:tuningFields.InputFilter.Text.Trim()
    $profile.OneEuroMinCutoffHz = $script:tuningFields.OneEuroMinCutoffHz.Text.Trim()
    $profile.OneEuroBeta = $script:tuningFields.OneEuroBeta.Text.Trim()
    $profile.OneEuroDcutoffHz = $script:tuningFields.OneEuroDcutoffHz.Text.Trim()
    $profile.DespikeEnabled = ConvertTo-TomlBool $script:tuningChecks.DespikeEnabled.Checked
    if ($script:tuningChecks.ContainsKey('DespikeCountEnabled')) {
        $profile.DespikeCountEnabled = ConvertTo-TomlBool $script:tuningChecks.DespikeCountEnabled.Checked
    }
    $profile.DespikeWindow = $script:tuningFields.DespikeWindow.Text.Trim()
    $profile.DespikeThresholdSigma = $script:tuningFields.DespikeThresholdSigma.Text.Trim()
    $profile.OutputCurve = $script:tuningFields.OutputCurve.Text.Trim()
    $profile.ActualCenter = $script:tuningFields.ActualCenter.Text.Trim()
    $profile.ActualMax = $script:tuningFields.ActualMax.Text.Trim()
    $profile.ActualExpo = $script:tuningFields.ActualExpo.Text.Trim()
    $profile.PositionModel = $script:tuningFields.PositionModel.Text.Trim()
    $profile.GimbalFrequencyHz = $script:tuningFields.GimbalFrequencyHz.Text.Trim()
    $profile.GimbalDampingRatio = $script:tuningFields.GimbalDampingRatio.Text.Trim()
    $profile.GimbalInputImpulse = $script:tuningFields.GimbalInputImpulse.Text.Trim()
    $profile.GimbalStaticFriction = $script:tuningFields.GimbalStaticFriction.Text.Trim()
    $profile.GimbalDynamicFriction = $script:tuningFields.GimbalDynamicFriction.Text.Trim()
    $profile.GimbalEdgeBumper = $script:tuningFields.GimbalEdgeBumper.Text.Trim()
    $profile.GimbalAntiwindupEnabled = ConvertTo-TomlBool $script:tuningChecks.GimbalAntiwindupEnabled.Checked
    $profile.GimbalAntiwindupStart = $script:tuningFields.GimbalAntiwindupStart.Text.Trim()
    $profile.GimbalAntiwindupMinGain = $script:tuningFields.GimbalAntiwindupMinGain.Text.Trim()
    $profile.InputGainMode = $script:tuningFields.InputGainMode.Text.Trim()
    $profile.AdaptiveSlowGain = $script:tuningFields.AdaptiveSlowGain.Text.Trim()
    $profile.AdaptiveFastGain = $script:tuningFields.AdaptiveFastGain.Text.Trim()
    $profile.AdaptiveSpeedLow = $script:tuningFields.AdaptiveSpeedLow.Text.Trim()
    $profile.AdaptiveSpeedHigh = $script:tuningFields.AdaptiveSpeedHigh.Text.Trim()
    $profile.AdaptiveCurve = $script:tuningFields.AdaptiveCurve.Text.Trim()
    $profile.AdaptiveTrackerMs = $script:tuningFields.AdaptiveTrackerMs.Text.Trim()
    $profile.GateShape = $script:tuningFields.GateShape.Text.Trim()
    $profile.DiagonalScale = $script:tuningFields.DiagonalScale.Text.Trim()
    $profile.ReturnEnabled = ConvertTo-TomlBool $script:tuningChecks.ReturnEnabled.Checked
    $profile.ReturnRate = $script:tuningFields.ReturnRate.Text.Trim()
    $profile.ReturnIdle = $script:tuningFields.ReturnIdle.Text.Trim()
    $profile.ConstantReturnEnabled = ConvertTo-TomlBool $script:tuningChecks.ConstantReturnEnabled.Checked
    $profile.ConstantReturnRate = $script:tuningFields.ConstantReturnRate.Text.Trim()
    $profile.ElasticReturnEnabled = ConvertTo-TomlBool $script:tuningChecks.ElasticReturnEnabled.Checked
    $profile.ElasticReturnMode = $script:tuningFields.ElasticReturnMode.Text.Trim()
    $profile.ElasticReturnCoefficient = $script:tuningFields.ElasticReturnCoefficient.Text.Trim()
    $profile.ElasticReturnCurve = $script:tuningFields.ElasticReturnCurve.Text.Trim()
    $profile.OutputShapingEnabled = ConvertTo-TomlBool $script:tuningChecks.OutputShapingEnabled.Checked
    $profile.OutputShapeNodesText = $outputNodesText
    $profile.ReturnShapingEnabled = ConvertTo-TomlBool $script:tuningChecks.ReturnShapingEnabled.Checked
    $profile.ReturnShapeNodesText = $returnNodesText
    $profile.InvertRoll = ConvertTo-TomlBool $script:tuningChecks.InvertRoll.Checked
    $profile.InvertPitch = ConvertTo-TomlBool $script:tuningChecks.InvertPitch.Checked
    $profile.SwapAxes = ConvertTo-TomlBool $script:tuningChecks.SwapAxes.Checked
    $profile.ControlMode = $(if ($script:tuningChecks.WarThunderMode.Checked) { 'drone_mouse_aim' } else { 'direct_mouse' })
    $profile.AimSensitivityX = $script:tuningFields.AimSensitivityX.Text.Trim()
    $profile.AimSensitivityY = $script:tuningFields.AimSensitivityY.Text.Trim()
    $profile.AimReticleLimit = $script:tuningFields.AimReticleLimit.Text.Trim()
    $profile.AimDeadband = $script:tuningFields.AimDeadband.Text.Trim()
    $profile.AimReturnRate = $script:tuningFields.AimReturnRate.Text.Trim()
    $profile.AimSmoothing = $script:tuningFields.AimSmoothing.Text.Trim()
    $profile.AimRollGain = $script:tuningFields.AimRollGain.Text.Trim()
    $profile.AimYawGain = $script:tuningFields.AimYawGain.Text.Trim()
    $profile.AimPitchGain = $script:tuningFields.AimPitchGain.Text.Trim()
    $profile.AimRollMax = $script:tuningFields.AimRollMax.Text.Trim()
    $profile.AimYawMax = $script:tuningFields.AimYawMax.Text.Trim()
    $profile.AimPitchMax = $script:tuningFields.AimPitchMax.Text.Trim()
    $profile.AimSlewRate = $script:tuningFields.AimSlewRate.Text.Trim()
    $profile.AimInvertX = ConvertTo-TomlBool $script:tuningChecks.AimInvertX.Checked
    $profile.AimInvertY = ConvertTo-TomlBool $script:tuningChecks.AimInvertY.Checked
    $profile.MouseRightStickEnabled = ConvertTo-TomlBool $script:tuningChecks.MouseRightStickEnabled.Checked
    $profile.MouseDeviceRight = $script:tuningFields.MouseDeviceRight.Text.Trim()
    $profile.MouseDeviceLeft = $script:tuningFields.MouseDeviceLeft.Text.Trim()
    $profile.MouseLeftEnabled = ConvertTo-TomlBool $script:tuningChecks.MouseLeftEnabled.Checked
    $profile.MouseLeftRequireDevice = ConvertTo-TomlBool $script:tuningChecks.MouseLeftRequireDevice.Checked
    $profile.MouseLeftThrottleRate = $script:tuningFields.MouseLeftThrottleRate.Text.Trim()
    $profile.MouseLeftThrottleReturnEnabled = ConvertTo-TomlBool $script:tuningChecks.MouseLeftThrottleReturnEnabled.Checked
    $profile.MouseLeftThrottleReturnRate = $script:tuningFields.MouseLeftThrottleReturnRate.Text.Trim()
    $profile.MouseLeftYawGain = $script:tuningFields.MouseLeftYawGain.Text.Trim()
    $profile.MouseLeftYawMax = $script:tuningFields.MouseLeftYawMax.Text.Trim()
    $profile.MouseLeftYawDeadband = $script:tuningFields.MouseLeftYawDeadband.Text.Trim()
    $profile.MouseLeftYawSmoothing = $script:tuningFields.MouseLeftYawSmoothing.Text.Trim()
    $profile.MouseLeftYawSlewRate = $script:tuningFields.MouseLeftYawSlewRate.Text.Trim()
    $profile.MouseLeftYawShapingEnabled = ConvertTo-TomlBool $script:tuningChecks.MouseLeftYawShapingEnabled.Checked
    $profile.MouseLeftYawInputFilter = $script:tuningFields.MouseLeftYawInputFilter.Text.Trim()
    $profile.MouseLeftYawOneEuroMinCutoffHz = $script:tuningFields.MouseLeftYawOneEuroMinCutoffHz.Text.Trim()
    $profile.MouseLeftYawOneEuroBeta = $script:tuningFields.MouseLeftYawOneEuroBeta.Text.Trim()
    $profile.MouseLeftYawOneEuroDcutoffHz = $script:tuningFields.MouseLeftYawOneEuroDcutoffHz.Text.Trim()
    $profile.MouseLeftYawDespikeEnabled = ConvertTo-TomlBool $script:tuningChecks.MouseLeftYawDespikeEnabled.Checked
    $profile.MouseLeftYawDespikeCountEnabled = ConvertTo-TomlBool $script:tuningChecks.MouseLeftYawDespikeCountEnabled.Checked
    $profile.MouseLeftYawDespikeWindow = $script:tuningFields.MouseLeftYawDespikeWindow.Text.Trim()
    $profile.MouseLeftYawDespikeThresholdSigma = $script:tuningFields.MouseLeftYawDespikeThresholdSigma.Text.Trim()
    $profile.MouseLeftYawOutputCurve = $script:tuningFields.MouseLeftYawOutputCurve.Text.Trim()
    $profile.MouseLeftYawExpo = $script:tuningFields.MouseLeftYawExpo.Text.Trim()
    $profile.MouseLeftYawActualCenter = $script:tuningFields.MouseLeftYawActualCenter.Text.Trim()
    $profile.MouseLeftYawActualMax = $script:tuningFields.MouseLeftYawActualMax.Text.Trim()
    $profile.MouseLeftYawActualExpo = $script:tuningFields.MouseLeftYawActualExpo.Text.Trim()
    $profile.MouseLeftYawPositionModel = $script:tuningFields.MouseLeftYawPositionModel.Text.Trim()
    $profile.MouseLeftYawGimbalFrequencyHz = $script:tuningFields.MouseLeftYawGimbalFrequencyHz.Text.Trim()
    $profile.MouseLeftYawGimbalDampingRatio = $script:tuningFields.MouseLeftYawGimbalDampingRatio.Text.Trim()
    $profile.MouseLeftYawGimbalInputImpulse = $script:tuningFields.MouseLeftYawGimbalInputImpulse.Text.Trim()
    $profile.MouseLeftYawGimbalStaticFriction = $script:tuningFields.MouseLeftYawGimbalStaticFriction.Text.Trim()
    $profile.MouseLeftYawGimbalDynamicFriction = $script:tuningFields.MouseLeftYawGimbalDynamicFriction.Text.Trim()
    $profile.MouseLeftYawGimbalEdgeBumper = $script:tuningFields.MouseLeftYawGimbalEdgeBumper.Text.Trim()
    $profile.MouseLeftYawGimbalAntiwindupEnabled = ConvertTo-TomlBool $script:tuningChecks.MouseLeftYawGimbalAntiwindupEnabled.Checked
    $profile.MouseLeftYawGimbalAntiwindupStart = $script:tuningFields.MouseLeftYawGimbalAntiwindupStart.Text.Trim()
    $profile.MouseLeftYawGimbalAntiwindupMinGain = $script:tuningFields.MouseLeftYawGimbalAntiwindupMinGain.Text.Trim()
    $profile.MouseLeftYawInputGainMode = $script:tuningFields.MouseLeftYawInputGainMode.Text.Trim()
    $profile.MouseLeftYawAdaptiveSlowGain = $script:tuningFields.MouseLeftYawAdaptiveSlowGain.Text.Trim()
    $profile.MouseLeftYawAdaptiveFastGain = $script:tuningFields.MouseLeftYawAdaptiveFastGain.Text.Trim()
    $profile.MouseLeftYawAdaptiveSpeedLow = $script:tuningFields.MouseLeftYawAdaptiveSpeedLow.Text.Trim()
    $profile.MouseLeftYawAdaptiveSpeedHigh = $script:tuningFields.MouseLeftYawAdaptiveSpeedHigh.Text.Trim()
    $profile.MouseLeftYawAdaptiveCurve = $script:tuningFields.MouseLeftYawAdaptiveCurve.Text.Trim()
    $profile.MouseLeftYawAdaptiveTrackerMs = $script:tuningFields.MouseLeftYawAdaptiveTrackerMs.Text.Trim()
    $profile.MouseLeftYawGateShape = $script:tuningFields.MouseLeftYawGateShape.Text.Trim()
    $profile.MouseLeftYawDiagonalScale = $script:tuningFields.MouseLeftYawDiagonalScale.Text.Trim()
    $profile.MouseLeftYawReturnEnabled = ConvertTo-TomlBool $script:tuningChecks.MouseLeftYawReturnEnabled.Checked
    $profile.MouseLeftYawReturnRate = $script:tuningFields.MouseLeftYawReturnRate.Text.Trim()
    $profile.MouseLeftYawReturnIdle = $script:tuningFields.MouseLeftYawReturnIdle.Text.Trim()
    $profile.MouseLeftYawConstantReturnEnabled = ConvertTo-TomlBool $script:tuningChecks.MouseLeftYawConstantReturnEnabled.Checked
    $profile.MouseLeftYawConstantReturnRate = $script:tuningFields.MouseLeftYawConstantReturnRate.Text.Trim()
    $profile.MouseLeftYawElasticReturnEnabled = ConvertTo-TomlBool $script:tuningChecks.MouseLeftYawElasticReturnEnabled.Checked
    $profile.MouseLeftYawElasticReturnMode = $script:tuningFields.MouseLeftYawElasticReturnMode.Text.Trim()
    $profile.MouseLeftYawElasticReturnCoefficient = $script:tuningFields.MouseLeftYawElasticReturnCoefficient.Text.Trim()
    $profile.MouseLeftYawElasticReturnCurve = $script:tuningFields.MouseLeftYawElasticReturnCurve.Text.Trim()
    $profile.MouseLeftYawOutputShapingEnabled = ConvertTo-TomlBool $script:tuningChecks.MouseLeftYawOutputShapingEnabled.Checked
    $profile.MouseLeftYawOutputShapeNodesText = $mouseLeftYawOutputNodesText
    $profile.MouseLeftYawReturnShapingEnabled = ConvertTo-TomlBool $script:tuningChecks.MouseLeftYawReturnShapingEnabled.Checked
    $profile.MouseLeftYawReturnShapeNodesText = $mouseLeftYawReturnNodesText
    $profile.MouseLeftInvertThrottle = ConvertTo-TomlBool $script:tuningChecks.MouseLeftInvertThrottle.Checked
    $profile.MouseLeftInvertYaw = ConvertTo-TomlBool $script:tuningChecks.MouseLeftInvertYaw.Checked
    $profile.MouseLeftSwapAxes = ConvertTo-TomlBool $script:tuningChecks.MouseLeftSwapAxes.Checked
    $profile.KeyboardEnabled = ConvertTo-TomlBool $script:tuningChecks.KeyboardEnabled.Checked
    $profile.BlockSelectedKeys = ConvertTo-TomlBool $script:tuningChecks.BlockSelectedKeys.Checked
    $profile.ThrottleUpKey = $script:tuningFields.ThrottleUpKey.Text.Trim()
    $profile.ThrottleDownKey = $script:tuningFields.ThrottleDownKey.Text.Trim()
    $profile.YawLeftKey = $script:tuningFields.YawLeftKey.Text.Trim()
    $profile.YawRightKey = $script:tuningFields.YawRightKey.Text.Trim()
    $profile.ThrottleCutKey = $script:tuningFields.ThrottleCutKey.Text.Trim()
    $profile.ThrottleRate = $script:tuningFields.ThrottleRate.Text.Trim()
    $profile.ThrottleReturnEnabled = ConvertTo-TomlBool $script:tuningChecks.ThrottleReturnEnabled.Checked
    $profile.ThrottleReturnRate = $script:tuningFields.ThrottleReturnRate.Text.Trim()
    $profile.YawPulse = $script:tuningFields.YawPulse.Text.Trim()
    $profile.YawSlewRate = $script:tuningFields.YawSlewRate.Text.Trim()
    $profile.InvertYaw = ConvertTo-TomlBool $script:tuningChecks.InvertYaw.Checked
    $status.Text = "Saved tuning parameters to $($profile.FileName)."
}

function Get-DoubleOrDefault {
    param(
        [string]$Text,
        [double]$Default
    )

    $value = 0.0
    if ([double]::TryParse($Text, [ref]$value)) {
        return $value
    }
    return $Default
}

function Get-IntOrDefault {
    param(
        [string]$Text,
        [int]$Default
    )

    $value = 0
    if ([int]::TryParse($Text, [ref]$value)) {
        return $value
    }
    return $Default
}

function Get-ElasticReturnScale {
    param(
        [double]$Norm,
        [string]$Mode,
        [double]$Curve
    )

    $n = [Math]::Max(0.0, [Math]::Min(1.0, $Norm))
    $c = [Math]::Max(0.0, $Curve)
    switch ($Mode.Trim().ToLowerInvariant()) {
        'linear' { return 1.0 }
        'smoothstep' {
            $smooth = $n * $n * (3.0 - (2.0 * $n))
            if ($c -le 0.0) {
                return $smooth
            }
            return [Math]::Pow($smooth, 1.0 + $c)
        }
        'expo' {
            $k = 1.0 + $c
            $denom = [Math]::Exp($k) - 1.0
            if ($denom -le 0.0) {
                return $n
            }
            return ([Math]::Exp($k * $n) - 1.0) / $denom
        }
        default {
            if ($c -le 0.0) {
                return 1.0
            }
            return [Math]::Pow($n, $c)
        }
    }
}

function Get-StickShapeCurveValue {
    param(
        [double]$Norm,
        [object[]]$Nodes
    )

    $n = [Math]::Max(0.0, [Math]::Min(1.0, $Norm))
    if ($null -eq $Nodes -or $Nodes.Count -eq 0) {
        return $n
    }
    $sumK = 0.0
    $sumKy = 0.0
    $maxK = 0.0
    foreach ($node in $Nodes) {
        $w = [double]$node.Width
        if ($w -lt 0.05) { $w = 0.05 } elseif ($w -gt 1.0) { $w = 1.0 }
        $dx = $n - [double]$node.X
        if ($dx -lt 0.0) { $dx = -$dx }
        if ($dx -ge $w) { continue }
        $k = 0.5 * (1.0 + [Math]::Cos(([Math]::PI * $dx) / $w))
        $yc = [double]$node.Y
        if ($yc -lt 0.0) { $yc = 0.0 } elseif ($yc -gt 1.0) { $yc = 1.0 }
        $sumK += $k
        $sumKy += $k * $yc
        if ($k -gt $maxK) { $maxK = $k }
    }
    if ($sumK -le 0.0) { return $n }
    $weighted = $sumKy / $sumK
    $blend = $maxK
    if ($blend -lt 0.0) { $blend = 0.0 } elseif ($blend -gt 1.0) { $blend = 1.0 }
    $v = ($blend * $weighted) + ((1.0 - $blend) * $n)
    if ($v -lt 0.0) { $v = 0.0 } elseif ($v -gt 1.0) { $v = 1.0 }
    return $v
}

function Get-ElasticReturnRate {
    param(
        [double]$Norm,
        [int]$MaxOutput,
        [double]$Coefficient,
        [string]$Mode,
        [double]$Curve,
        [bool]$ReturnShapingEnabled = $false,
        [object[]]$ReturnShapeNodes = @()
    )

    $maxValue = [Math]::Max(1.0, [double]$MaxOutput)
    $n = [Math]::Max(0.0, [Math]::Min(1.0, $Norm))
    $position = $n * $maxValue
    if ($position -le 0.0 -or $Coefficient -le 0.0) {
        return 0.0
    }
    if ($ReturnShapingEnabled -and $null -ne $ReturnShapeNodes -and $ReturnShapeNodes.Count -gt 0) {
        return $maxValue * $Coefficient * (Get-StickShapeCurveValue -Norm $n -Nodes $ReturnShapeNodes)
    }
    return $position * $Coefficient * (Get-ElasticReturnScale -Norm $n -Mode $Mode -Curve $Curve)
}

function Get-AdaptiveGainFromSpeed {
    param(
        [double]$Speed,
        [double]$SlowGain,
        [double]$FastGain,
        [double]$SpeedLow,
        [double]$SpeedHigh,
        [double]$Curve
    )

    $low = [Math]::Max(0.0, $SpeedLow)
    $high = [Math]::Max($low + 1.0, $SpeedHigh)
    $t = ([Math]::Max(0.0, [Math]::Min(1.0, (($Speed - $low) / ($high - $low)))))
    $c = [Math]::Max(0.01, $Curve)
    if ([Math]::Abs($c - 1.0) -gt 0.0001) {
        $t = [Math]::Pow($t, $c)
    }
    return [Math]::Max(0.0, $SlowGain + (($FastGain - $SlowGain) * $t))
}

function Apply-PreviewRadialGate {
    param(
        [double]$Roll,
        [double]$Pitch,
        [int]$MaxOutput,
        [string]$GateShape,
        [double]$DiagonalScale
    )

    $maxValue = [Math]::Max(1.0, [double]$MaxOutput)
    $x = [Math]::Max(-$maxValue, [Math]::Min($maxValue, $Roll))
    $y = [Math]::Max(-$maxValue, [Math]::Min($maxValue, $Pitch))
    $scale = 1.0
    $shape = $GateShape.Trim().ToLowerInvariant()
    if ($shape -ne 'axis') {
        $r = [Math]::Sqrt(($x * $x) + ($y * $y))
        if ($r -gt 0.0) {
            $ux = [Math]::Abs($x) / $r
            $uy = [Math]::Abs($y) / $r
            $squareLimit = $maxValue / [Math]::Max($ux, $uy)
            $diag = [Math]::Max(0.0, [Math]::Min(1.5, $DiagonalScale))
            $limit = $maxValue
            if ($shape -eq 'octagon') {
                $limit = $maxValue + (($squareLimit - $maxValue) * 0.5 * $diag)
            } elseif ($shape -eq 'square') {
                $limit = $maxValue + (($squareLimit - $maxValue) * $diag)
            }
            if ($r -gt $limit) {
                $scale = $limit / $r
                $x *= $scale
                $y *= $scale
            }
        }
    }

    [pscustomobject]@{
        Roll = [Math]::Max(-$maxValue, [Math]::Min($maxValue, $x))
        Pitch = [Math]::Max(-$maxValue, [Math]::Min($maxValue, $y))
        Scale = $scale
    }
}

function Get-ShapedStickAxis {
    param(
        [double]$Value,
        [int]$MaxOutput,
        [int]$Deadband,
        [double]$Expo,
        [bool]$OutputShapingEnabled = $false,
        [object[]]$OutputShapeNodes = @(),
        [string]$OutputCurve = 'expo',
        [double]$ActualCenter = 0.45,
        [double]$ActualMax = 1.0,
        [double]$ActualExpo = 0.30
    )

    $maxValue = [Math]::Max(1.0, [double]$MaxOutput)
    $sign = if ($Value -lt 0.0) { -1.0 } else { 1.0 }
    $magnitude = [Math]::Abs($Value)
    if ($magnitude -le [double]$Deadband) {
        return 0.0
    }

    $magnitude = [Math]::Min($magnitude, $maxValue)
    if ($Deadband -gt 0 -and $Deadband -lt $MaxOutput) {
        $norm = ($magnitude - [double]$Deadband) / [double]($MaxOutput - $Deadband)
    } else {
        $norm = $magnitude / $maxValue
    }
    $norm = [Math]::Max(0.0, [Math]::Min(1.0, $norm))

    $curve = $OutputCurve.Trim().Trim('"').ToLowerInvariant()
    if ($curve -eq 'nodes' -or ($curve -eq 'expo' -and $OutputShapingEnabled)) {
        $curved = Get-StickShapeCurveValue -Norm $norm -Nodes $OutputShapeNodes
    } elseif ($curve -eq 'actual') {
        $center = [Math]::Max(0.0, [Math]::Min(1.0, $ActualCenter))
        $maxRate = [Math]::Max(0.0, [Math]::Min(1.0, $ActualMax))
        $actualExpo = [Math]::Max(0.0, [Math]::Min(0.95, $ActualExpo))
        $expoNorm = ($norm * (1.0 - $actualExpo)) + ($norm * $norm * $norm * $actualExpo)
        $curved = [Math]::Max(0.0, [Math]::Min(1.0, ($center * $norm) + (($maxRate - $center) * $expoNorm * $norm)))
    } else {
        $expoClamped = [Math]::Max(0.0, [Math]::Min(1.0, $Expo))
        $curved = ((1.0 - $expoClamped) * $norm) + ($expoClamped * $norm * $norm * $norm)
    }
    return $sign * $curved * $maxValue
}

function Get-StickPreviewSettings {
    $mode = 'progressive'
    if ($script:tuningFields.ContainsKey('ElasticReturnMode')) {
        $mode = $script:tuningFields.ElasticReturnMode.Text.Trim().Trim('"').ToLowerInvariant()
    }
    if (-not (Test-ElasticReturnModeField -Text $mode)) {
        $mode = 'progressive'
    }
    $positionModel = $script:tuningFields.PositionModel.Text.Trim().Trim('"').ToLowerInvariant()
    if (-not (Test-PositionModelField -Text $positionModel)) {
        $positionModel = 'integrator'
    }
    $inputGainMode = $script:tuningFields.InputGainMode.Text.Trim().Trim('"').ToLowerInvariant()
    if (-not (Test-InputGainModeField -Text $inputGainMode)) {
        $inputGainMode = 'flat'
    }
    $gateShape = $script:tuningFields.GateShape.Text.Trim().Trim('"').ToLowerInvariant()
    if (-not (Test-GateShapeField -Text $gateShape)) {
        $gateShape = 'axis'
    }
    $outputCurve = $script:tuningFields.OutputCurve.Text.Trim().Trim('"').ToLowerInvariant()
    if (-not (Test-OutputCurveField -Text $outputCurve)) {
        $outputCurve = 'expo'
    }

    $maxOutput = Get-IntOrDefault -Text $script:tuningFields.MaxOutput.Text -Default 512
    $deadband = Get-IntOrDefault -Text $script:tuningFields.Deadband.Text -Default 0
    $frameRate = Get-IntOrDefault -Text $script:tuningFields.FrameRate.Text -Default 1000
    $mapperRate = [Math]::Max(1, [Math]::Min(1000, $frameRate))
    $maxOutput = [Math]::Max(1, [Math]::Min(512, $maxOutput))
    $deadband = [Math]::Max(0, [Math]::Min($maxOutput - 1, $deadband))

    $elasticEnabled = $script:tuningChecks.ElasticReturnEnabled.Checked
    $idleEnabled = $script:tuningChecks.ReturnEnabled.Checked
    $constantEnabled = $script:tuningChecks.ConstantReturnEnabled.Checked
    $coefficient = if ($elasticEnabled) { Get-DoubleOrDefault -Text $script:tuningFields.ElasticReturnCoefficient.Text -Default 0.0 } else { 0.0 }
    $rollGain = Get-DoubleOrDefault -Text $script:tuningFields.RollGain.Text -Default 0.0
    $pitchGain = Get-DoubleOrDefault -Text $script:tuningFields.PitchGain.Text -Default 0.0
    $gainScale = [double]$mapperRate / 1000.0
    $returnRate = if ($idleEnabled) { Get-DoubleOrDefault -Text $script:tuningFields.ReturnRate.Text -Default 0.0 } else { 0.0 }
    $constantRate = if ($constantEnabled) { Get-DoubleOrDefault -Text $script:tuningFields.ConstantReturnRate.Text -Default 0.0 } else { 0.0 }
    $outputShapingEnabled = $script:tuningChecks.OutputShapingEnabled.Checked
    $returnShapingEnabled = $script:tuningChecks.ReturnShapingEnabled.Checked

    [pscustomobject]@{
        Mode = $mode
        MaxOutput = $maxOutput
        Deadband = $deadband
        Expo = [Math]::Max(0.0, [Math]::Min(1.0, (Get-DoubleOrDefault -Text $script:tuningFields.Expo.Text -Default 0.0)))
        Smoothing = [Math]::Max(0.0, [Math]::Min(0.999, (Get-DoubleOrDefault -Text $script:tuningFields.Smoothing.Text -Default 0.0)))
        OutputCurve = $outputCurve
        ActualCenter = [Math]::Max(0.0, [Math]::Min(1.0, (Get-DoubleOrDefault -Text $script:tuningFields.ActualCenter.Text -Default 0.45)))
        ActualMax = [Math]::Max(0.0, [Math]::Min(1.0, (Get-DoubleOrDefault -Text $script:tuningFields.ActualMax.Text -Default 1.0)))
        ActualExpo = [Math]::Max(0.0, [Math]::Min(0.95, (Get-DoubleOrDefault -Text $script:tuningFields.ActualExpo.Text -Default 0.30)))
        MapperRate = $mapperRate
        GainScale = $gainScale
        RollGain = [Math]::Max(0.0, $rollGain) * $gainScale
        PitchGain = [Math]::Max(0.0, $pitchGain) * $gainScale
        PositionModel = $positionModel
        GimbalFrequencyHz = [Math]::Max(0.1, (Get-DoubleOrDefault -Text $script:tuningFields.GimbalFrequencyHz.Text -Default 5.0))
        GimbalDampingRatio = [Math]::Max(0.0, (Get-DoubleOrDefault -Text $script:tuningFields.GimbalDampingRatio.Text -Default 1.15))
        GimbalInputImpulse = [Math]::Max(0.0, (Get-DoubleOrDefault -Text $script:tuningFields.GimbalInputImpulse.Text -Default 1.0))
        GimbalStaticFriction = [Math]::Max(0.0, (Get-DoubleOrDefault -Text $script:tuningFields.GimbalStaticFriction.Text -Default 0.0))
        GimbalDynamicFriction = [Math]::Max(0.0, (Get-DoubleOrDefault -Text $script:tuningFields.GimbalDynamicFriction.Text -Default 0.0))
        GimbalEdgeBumper = [Math]::Max(0.0, (Get-DoubleOrDefault -Text $script:tuningFields.GimbalEdgeBumper.Text -Default 0.0))
        InputGainMode = $inputGainMode
        AdaptiveSlowGain = [Math]::Max(0.0, (Get-DoubleOrDefault -Text $script:tuningFields.AdaptiveSlowGain.Text -Default 0.65))
        AdaptiveFastGain = [Math]::Max(0.0, (Get-DoubleOrDefault -Text $script:tuningFields.AdaptiveFastGain.Text -Default 1.60))
        AdaptiveSpeedLow = [Math]::Max(0.0, (Get-DoubleOrDefault -Text $script:tuningFields.AdaptiveSpeedLow.Text -Default 120.0))
        AdaptiveSpeedHigh = [Math]::Max(1.0, (Get-DoubleOrDefault -Text $script:tuningFields.AdaptiveSpeedHigh.Text -Default 1800.0))
        AdaptiveCurve = [Math]::Max(0.01, (Get-DoubleOrDefault -Text $script:tuningFields.AdaptiveCurve.Text -Default 1.0))
        AdaptiveTrackerMs = [Math]::Max(0.0, (Get-DoubleOrDefault -Text $script:tuningFields.AdaptiveTrackerMs.Text -Default 35.0))
        GateShape = $gateShape
        DiagonalScale = [Math]::Max(0.0, [Math]::Min(1.5, (Get-DoubleOrDefault -Text $script:tuningFields.DiagonalScale.Text -Default 1.0)))
        MouseEnabled = $script:tuningChecks.MouseRightStickEnabled.Checked
        InvertRoll = $script:tuningChecks.InvertRoll.Checked
        InvertPitch = $script:tuningChecks.InvertPitch.Checked
        SwapAxes = $script:tuningChecks.SwapAxes.Checked
        IdleEnabled = $idleEnabled
        IdleRate = [Math]::Max(0.0, $returnRate)
        IdleMs = [Math]::Max(0.0, (Get-DoubleOrDefault -Text $script:tuningFields.ReturnIdle.Text -Default 0.0))
        ConstantEnabled = $constantEnabled
        ConstantRate = [Math]::Max(0.0, $constantRate)
        ElasticEnabled = $elasticEnabled
        Coefficient = [Math]::Max(0.0, $coefficient)
        Curve = [Math]::Max(0.0, (Get-DoubleOrDefault -Text $script:tuningFields.ElasticReturnCurve.Text -Default 0.0))
        OutputShapingEnabled = $outputShapingEnabled
        OutputShapeNodes = (Get-StickShapeEditorNodes -Name 'OutputShape')
        ReturnShapingEnabled = $returnShapingEnabled
        ReturnShapeNodes = (Get-StickShapeEditorNodes -Name 'ReturnShape')
    }
}

function Get-PreviewReturnStep {
    param($Settings)

    return ($Settings.IdleRate + $Settings.ConstantRate) / [double]$Settings.MapperRate
}

function Get-PreviewElasticStep {
    param(
        $Settings,
        [double]$Position
    )

    $rate = Get-ElasticReturnRate -Norm ([Math]::Abs($Position) / [double]$Settings.MaxOutput) -MaxOutput $Settings.MaxOutput -Coefficient $Settings.Coefficient -Mode $Settings.Mode -Curve $Settings.Curve -ReturnShapingEnabled $Settings.ReturnShapingEnabled -ReturnShapeNodes $Settings.ReturnShapeNodes
    return $rate / [double]$Settings.MapperRate
}

function New-StickResponseRows {
    param($Settings)

    $rows = @()
    for ($i = 0; $i -le 8; $i++) {
        $norm = [double]$i / 8.0
        $position = $norm * [double]$Settings.MaxOutput
        $linear = Get-ShapedStickAxis -Value $position -MaxOutput $Settings.MaxOutput -Deadband $Settings.Deadband -Expo 0.0
        $shaped = Get-ShapedStickAxis -Value $position -MaxOutput $Settings.MaxOutput -Deadband $Settings.Deadband -Expo $Settings.Expo -OutputShapingEnabled $Settings.OutputShapingEnabled -OutputShapeNodes $Settings.OutputShapeNodes -OutputCurve $Settings.OutputCurve -ActualCenter $Settings.ActualCenter -ActualMax $Settings.ActualMax -ActualExpo $Settings.ActualExpo
        $smoothed = (1.0 - $Settings.Smoothing) * $shaped
        $rows += [pscustomobject]@{
            Norm = $norm
            Percent = [int][Math]::Round($norm * 100.0)
            Position = $position
            Linear = $linear
            Shaped = $shaped
            Smoothed = $smoothed
        }
    }
    return @($rows)
}

function New-ElasticCurveRows {
    param($Settings)

    $rows = @()
    for ($i = 0; $i -le 8; $i++) {
        $norm = [double]$i / 8.0
        $elasticRate = if ($Settings.ElasticEnabled) {
            Get-ElasticReturnRate -Norm $norm -MaxOutput $Settings.MaxOutput -Coefficient $Settings.Coefficient -Mode $Settings.Mode -Curve $Settings.Curve -ReturnShapingEnabled $Settings.ReturnShapingEnabled -ReturnShapeNodes $Settings.ReturnShapeNodes
        } else {
            0.0
        }
        $linearElasticRate = if ($Settings.ElasticEnabled) {
            Get-ElasticReturnRate -Norm $norm -MaxOutput $Settings.MaxOutput -Coefficient $Settings.Coefficient -Mode 'linear' -Curve 0.0
        } else {
            0.0
        }
        $fixedRate = $Settings.IdleRate + $Settings.ConstantRate
        $rows += [pscustomobject]@{
            Norm = $norm
            Percent = [int][Math]::Round($norm * 100.0)
            ElasticRate = $elasticRate
            LinearElasticRate = $linearElasticRate
            TotalRate = $fixedRate + $elasticRate
            PerTick = ($fixedRate + $elasticRate) / [double]$Settings.MapperRate
        }
    }
    return @($rows)
}

function New-StickReturnRows {
    param($Settings)

    $rows = @()
    $position = [double]$Settings.MaxOutput
    $dtMs = 1000.0 / [double]$Settings.MapperRate
    $nextSampleMs = 0.0
    $totalTicks = [Math]::Min([int]($Settings.MapperRate * 1.5), 3000)
    for ($tick = 0; $tick -le $totalTicks; $tick++) {
        $elapsedMs = $tick * $dtMs
        if ($elapsedMs -ge $nextSampleMs -or $tick -eq $totalTicks -or $position -le 0.0) {
            $output = Get-ShapedStickAxis -Value $position -MaxOutput $Settings.MaxOutput -Deadband $Settings.Deadband -Expo $Settings.Expo -OutputShapingEnabled $Settings.OutputShapingEnabled -OutputShapeNodes $Settings.OutputShapeNodes -OutputCurve $Settings.OutputCurve -ActualCenter $Settings.ActualCenter -ActualMax $Settings.ActualMax -ActualExpo $Settings.ActualExpo
            $rows += [pscustomobject]@{
                TimeMs = [int][Math]::Round($elapsedMs)
                Position = $position
                Output = $output
            }
            $nextSampleMs += 100.0
        }
        if ($tick -eq $totalTicks -or $position -le 0.0) {
            continue
        }
        $step = Get-PreviewReturnStep -Settings $Settings
        if ($Settings.ElasticEnabled) {
            $step += Get-PreviewElasticStep -Settings $Settings -Position $position
        }
        if ($step -le 0.0) {
            continue
        }
        $position = [Math]::Max(0.0, $position - $step)
    }
    return @($rows)
}

function New-AdaptiveGainRows {
    param($Settings)

    $speeds = @(
        0.0,
        ($Settings.AdaptiveSpeedLow * 0.5),
        $Settings.AdaptiveSpeedLow,
        (($Settings.AdaptiveSpeedLow + $Settings.AdaptiveSpeedHigh) * 0.5),
        $Settings.AdaptiveSpeedHigh,
        ($Settings.AdaptiveSpeedHigh * 1.5)
    )
    $rows = @()
    foreach ($speed in $speeds) {
        $gain = if ($Settings.InputGainMode -eq 'adaptive') {
            Get-AdaptiveGainFromSpeed -Speed $speed -SlowGain $Settings.AdaptiveSlowGain -FastGain $Settings.AdaptiveFastGain -SpeedLow $Settings.AdaptiveSpeedLow -SpeedHigh $Settings.AdaptiveSpeedHigh -Curve $Settings.AdaptiveCurve
        } else {
            1.0
        }
        $rows += [pscustomobject]@{ Speed = $speed; Gain = $gain }
    }
    return @($rows)
}

function New-GatePreviewRows {
    param($Settings)

    $max = [double]$Settings.MaxOutput
    $samples = @(
        @{ Roll = $max; Pitch = 0.0 },
        @{ Roll = $max; Pitch = $max * 0.5 },
        @{ Roll = $max; Pitch = $max },
        @{ Roll = $max * 0.75; Pitch = $max * 0.75 },
        @{ Roll = $max * 1.1; Pitch = $max * 0.9 }
    )
    $rows = @()
    foreach ($sample in $samples) {
        $gated = Apply-PreviewRadialGate -Roll $sample.Roll -Pitch $sample.Pitch -MaxOutput $Settings.MaxOutput -GateShape $Settings.GateShape -DiagonalScale $Settings.DiagonalScale
        $rows += [pscustomobject]@{
            RollIn = $sample.Roll
            PitchIn = $sample.Pitch
            RollOut = $gated.Roll
            PitchOut = $gated.Pitch
            Scale = $gated.Scale
        }
    }
    return @($rows)
}

function New-ModelResponseRows {
    param($Settings)

    $rows = @()
    $dt = 1.0 / [double]$Settings.MapperRate
    $rollPosition = 0.0
    $pitchPosition = 0.0
    $rollVelocity = 0.0
    $trackedSpeed = 0.0
    $sampleTicks = [Math]::Max(1, [int]($Settings.MapperRate / 20))
    $flickTicks = [Math]::Max(1, [int]($Settings.MapperRate / 25))
    $totalTicks = [Math]::Min([int]($Settings.MapperRate * 1.2), 2400)
    $idleTicks = if ($Settings.IdleMs -le 0.0) { 0 } else { [int][Math]::Ceiling(($Settings.IdleMs / 1000.0) * [double]$Settings.MapperRate) }
    $lastMotionTick = -100000
    $omega = 2.0 * [Math]::PI * $Settings.GimbalFrequencyHz
    for ($tick = 0; $tick -le $totalTicks; $tick++) {
        $dx = if ($tick -lt $flickTicks) { 8.0 } else { 0.0 }
        $dy = 0.0
        if ($dx -ne 0.0 -or $dy -ne 0.0) {
            $lastMotionTick = $tick
        }
        $rawSpeed = [Math]::Sqrt(($dx * $dx) + ($dy * $dy)) / $dt
        if ($Settings.InputGainMode -eq 'adaptive') {
            $trackerSeconds = $Settings.AdaptiveTrackerMs / 1000.0
            $alpha = if ($trackerSeconds -le 0.0) { 1.0 } else { 1.0 - [Math]::Exp(-$dt / $trackerSeconds) }
            $trackedSpeed += ($rawSpeed - $trackedSpeed) * [Math]::Max(0.0, [Math]::Min(1.0, $alpha))
            $inputGain = Get-AdaptiveGainFromSpeed -Speed $trackedSpeed -SlowGain $Settings.AdaptiveSlowGain -FastGain $Settings.AdaptiveFastGain -SpeedLow $Settings.AdaptiveSpeedLow -SpeedHigh $Settings.AdaptiveSpeedHigh -Curve $Settings.AdaptiveCurve
        } else {
            $trackedSpeed = 0.0
            $inputGain = 1.0
        }
        $rollInput = $dx * $inputGain * $Settings.RollGain
        if ($Settings.InvertRoll) { $rollInput = -$rollInput }

        if ($Settings.PositionModel -eq 'dynamic_gimbal') {
            $rollVelocity += $rollInput * $Settings.GimbalInputImpulse / $dt
            $accel = (-$omega * $omega * $rollPosition) - (2.0 * $Settings.GimbalDampingRatio * $omega * $rollVelocity)
            $rollVelocity += $accel * $dt
            if ($Settings.GimbalDynamicFriction -gt 0.0 -and [Math]::Abs($rollVelocity) -gt 0.0) {
                $dv = $Settings.GimbalDynamicFriction * [double]$Settings.MaxOutput * $dt
                if ([Math]::Abs($rollVelocity) -le $dv -and [Math]::Abs($rollInput) -lt 0.000001) {
                    $rollVelocity = 0.0
                } else {
                    $rollVelocity -= $(if ($rollVelocity -lt 0.0) { -$dv } else { $dv })
                }
            }
            $rollPosition += $rollVelocity * $dt
        } else {
            $rollVelocity = 0.0
            $rollPosition += $rollInput
        }

        $applyIdle = $Settings.IdleEnabled -and $Settings.IdleRate -gt 0.0 -and $dx -eq 0.0 -and (($tick - $lastMotionTick) -ge $idleTicks)
        $step = ($(if ($applyIdle) { $Settings.IdleRate / [double]$Settings.MapperRate } else { 0.0 })) + ($(if ($Settings.ConstantEnabled) { $Settings.ConstantRate / [double]$Settings.MapperRate } else { 0.0 }))
        if ($Settings.ElasticEnabled) {
            $step += Get-PreviewElasticStep -Settings $Settings -Position $rollPosition
        }
        if ($step -gt 0.0) {
            if ($rollPosition -gt 0.0) { $rollPosition = [Math]::Max(0.0, $rollPosition - $step) }
            elseif ($rollPosition -lt 0.0) { $rollPosition = [Math]::Min(0.0, $rollPosition + $step) }
        }

        $gate = Apply-PreviewRadialGate -Roll $rollPosition -Pitch $pitchPosition -MaxOutput $Settings.MaxOutput -GateShape $Settings.GateShape -DiagonalScale $Settings.DiagonalScale
        $rollPosition = $gate.Roll
        $pitchPosition = $gate.Pitch
        $output = Get-ShapedStickAxis -Value $rollPosition -MaxOutput $Settings.MaxOutput -Deadband $Settings.Deadband -Expo $Settings.Expo -OutputShapingEnabled $Settings.OutputShapingEnabled -OutputShapeNodes $Settings.OutputShapeNodes -OutputCurve $Settings.OutputCurve -ActualCenter $Settings.ActualCenter -ActualMax $Settings.ActualMax -ActualExpo $Settings.ActualExpo
        if (($tick % $sampleTicks) -eq 0 -or $tick -eq $totalTicks) {
            $rows += [pscustomobject]@{
                TimeMs = [int][Math]::Round(1000.0 * [double]$tick / [double]$Settings.MapperRate)
                Dx = $dx
                Gain = $inputGain
                Gate = $gate.Scale
                Position = $rollPosition
                Velocity = $rollVelocity
                Output = $output
            }
        }
    }
    return @($rows)
}

function Show-ElasticReturnPreview {
    if (-not (Validate-TuningControls)) {
        return
    }

    $settings = Get-StickPreviewSettings
    $rows = New-StickResponseRows -Settings $settings
    $elasticRows = New-ElasticCurveRows -Settings $settings
    $returnRows = New-StickReturnRows -Settings $settings
    $adaptiveRows = New-AdaptiveGainRows -Settings $settings
    $gateRows = New-GatePreviewRows -Settings $settings
    $modelRows = New-ModelResponseRows -Settings $settings
    $preview = New-Object System.Windows.Forms.Form
    $preview.Text = 'Stick Response Preview'
    $preview.StartPosition = 'CenterParent'
    $preview.Size = New-Object System.Drawing.Size(1080, 880)
    $preview.MinimumSize = New-Object System.Drawing.Size(860, 720)
    $preview.Font = New-Object System.Drawing.Font('Segoe UI', 9.0)
    $preview.BackColor = [System.Drawing.Color]::White

    $summary = New-Object System.Windows.Forms.Label
    $summary.Location = New-Object System.Drawing.Point(16, 14)
    $summary.Size = New-Object System.Drawing.Size(1010, 58)
    $summary.Anchor = 'Top,Left,Right'
    $summary.Text = ('mapper={0} Hz  model={1}  input_gain={2}  gate={3} diag={4:0.###}  max={5}  deadband={6}  expo={7:0.###}  output_shape={8}  smoothing={9:0.###}  axes={10}{11}{12}' -f $settings.MapperRate, $settings.PositionModel, $settings.InputGainMode, $settings.GateShape, $settings.DiagonalScale, $settings.MaxOutput, $settings.Deadband, $settings.Expo, ($(if ($settings.OutputShapingEnabled) { 'on' } else { 'off' })), $settings.Smoothing, ($(if ($settings.SwapAxes) { 'swapped ' } else { 'normal ' })), ($(if ($settings.InvertRoll) { 'roll-inverted ' } else { '' })), ($(if ($settings.InvertPitch) { 'pitch-inverted' } else { '' })))
    $preview.Controls.Add($summary)

    $graph = New-Object System.Windows.Forms.Panel
    $graph.Location = New-Object System.Drawing.Point(16, 78)
    $graph.Size = New-Object System.Drawing.Size(1010, 280)
    $graph.Anchor = 'Top,Left,Right'
    $graph.BackColor = [System.Drawing.Color]::White
    $graph.BorderStyle = [System.Windows.Forms.BorderStyle]::FixedSingle
    $graph.Add_Paint({
        param($sender, $eventArgs)

        $g = $eventArgs.Graphics
        $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
        $left = 58
        $top = 22
        $right = $sender.Width - 22
        $bottom = $sender.Height - 42
        $width = [Math]::Max(1, $right - $left)
        $height = [Math]::Max(1, $bottom - $top)
        $axisPen = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(180, 190, 200), 1)
        $linearPen = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(150, 150, 150), 1)
        $linearPen.DashStyle = [System.Drawing.Drawing2D.DashStyle]::Dash
        $curvePen = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(26, 115, 232), 2)
        $smoothPen = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(20, 150, 110), 2)
        $smoothPen.DashStyle = [System.Drawing.Drawing2D.DashStyle]::Dot
        $textBrush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(45, 52, 62))
        try {
            $g.DrawLine($axisPen, $left, $bottom, $right, $bottom)
            $g.DrawLine($axisPen, $left, $top, $left, $bottom)
            $g.DrawString('0%', $sender.Font, $textBrush, [single]($left - 10), [single]($bottom + 8))
            $g.DrawString('100%', $sender.Font, $textBrush, [single]($right - 36), [single]($bottom + 8))
            $g.DrawString('output', $sender.Font, $textBrush, 8, [single]($top - 4))

            $maxRate = [Math]::Max(1.0, [double]$settings.MaxOutput)
            $selectedPoints = New-Object 'System.Collections.Generic.List[System.Drawing.PointF]'
            $linearPoints = New-Object 'System.Collections.Generic.List[System.Drawing.PointF]'
            $smoothPoints = New-Object 'System.Collections.Generic.List[System.Drawing.PointF]'
            $sampleCount = [Math]::Max(160, [Math]::Min(1600, [int]($width * 2)))
            for ($i = 0; $i -le $sampleCount; $i++) {
                $norm = [double]$i / [double]$sampleCount
                $x = $left + ($norm * $width)
                $position = $norm * [double]$settings.MaxOutput
                $selectedRate = Get-ShapedStickAxis -Value $position -MaxOutput $settings.MaxOutput -Deadband $settings.Deadband -Expo $settings.Expo -OutputShapingEnabled $settings.OutputShapingEnabled -OutputShapeNodes $settings.OutputShapeNodes -OutputCurve $settings.OutputCurve -ActualCenter $settings.ActualCenter -ActualMax $settings.ActualMax -ActualExpo $settings.ActualExpo
                $linearRate = Get-ShapedStickAxis -Value $position -MaxOutput $settings.MaxOutput -Deadband $settings.Deadband -Expo 0.0
                $smoothRate = (1.0 - $settings.Smoothing) * $selectedRate
                $selectedY = $bottom - ([Math]::Min(1.0, $selectedRate / $maxRate) * $height)
                $linearY = $bottom - ([Math]::Min(1.0, $linearRate / $maxRate) * $height)
                $smoothY = $bottom - ([Math]::Min(1.0, $smoothRate / $maxRate) * $height)
                $selectedPoints.Add([System.Drawing.PointF]::new([single]$x, [single]$selectedY))
                $linearPoints.Add([System.Drawing.PointF]::new([single]$x, [single]$linearY))
                $smoothPoints.Add([System.Drawing.PointF]::new([single]$x, [single]$smoothY))
            }
            if ($linearPoints.Count -gt 1) {
                $g.DrawLines($linearPen, $linearPoints.ToArray())
            }
            if ($selectedPoints.Count -gt 1) {
                $g.DrawLines($curvePen, $selectedPoints.ToArray())
            }
            if ($smoothPoints.Count -gt 1 -and $settings.Smoothing -gt 0.0) {
                $g.DrawLines($smoothPen, $smoothPoints.ToArray())
            }
            $g.DrawString('solid: shaped output  dashed: no expo  dotted: first smoothed tick', $sender.Font, $textBrush, [single]($left + 4), [single]($top + 4))
        } finally {
            $axisPen.Dispose()
            $linearPen.Dispose()
            $curvePen.Dispose()
            $smoothPen.Dispose()
            $textBrush.Dispose()
        }
    })
    $preview.Controls.Add($graph)

    $returnGraph = New-Object System.Windows.Forms.Panel
    $returnGraph.Location = New-Object System.Drawing.Point(16, 372)
    $returnGraph.Size = New-Object System.Drawing.Size(1010, 230)
    $returnGraph.Anchor = 'Top,Left,Right'
    $returnGraph.BackColor = [System.Drawing.Color]::White
    $returnGraph.BorderStyle = [System.Windows.Forms.BorderStyle]::FixedSingle
    $returnGraph.Add_Paint({
        param($sender, $eventArgs)

        $g = $eventArgs.Graphics
        $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
        $left = 58
        $top = 22
        $right = $sender.Width - 22
        $bottom = $sender.Height - 34
        $width = [Math]::Max(1, $right - $left)
        $height = [Math]::Max(1, $bottom - $top)
        $axisPen = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(180, 190, 200), 1)
        $linearPen = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(150, 150, 150), 1)
        $linearPen.DashStyle = [System.Drawing.Drawing2D.DashStyle]::Dash
        $returnPen = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(190, 85, 35), 2)
        $textBrush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(45, 52, 62))
        try {
            $g.DrawLine($axisPen, $left, $bottom, $right, $bottom)
            $g.DrawLine($axisPen, $left, $top, $left, $bottom)
            $g.DrawString('0%', $sender.Font, $textBrush, [single]($left - 10), [single]($bottom + 6))
            $g.DrawString('100%', $sender.Font, $textBrush, [single]($right - 36), [single]($bottom + 6))
            $g.DrawString('return/s', $sender.Font, $textBrush, 6, [single]($top - 4))

            $fullElastic = if ($settings.ElasticEnabled) {
                Get-ElasticReturnRate -Norm 1.0 -MaxOutput $settings.MaxOutput -Coefficient $settings.Coefficient -Mode $settings.Mode -Curve $settings.Curve -ReturnShapingEnabled $settings.ReturnShapingEnabled -ReturnShapeNodes $settings.ReturnShapeNodes
            } else {
                0.0
            }
            $fullLinearElastic = if ($settings.ElasticEnabled) {
                Get-ElasticReturnRate -Norm 1.0 -MaxOutput $settings.MaxOutput -Coefficient $settings.Coefficient -Mode 'linear' -Curve 0.0
            } else {
                0.0
            }
            $fixedRate = $settings.IdleRate + $settings.ConstantRate
            $maxRate = [Math]::Max(1.0, [Math]::Max($fixedRate + $fullElastic, $fixedRate + $fullLinearElastic))
            $returnPoints = New-Object 'System.Collections.Generic.List[System.Drawing.PointF]'
            $linearPoints = New-Object 'System.Collections.Generic.List[System.Drawing.PointF]'
            $sampleCount = [Math]::Max(160, [Math]::Min(1600, [int]($width * 2)))
            for ($i = 0; $i -le $sampleCount; $i++) {
                $norm = [double]$i / [double]$sampleCount
                $x = $left + ($norm * $width)
                $selectedElastic = if ($settings.ElasticEnabled) {
                    Get-ElasticReturnRate -Norm $norm -MaxOutput $settings.MaxOutput -Coefficient $settings.Coefficient -Mode $settings.Mode -Curve $settings.Curve -ReturnShapingEnabled $settings.ReturnShapingEnabled -ReturnShapeNodes $settings.ReturnShapeNodes
                } else {
                    0.0
                }
                $linearElastic = if ($settings.ElasticEnabled) {
                    Get-ElasticReturnRate -Norm $norm -MaxOutput $settings.MaxOutput -Coefficient $settings.Coefficient -Mode 'linear' -Curve 0.0
                } else {
                    0.0
                }
                $selectedY = $bottom - ([Math]::Min(1.0, ($fixedRate + $selectedElastic) / $maxRate) * $height)
                $linearY = $bottom - ([Math]::Min(1.0, ($fixedRate + $linearElastic) / $maxRate) * $height)
                $returnPoints.Add([System.Drawing.PointF]::new([single]$x, [single]$selectedY))
                $linearPoints.Add([System.Drawing.PointF]::new([single]$x, [single]$linearY))
            }
            if ($linearPoints.Count -gt 1) {
                $g.DrawLines($linearPen, $linearPoints.ToArray())
            }
            if ($returnPoints.Count -gt 1) {
                $g.DrawLines($returnPen, $returnPoints.ToArray())
            }
            $g.DrawString(('solid: enabled return, mode={0}, curve={1:0.###}, shape={2}  dashed: linear elastic reference' -f $settings.Mode, $settings.Curve, ($(if ($settings.ReturnShapingEnabled) { 'on' } else { 'off' }))), $sender.Font, $textBrush, [single]($left + 4), [single]($top + 4))
        } finally {
            $axisPen.Dispose()
            $linearPen.Dispose()
            $returnPen.Dispose()
            $textBrush.Dispose()
        }
    })
    $preview.Controls.Add($returnGraph)

    $table = New-Object System.Windows.Forms.TextBox
    $table.Location = New-Object System.Drawing.Point(16, 616)
    $table.Size = New-Object System.Drawing.Size(1010, 172)
    $table.Anchor = 'Top,Left,Right,Bottom'
    $table.Multiline = $true
    $table.ReadOnly = $true
    $table.ScrollBars = [System.Windows.Forms.ScrollBars]::Vertical
    $table.Font = New-Object System.Drawing.Font('Consolas', 9.0)
    $text = New-Object System.Text.StringBuilder
    [void]$text.AppendLine(('mouse stick={0}  roll gain/tick={1:0.###}  pitch gain/tick={2:0.###}' -f ($(if ($settings.MouseEnabled) { 'on' } else { 'off' })), $settings.RollGain, $settings.PitchGain))
    [void]$text.AppendLine(('model={0}  gimbal freq={1:0.###}Hz damping={2:0.###} impulse={3:0.###} friction static/dynamic={4:0.###}/{5:0.###} edge={6:0.###}' -f $settings.PositionModel, $settings.GimbalFrequencyHz, $settings.GimbalDampingRatio, $settings.GimbalInputImpulse, $settings.GimbalStaticFriction, $settings.GimbalDynamicFriction, $settings.GimbalEdgeBumper))
    [void]$text.AppendLine(('input gain={0} slow={1:0.###} fast={2:0.###} speed={3:0.#}..{4:0.#} curve={5:0.###} tracker={6:0.#}ms' -f $settings.InputGainMode, $settings.AdaptiveSlowGain, $settings.AdaptiveFastGain, $settings.AdaptiveSpeedLow, $settings.AdaptiveSpeedHigh, $settings.AdaptiveCurve, $settings.AdaptiveTrackerMs))
    [void]$text.AppendLine(('radial gate={0} diagonal_scale={1:0.###}' -f $settings.GateShape, $settings.DiagonalScale))
    $outputNodesSummary = ConvertTo-StickShapeNodesText -Nodes $settings.OutputShapeNodes
    $returnNodesSummary = ConvertTo-StickShapeNodesText -Nodes $settings.ReturnShapeNodes
    [void]$text.AppendLine(('output shaping={0} nodes={1}' -f $settings.OutputShapingEnabled, $outputNodesSummary))
    [void]$text.AppendLine(('returns: idle={0} {1:0.#}/s after {2:0.#}ms  constant={3} {4:0.#}/s  elastic={5} {6:0.###}/s mode={7} curve={8:0.###} return_shape={9} nodes={10}' -f $settings.IdleEnabled, $settings.IdleRate, $settings.IdleMs, $settings.ConstantEnabled, $settings.ConstantRate, $settings.ElasticEnabled, $settings.Coefficient, $settings.Mode, $settings.Curve, $settings.ReturnShapingEnabled, $returnNodesSummary))
    [void]$text.AppendLine('')
    [void]$text.AppendLine('deflect   raw pos   legacy linear   shaped   first smoothed tick')
    foreach ($row in $rows) {
        [void]$text.AppendLine(('{0,4}%   {1,7:0.0}   {2,7:0.0}   {3,7:0.0}   {4,9:0.0}' -f $row.Percent, $row.Position, $row.Linear, $row.Shaped, $row.Smoothed))
    }
    [void]$text.AppendLine('')
    [void]$text.AppendLine(('elastic curve mode={0} curve={1:0.###}; total includes enabled idle/constant return' -f $settings.Mode, $settings.Curve))
    [void]$text.AppendLine('deflect   elastic/s   linear ref/s   total/s   total/tick')
    foreach ($row in $elasticRows) {
        [void]$text.AppendLine(('{0,4}%   {1,9:0.1}   {2,12:0.1}   {3,7:0.1}   {4,10:0.000}' -f $row.Percent, $row.ElasticRate, $row.LinearElasticRate, $row.TotalRate, $row.PerTick))
    }
    [void]$text.AppendLine('')
    [void]$text.AppendLine('no-input return from full stick; output column includes deadband/expo')
    [void]$text.AppendLine('time_ms   virtual pos   shaped output')
    foreach ($row in $returnRows) {
        [void]$text.AppendLine(('{0,7}   {1,11:0.0}   {2,13:0.0}' -f $row.TimeMs, $row.Position, $row.Output))
    }
    [void]$text.AppendLine('')
    [void]$text.AppendLine('adaptive speed preview')
    [void]$text.AppendLine('counts/s       gain')
    foreach ($row in $adaptiveRows) {
        [void]$text.AppendLine(('{0,8:0.0}   {1,8:0.###}' -f $row.Speed, $row.Gain))
    }
    [void]$text.AppendLine('')
    [void]$text.AppendLine('radial gate preview')
    [void]$text.AppendLine('roll_in pitch_in   roll_out pitch_out scale')
    foreach ($row in $gateRows) {
        [void]$text.AppendLine(('{0,7:0.0} {1,8:0.0}   {2,8:0.0} {3,9:0.0} {4,5:0.###}' -f $row.RollIn, $row.PitchIn, $row.RollOut, $row.PitchOut, $row.Scale))
    }
    [void]$text.AppendLine('')
    [void]$text.AppendLine('40 ms roll flick, then release')
    [void]$text.AppendLine('time_ms dx gain gate position velocity output')
    foreach ($row in $modelRows) {
        [void]$text.AppendLine(('{0,7} {1,2:0} {2,4:0.##} {3,4:0.##} {4,8:0.0} {5,8:0.0} {6,6:0.0}' -f $row.TimeMs, $row.Dx, $row.Gain, $row.Gate, $row.Position, $row.Velocity, $row.Output))
    }
    $table.Text = $text.ToString()
    $preview.Controls.Add($table)

    $close = New-Object System.Windows.Forms.Button
    $close.Text = 'Close'
    $close.Location = New-Object System.Drawing.Point(926, 804)
    $close.Size = New-Object System.Drawing.Size(100, 30)
    $close.Anchor = 'Right,Bottom'
    $close.Add_Click({ $preview.Close() })
    $preview.Controls.Add($close)

    $resizePreview = {
        $client = $preview.ClientSize
        $contentW = [Math]::Max(620, $client.Width - 32)
        $close.Left = $client.Width - 116
        $close.Top = $client.Height - 46
        $summary.Width = $contentW

        $availableH = [Math]::Max(520, $close.Top - 78)
        $graphH = [int][Math]::Max(240, [Math]::Floor($availableH * 0.42))
        $returnH = [int][Math]::Max(210, [Math]::Floor($availableH * 0.32))
        $tableTop = 78 + $graphH + 14 + $returnH + 14
        $tableH = [int][Math]::Max(120, $close.Top - $tableTop - 12)

        $graph.Size = New-Object System.Drawing.Size($contentW, $graphH)
        $returnGraph.Location = New-Object System.Drawing.Point(16, (78 + $graphH + 14))
        $returnGraph.Size = New-Object System.Drawing.Size($contentW, $returnH)
        $table.Location = New-Object System.Drawing.Point(16, $tableTop)
        $table.Size = New-Object System.Drawing.Size($contentW, $tableH)
        $graph.Invalidate()
        $returnGraph.Invalidate()
    }
    $preview.Add_Resize($resizePreview)
    & $resizePreview

    [void]$preview.ShowDialog()
}

if ($SelfTest) {
    $profiles = Load-Profiles
    "root=$root"
    "profiles_dir=$profilesDir"
    "exe_exists=$(Test-Path -LiteralPath $exePath)"
    "default_profile=$(Get-DefaultProfileFileName)"
    "profiles=$($profiles.Count)"
    foreach ($profile in $profiles) {
        "{0} [{1} Hz] {2}" -f $profile.Name, $profile.FrameRate, $profile.FileName
    }
    $testLines = @('[mapper]', 'roll_gain = 10.0', '', '[logging]', 'csv = true')
    $testLines = Set-TomlValue -Lines $testLines -Section 'mapper' -Key 'roll_gain' -Value '12.5'
    $testLines = Set-TomlValue -Lines $testLines -Section 'mapper' -Key 'return_rate' -Value '0'
    if (($testLines -notcontains 'roll_gain = 12.5') -or ($testLines -notcontains 'return_rate = 0')) {
        throw 'Set-TomlValue self-test failed.'
    }
    "toml_update=ok"
    $previewRate = Get-ElasticReturnRate -Norm 0.5 -MaxOutput 512 -Coefficient 10.0 -Mode 'progressive' -Curve 0.5
    if ($previewRate -le 0.0) {
        throw 'elastic preview self-test failed.'
    }
    "elastic_preview=ok"
    Stop-ActiveGx12Run -TimeoutMs 50
    Reset-LauncherStopEvent
    "stop_logic=ok"
    exit 0
}

$form = New-Object System.Windows.Forms.Form
$form.Text = 'GX12 Mouse Launcher'
$form.StartPosition = 'CenterScreen'
$form.Size = New-Object System.Drawing.Size(980, 660)
$form.MinimumSize = New-Object System.Drawing.Size(900, 560)
$form.Font = New-Object System.Drawing.Font('Segoe UI', 9.0)
$form.BackColor = [System.Drawing.SystemColors]::ControlLightLight

$profilesLabel = New-Object System.Windows.Forms.Label
$profilesLabel.Text = 'Tuning profile'
$profilesLabel.Location = New-Object System.Drawing.Point(16, 16)
$profilesLabel.Size = New-Object System.Drawing.Size(160, 22)
$form.Controls.Add($profilesLabel)

$profilesList = New-Object System.Windows.Forms.ListBox
$profilesList.Location = New-Object System.Drawing.Point(16, 42)
$profilesList.Size = New-Object System.Drawing.Size(270, 490)
$profilesList.Anchor = 'Top,Bottom,Left'
$profilesList.DisplayMember = 'Display'
$profilesList.IntegralHeight = $false
$form.Controls.Add($profilesList)

$editor = New-Object System.Windows.Forms.Panel
$editor.Location = New-Object System.Drawing.Point(306, 42)
$editor.Size = New-Object System.Drawing.Size(628, 490)
$editor.Anchor = 'Top,Bottom,Left,Right'
$editor.AutoScroll = $false
$form.Controls.Add($editor)

$profileTitle = New-Object System.Windows.Forms.Label
$profileTitle.Location = New-Object System.Drawing.Point(0, 0)
$profileTitle.Size = New-Object System.Drawing.Size(610, 22)
$profileTitle.Anchor = 'Top,Left,Right'
$profileTitle.Font = New-Object System.Drawing.Font($form.Font, [System.Drawing.FontStyle]::Bold)
$editor.Controls.Add($profileTitle)

$profilePath = New-Object System.Windows.Forms.Label
$profilePath.Location = New-Object System.Drawing.Point(0, 24)
$profilePath.Size = New-Object System.Drawing.Size(610, 34)
$profilePath.Anchor = 'Top,Left,Right'
$editor.Controls.Add($profilePath)

$tabs = New-Object System.Windows.Forms.TabControl
$tabs.Location = New-Object System.Drawing.Point(0, 66)
$tabs.Size = New-Object System.Drawing.Size(620, 420)
$tabs.Anchor = 'Top,Bottom,Left,Right'
$editor.Controls.Add($tabs)

$directTab = New-Object System.Windows.Forms.TabPage
$directTab.Text = 'Right Stick'
$directTab.BackColor = [System.Drawing.SystemColors]::ControlLightLight
$directTab.AutoScroll = $true
[void]$tabs.TabPages.Add($directTab)

$aimTab = New-Object System.Windows.Forms.TabPage
$aimTab.Text = 'Drone aim'
$aimTab.BackColor = [System.Drawing.SystemColors]::ControlLightLight
[void]$tabs.TabPages.Add($aimTab)

$leftStickTab = New-Object System.Windows.Forms.TabPage
$leftStickTab.Text = 'Left stick'
$leftStickTab.BackColor = [System.Drawing.SystemColors]::ControlLightLight
$leftStickTab.AutoScroll = $true
[void]$tabs.TabPages.Add($leftStickTab)

$saveTimer = New-Object System.Windows.Forms.Timer
$saveTimer.Interval = 600
$saveTimer.Add_Tick({
    $saveTimer.Stop()
    Save-SelectedProfile
})

function Queue-TuningSave {
    if ($script:loadingProfile) {
        return
    }
    $saveTimer.Stop()
    $saveTimer.Start()
}

function Flush-PendingTuningSave {
    if ($saveTimer.Enabled) {
        $saveTimer.Stop()
        Save-SelectedProfile
    }
}

function Add-TuningTextBox {
    param(
        [string]$Name,
        [string]$Label,
        [int]$Y,
        [int]$X = 18,
        [System.Windows.Forms.Control]$Parent = $directTab,
        [switch]$KeyBinding,
        [string]$Tip = '',
        [int]$Width = 96
    )

    $labelControl = New-Object System.Windows.Forms.Label
    $labelControl.Text = $Label
    $labelControl.Location = New-Object System.Drawing.Point($X, $Y)
    $labelControl.Size = New-Object System.Drawing.Size(132, 24)
    $labelControl.TextAlign = [System.Drawing.ContentAlignment]::MiddleLeft
      $Parent.Controls.Add($labelControl)
      $script:tuningLabels[$Name] = $labelControl

      $textControl = New-Object System.Windows.Forms.TextBox
      $textControl.Location = New-Object System.Drawing.Point(($X + 142), ($Y - 1))
      $textControl.Size = New-Object System.Drawing.Size($Width, 23)
    $textControl.Anchor = 'Top,Left'
    $textControl.Add_TextChanged({ Queue-TuningSave })
    if ($KeyBinding) {
        $textControl.ReadOnly = $true
        $textControl.Add_Click({
            $this.SelectAll()
            $status.Text = 'Press a key to set this binding.'
        })
        $textControl.Add_Enter({
            $this.SelectAll()
            $status.Text = 'Press a key to set this binding.'
        })
        $textControl.Add_PreviewKeyDown({
            param($sender, $eventArgs)
            $eventArgs.IsInputKey = $true
        })
        $textControl.Add_KeyDown({
            param($sender, $eventArgs)
            $sender.Text = Convert-KeyCodeToProfileKey -KeyCode $eventArgs.KeyCode
            $sender.SelectAll()
            $eventArgs.SuppressKeyPress = $true
            Queue-TuningSave
        })
    }
      $Parent.Controls.Add($textControl)
      if (-not [string]::IsNullOrWhiteSpace($Tip)) {
          $script:toolTip.SetToolTip($labelControl, $Tip)
          $script:toolTip.SetToolTip($textControl, $Tip)
      }
      $script:tuningFields[$Name] = $textControl
  }

function Add-TuningComboBox {
    param(
        [string]$Name,
        [string]$Label,
        [int]$Y,
        [string[]]$Items,
        [int]$X = 18,
        [System.Windows.Forms.Control]$Parent = $directTab,
        [string]$Tip = '',
        [int]$Width = 112
    )

    $labelControl = New-Object System.Windows.Forms.Label
    $labelControl.Text = $Label
    $labelControl.Location = New-Object System.Drawing.Point($X, $Y)
    $labelControl.Size = New-Object System.Drawing.Size(132, 24)
    $labelControl.TextAlign = [System.Drawing.ContentAlignment]::MiddleLeft
    $Parent.Controls.Add($labelControl)
    $script:tuningLabels[$Name] = $labelControl

    $combo = New-Object System.Windows.Forms.ComboBox
    $combo.Location = New-Object System.Drawing.Point(($X + 142), ($Y - 1))
    $combo.Size = New-Object System.Drawing.Size($Width, 23)
    $combo.DropDownStyle = [System.Windows.Forms.ComboBoxStyle]::DropDownList
    [void]$combo.Items.AddRange([object[]]$Items)
    $combo.Add_SelectedIndexChanged({ Queue-TuningSave })
    $Parent.Controls.Add($combo)
    if (-not [string]::IsNullOrWhiteSpace($Tip)) {
        $script:toolTip.SetToolTip($labelControl, $Tip)
        $script:toolTip.SetToolTip($combo, $Tip)
    }
    $script:tuningFields[$Name] = $combo
}

function Add-TuningCheckBox {
    param(
        [string]$Name,
        [string]$Label,
        [int]$X,
        [int]$Y,
        [System.Windows.Forms.Control]$Parent = $directTab,
        [string]$Tip = ''
    )

    $check = New-Object System.Windows.Forms.CheckBox
    $check.Text = $Label
    $check.Location = New-Object System.Drawing.Point($X, $Y)
    $check.Size = New-Object System.Drawing.Size(142, 24)
      $check.Add_CheckedChanged({ Queue-TuningSave })
      $Parent.Controls.Add($check)
      if (-not [string]::IsNullOrWhiteSpace($Tip)) {
          $script:toolTip.SetToolTip($check, $Tip)
      }
      $script:tuningChecks[$Name] = $check
  }

Add-TuningTextBox -Name 'FrameRate' -Label 'Frame rate Hz' -Y 22 -Parent $directTab
Add-TuningCheckBox -Name 'MouseRightStickEnabled' -Label 'Mouse stick' -X 324 -Y 22 -Parent $directTab
Add-TuningTextBox -Name 'StopKey' -Label 'Stop key' -X 324 -Y 538 -Parent $directTab -KeyBinding -Tip 'Key that stops the active run and triggers the same cleanup Escape used to do.'
Add-TuningTextBox -Name 'FreezeKey' -Label 'Mouse freeze key' -X 324 -Y 572 -Parent $directTab -KeyBinding -Tip 'Key that toggles cursor freeze/lock during the active run.'

Add-TuningTextBox -Name 'RollGain' -Label 'Mouse roll sens' -Y 62 -Parent $directTab
Add-TuningTextBox -Name 'PitchGain' -Label 'Mouse pitch sens' -Y 96 -Parent $directTab
Add-TuningTextBox -Name 'MaxOutput' -Label 'Max output' -Y 130 -Parent $directTab
Add-TuningTextBox -Name 'Deadband' -Label 'Deadband' -Y 164 -Parent $directTab
Add-TuningTextBox -Name 'Expo' -Label 'Expo' -Y 198 -Parent $directTab
Add-TuningTextBox -Name 'Smoothing' -Label 'Smoothing' -Y 232 -Parent $directTab
Add-TuningTextBox -Name 'ReturnRate' -Label 'Idle return speed' -Y 266 -Parent $directTab -Tip 'Right-stick recenter speed when Idle stick return is enabled.'
Add-TuningTextBox -Name 'ReturnIdle' -Label 'Idle return delay ms' -Y 300 -Parent $directTab -Tip 'Delay before idle right-stick return starts after mouse movement stops.'
Add-TuningTextBox -Name 'ConstantReturnRate' -Label 'Constant return speed' -Y 334 -Parent $directTab -Tip 'Right-stick recenter speed applied continuously, including while mouse input is moving the virtual stick.'
Add-TuningTextBox -Name 'ElasticReturnCoefficient' -Label 'Elastic return coeff' -Y 368 -Parent $directTab -Tip 'Proportional center spring in 1/s. Higher pulls harder far from center but stays gentle near center.'
Add-TuningTextBox -Name 'ElasticReturnCurve' -Label 'Elastic return curve' -X 324 -Y 334 -Parent $directTab -Tip '0 keeps the current linear spring. Higher values make return lighter near center while preserving full-stick pull.'
$outputShapeLabel = New-Object System.Windows.Forms.Label
$outputShapeLabel.Text = 'Output shape'
$outputShapeLabel.Location = New-Object System.Drawing.Point(18, 402)
$outputShapeLabel.Size = New-Object System.Drawing.Size(110, 20)
$outputShapeLabel.Font = New-Object System.Drawing.Font('Segoe UI', 9.0)
$directTab.Controls.Add($outputShapeLabel)
$script:tuningLabels['OutputShape'] = $outputShapeLabel

$outputShapeEditor = New-Object Gx12Launcher.StickShapeEditor
$outputShapeEditor.Location = New-Object System.Drawing.Point(18, 424)
$outputShapeEditor.Size = New-Object System.Drawing.Size(290, 96)
$outputShapeEditor.CurveColor = [System.Drawing.Color]::FromArgb(26, 115, 232)
$outputShapeEditor.Add_NodesChanged({ Queue-TuningSave })
$directTab.Controls.Add($outputShapeEditor)
$script:tuningFields['OutputShape'] = $outputShapeEditor
$script:toolTip.SetToolTip($outputShapeLabel, 'Experimental output shape curve. Click to add a node, drag to move, scroll to widen, right-click to remove.')
$script:toolTip.SetToolTip($outputShapeEditor, 'Click to add a node, drag to move, scroll to widen, right-click to remove.')

$returnShapeLabel = New-Object System.Windows.Forms.Label
$returnShapeLabel.Text = 'Return shape'
$returnShapeLabel.Location = New-Object System.Drawing.Point(324, 368)
$returnShapeLabel.Size = New-Object System.Drawing.Size(110, 20)
$returnShapeLabel.Font = New-Object System.Drawing.Font('Segoe UI', 9.0)
$directTab.Controls.Add($returnShapeLabel)
$script:tuningLabels['ReturnShape'] = $returnShapeLabel

$returnShapeEditor = New-Object Gx12Launcher.StickShapeEditor
$returnShapeEditor.Location = New-Object System.Drawing.Point(324, 390)
$returnShapeEditor.Size = New-Object System.Drawing.Size(254, 96)
$returnShapeEditor.CurveColor = [System.Drawing.Color]::FromArgb(190, 85, 35)
$returnShapeEditor.Add_NodesChanged({ Queue-TuningSave })
$directTab.Controls.Add($returnShapeEditor)
$script:tuningFields['ReturnShape'] = $returnShapeEditor
$script:toolTip.SetToolTip($returnShapeLabel, 'Experimental elastic return shape curve. Click to add a node, drag to move, scroll to widen, right-click to remove.')
$script:toolTip.SetToolTip($returnShapeEditor, 'Click to add a node, drag to move, scroll to widen, right-click to remove.')
$script:elasticModeExplain = @{
    'progressive' = "Progressive - original behavior.`r`nReturn force scales with deflection raised to the curve exponent.`r`nCurve = 0 acts purely linear (force proportional to deflection); curve > 0 makes the spring soft near center and stronger near the edges. Good default for a forgiving feel."
    'linear'      = "Linear - pure proportional spring.`r`nReturn force is directly proportional to deflection; the curve field is ignored.`r`nFeels like a real centering spring: predictable, symmetric, no soft zone. Best when you want consistent return strength at every stick position."
    'smoothstep'  = "Smoothstep - eased S-curve return.`r`nUses 3x^2 - 2x^3 shaping so the pull is gentle at both ends and firmest in the middle of the throw.`r`nCurve > 0 sharpens the S. Good for smooth, cinematic recenters that don't snap."
    'expo'        = "Expo - exponential ramp.`r`nReturn force grows exponentially with deflection (k = 1 + curve).`r`nVery light near center, increasingly aggressive as you push toward full stick. Use when you want to hover near center but get hard recentering from the edges."
}
Add-TuningComboBox -Name 'ElasticReturnMode' -Label 'Elastic curve mode' -X 324 -Y 470 -Parent $directTab -Items @('progressive', 'linear', 'smoothstep', 'expo') -Tip $script:elasticModeExplain['progressive']
$script:tuningFields.ElasticReturnMode.Add_SelectedIndexChanged({
    $sel = [string]$script:tuningFields.ElasticReturnMode.SelectedItem
    $text = $script:elasticModeExplain[$sel]
    if ([string]::IsNullOrEmpty($text)) { $text = 'Named elastic return algorithm.' }
    $script:toolTip.SetToolTip($script:tuningFields.ElasticReturnMode, $text)
    if ($script:tuningLabels.ContainsKey('ElasticReturnMode')) {
        $script:toolTip.SetToolTip($script:tuningLabels.ElasticReturnMode, $text)
    }
})
Add-TuningCheckBox -Name 'InvertRoll' -Label 'Invert roll' -X 324 -Y 62 -Parent $directTab
Add-TuningCheckBox -Name 'InvertPitch' -Label 'Invert pitch' -X 324 -Y 96 -Parent $directTab
Add-TuningCheckBox -Name 'SwapAxes' -Label 'Swap axes' -X 324 -Y 130 -Parent $directTab
Add-TuningCheckBox -Name 'ReturnEnabled' -Label 'Idle stick return' -X 324 -Y 164 -Parent $directTab -Tip 'When enabled, the virtual right stick recenters after mouse movement stops.'
Add-TuningCheckBox -Name 'ConstantReturnEnabled' -Label 'Constant stick return' -X 324 -Y 198 -Parent $directTab -Tip 'When enabled, the virtual right stick is urged toward center on every mapper tick, even while mouse input is arriving.'
Add-TuningCheckBox -Name 'ElasticReturnEnabled' -Label 'Elastic stick return' -X 324 -Y 232 -Parent $directTab -Tip 'When enabled, return strength scales with current stick deflection so slow mouse input can still move away from center.'
Add-TuningCheckBox -Name 'OutputShapingEnabled' -Label 'Stick output shaping' -X 324 -Y 266 -Parent $directTab -Tip 'Experimental: replace scalar expo with editable output curve points.'
Add-TuningCheckBox -Name 'ReturnShapingEnabled' -Label 'Stick return shaping' -X 324 -Y 300 -Parent $directTab -Tip 'Experimental: replace the selected elastic return curve with editable return-rate points.'

$elasticPreviewButton = New-Object System.Windows.Forms.Button
$elasticPreviewButton.Text = 'Preview stick response'
$elasticPreviewButton.Location = New-Object System.Drawing.Point(324, 504)
$elasticPreviewButton.Size = New-Object System.Drawing.Size(254, 28)
$elasticPreviewButton.Add_Click({ Show-ElasticReturnPreview })
$directTab.Controls.Add($elasticPreviewButton)

$aimLabel = New-Object System.Windows.Forms.Label
$aimLabel.Text = 'War Thunder-style reticle control'
$aimLabel.Location = New-Object System.Drawing.Point(18, 20)
$aimLabel.Size = New-Object System.Drawing.Size(260, 24)
$aimLabel.Font = New-Object System.Drawing.Font($form.Font, [System.Drawing.FontStyle]::Bold)
$aimTab.Controls.Add($aimLabel)
Add-TuningCheckBox -Name 'WarThunderMode' -Label 'Use drone aim for this profile' -X 324 -Y 20 -Parent $aimTab

Add-TuningTextBox -Name 'AimSensitivityX' -Label 'Aim sens X' -Y 62 -Parent $aimTab
Add-TuningTextBox -Name 'AimSensitivityY' -Label 'Aim sens Y' -Y 96 -Parent $aimTab
Add-TuningTextBox -Name 'AimReticleLimit' -Label 'Reticle limit' -Y 130 -Parent $aimTab
Add-TuningTextBox -Name 'AimDeadband' -Label 'Aim deadband' -Y 164 -Parent $aimTab
Add-TuningTextBox -Name 'AimReturnRate' -Label 'Aim return' -Y 198 -Parent $aimTab
Add-TuningTextBox -Name 'AimSmoothing' -Label 'Aim smoothing' -Y 232 -Parent $aimTab
Add-TuningTextBox -Name 'AimRollGain' -Label 'Aim roll gain' -Y 62 -X 324 -Parent $aimTab
Add-TuningTextBox -Name 'AimYawGain' -Label 'Aim yaw gain' -Y 96 -X 324 -Parent $aimTab
Add-TuningTextBox -Name 'AimPitchGain' -Label 'Aim pitch gain' -Y 130 -X 324 -Parent $aimTab
Add-TuningTextBox -Name 'AimRollMax' -Label 'Aim roll max' -Y 164 -X 324 -Parent $aimTab
Add-TuningTextBox -Name 'AimYawMax' -Label 'Aim yaw max' -Y 198 -X 324 -Parent $aimTab
Add-TuningTextBox -Name 'AimPitchMax' -Label 'Aim pitch max' -Y 232 -X 324 -Parent $aimTab
Add-TuningTextBox -Name 'AimSlewRate' -Label 'Aim slew' -Y 266 -X 324 -Parent $aimTab
Add-TuningCheckBox -Name 'AimInvertX' -Label 'Aim invert X' -X 18 -Y 304 -Parent $aimTab
Add-TuningCheckBox -Name 'AimInvertY' -Label 'Aim invert Y' -X 150 -Y 304 -Parent $aimTab

$keyboardLabel = New-Object System.Windows.Forms.Label
$keyboardLabel.Text = 'Keyboard left stick'
$keyboardLabel.Location = New-Object System.Drawing.Point(18, 20)
$keyboardLabel.Size = New-Object System.Drawing.Size(220, 24)
$keyboardLabel.Font = New-Object System.Drawing.Font($form.Font, [System.Drawing.FontStyle]::Bold)
$leftStickTab.Controls.Add($keyboardLabel)

Add-TuningTextBox -Name 'ThrottleUpKey' -Label 'Throttle up key' -Y 62 -Parent $leftStickTab -KeyBinding
Add-TuningTextBox -Name 'ThrottleDownKey' -Label 'Throttle down key' -Y 96 -Parent $leftStickTab -KeyBinding
Add-TuningTextBox -Name 'ThrottleCutKey' -Label 'Throttle cut key' -Y 130 -Parent $leftStickTab -KeyBinding
Add-TuningTextBox -Name 'ThrottleRate' -Label 'Throttle speed' -Y 164 -Parent $leftStickTab -Tip 'Keyboard/Wooting throttle integration speed in trainer units per second.'
Add-TuningTextBox -Name 'ThrottleReturnRate' -Label 'Throttle return speed' -Y 198 -Parent $leftStickTab -Tip 'When throttle return is enabled, how quickly released throttle returns to low.'
Add-TuningTextBox -Name 'YawLeftKey' -Label 'Yaw left key' -Y 62 -X 324 -Parent $leftStickTab -KeyBinding
Add-TuningTextBox -Name 'YawRightKey' -Label 'Yaw right key' -Y 96 -X 324 -Parent $leftStickTab -KeyBinding
Add-TuningTextBox -Name 'YawPulse' -Label 'Keyboard max yaw' -Y 130 -X 324 -Parent $leftStickTab -Tip 'Maximum keyboard/Wooting yaw output. 512 is full left-stick yaw.'
Add-TuningTextBox -Name 'YawSlewRate' -Label 'Keyboard yaw response' -Y 164 -X 324 -Parent $leftStickTab -Tip 'How quickly keyboard/Wooting yaw moves toward its target.'

Add-TuningCheckBox -Name 'KeyboardEnabled' -Label 'Keyboard stick' -X 18 -Y 246 -Parent $leftStickTab
Add-TuningCheckBox -Name 'BlockSelectedKeys' -Label 'Block keys' -X 150 -Y 246 -Parent $leftStickTab
Add-TuningCheckBox -Name 'ThrottleReturnEnabled' -Label 'Throttle return' -X 282 -Y 246 -Parent $leftStickTab
Add-TuningCheckBox -Name 'InvertYaw' -Label 'Invert yaw' -X 432 -Y 246 -Parent $leftStickTab

$mouseLeftLabel = New-Object System.Windows.Forms.Label
$mouseLeftLabel.Text = 'Mouse left stick'
$mouseLeftLabel.Location = New-Object System.Drawing.Point(18, 292)
$mouseLeftLabel.Size = New-Object System.Drawing.Size(220, 24)
$mouseLeftLabel.Font = New-Object System.Drawing.Font($form.Font, [System.Drawing.FontStyle]::Bold)
$leftStickTab.Controls.Add($mouseLeftLabel)

Add-TuningCheckBox -Name 'MouseLeftEnabled' -Label 'Use mouse for left stick' -X 324 -Y 292 -Parent $leftStickTab -Tip 'Use the bound second mouse for throttle/yaw on this profile. This cannot be enabled with Keyboard stick.'
Add-TuningTextBox -Name 'MouseDeviceLeft' -Label 'Left mouse token' -Y 326 -Parent $leftStickTab -Width 420 -Tip 'Use auto to bind the first distinct non-right mouse automatically, or paste a GameInput root token from --mouse-devices-gameinput.'
Add-TuningTextBox -Name 'MouseDeviceRight' -Label 'Right mouse token' -Y 360 -Parent $leftStickTab -Width 420 -Tip 'Use auto to route all mice except the bound left mouse to roll/pitch.'
Add-TuningTextBox -Name 'MouseLeftThrottleRate' -Label 'Mouse throttle sens' -Y 394 -Parent $leftStickTab -Tip 'Left-mouse Y throttle integration speed. Higher means throttle changes faster.'
Add-TuningTextBox -Name 'MouseLeftYawGain' -Label 'Mouse yaw sens' -Y 394 -X 324 -Parent $leftStickTab -Tip 'Left-mouse X yaw sensitivity. This is the main setting when yaw feels too weak or too strong.'
Add-TuningTextBox -Name 'MouseLeftYawMax' -Label 'Mouse max yaw' -Y 428 -Parent $leftStickTab -Tip 'Maximum second-mouse yaw output. 512 is full left-stick yaw.'
Add-TuningTextBox -Name 'MouseLeftYawSlewRate' -Label 'Mouse yaw response' -Y 428 -X 324 -Parent $leftStickTab -Tip 'How quickly second-mouse yaw moves toward the target. 0 is instant/no rate limit.'
Add-TuningTextBox -Name 'MouseLeftYawDeadband' -Label 'Mouse yaw deadband' -Y 462 -Parent $leftStickTab -Tip 'Small yaw outputs below this trainer-unit value are suppressed.'
Add-TuningTextBox -Name 'MouseLeftThrottleReturnRate' -Label 'Mouse throttle return' -Y 462 -X 324 -Parent $leftStickTab -Tip 'When mouse throttle return is enabled, how quickly released throttle returns to low.'
Add-TuningCheckBox -Name 'MouseLeftRequireDevice' -Label 'Require left mouse' -X 18 -Y 496 -Parent $leftStickTab -Tip 'Stop safely if the configured left mouse is not present.'
Add-TuningCheckBox -Name 'MouseLeftThrottleReturnEnabled' -Label 'Mouse throttle return' -X 166 -Y 496 -Parent $leftStickTab -Tip 'Return throttle toward low when the second mouse is idle.'
Add-TuningCheckBox -Name 'MouseLeftYawMapperShapingEnabled' -Label 'Mouse yaw shaping' -X 324 -Y 496 -Parent $leftStickTab -Tip 'Use the left-yaw shaping stack for second-mouse yaw.'
Add-TuningCheckBox -Name 'MouseLeftInvertThrottle' -Label 'Invert mouse throttle' -X 324 -Y 496 -Parent $leftStickTab
Add-TuningCheckBox -Name 'MouseLeftInvertYaw' -Label 'Invert mouse yaw' -X 468 -Y 496 -Parent $leftStickTab
Add-TuningCheckBox -Name 'MouseLeftSwapAxes' -Label 'Swap mouse axes' -X 18 -Y 526 -Parent $leftStickTab

$mouseLeftControlNames = @(
    'MouseDeviceLeft',
    'MouseDeviceRight',
    'MouseLeftThrottleRate',
    'MouseLeftYawGain',
    'MouseLeftYawMax',
    'MouseLeftYawSlewRate',
    'MouseLeftYawDeadband',
    'MouseLeftThrottleReturnRate'
)
$mouseLeftCheckNames = @(
    'MouseLeftRequireDevice',
    'MouseLeftThrottleReturnEnabled',
    'MouseLeftYawMapperShapingEnabled',
    'MouseLeftInvertThrottle',
    'MouseLeftInvertYaw',
    'MouseLeftSwapAxes'
)

function Set-MouseLeftControlsVisible {
    param([bool]$Visible)

    foreach ($name in $mouseLeftControlNames) {
        if ($script:tuningFields.ContainsKey($name)) {
            $script:tuningFields[$name].Visible = $Visible
        }
        if ($script:tuningLabels.ContainsKey($name)) {
            $script:tuningLabels[$name].Visible = $Visible
        }
    }
    foreach ($name in $mouseLeftCheckNames) {
        if ($script:tuningChecks.ContainsKey($name)) {
            $script:tuningChecks[$name].Visible = $Visible
        }
    }
}

$script:tuningChecks.MouseLeftEnabled.Add_CheckedChanged({
    Set-MouseLeftControlsVisible -Visible $script:tuningChecks.MouseLeftEnabled.Checked
    if ($script:tuningChecks.MouseLeftEnabled.Checked -and -not $script:loadingProfile) {
        $script:tuningChecks.KeyboardEnabled.Checked = $false
        if ([string]::IsNullOrWhiteSpace($script:tuningFields.MouseDeviceLeft.Text)) {
            $script:tuningFields.MouseDeviceLeft.Text = 'auto'
        }
        $tabs.SelectedTab = $leftStickTab
    }
})

$script:tuningChecks.KeyboardEnabled.Add_CheckedChanged({
    if ($script:tuningChecks.KeyboardEnabled.Checked -and -not $script:loadingProfile) {
        $script:tuningChecks.MouseLeftEnabled.Checked = $false
    }
})

$aimControlNames = @(
    'AimSensitivityX',
    'AimSensitivityY',
    'AimReticleLimit',
    'AimDeadband',
    'AimReturnRate',
    'AimSmoothing',
    'AimRollGain',
    'AimYawGain',
    'AimPitchGain',
    'AimRollMax',
    'AimYawMax',
    'AimPitchMax',
    'AimSlewRate'
)
$aimCheckNames = @(
    'AimInvertX',
    'AimInvertY'
)
$directMouseControlNames = @(
    'RollGain',
    'PitchGain',
    'MaxOutput',
    'Deadband',
    'Expo',
    'Smoothing',
    'ReturnRate',
    'ReturnIdle',
    'ConstantReturnRate',
    'ElasticReturnCoefficient',
    'ElasticReturnCurve',
    'OutputShape',
    'ReturnShape'
)
$directMouseCheckNames = @(
    'InvertRoll',
    'InvertPitch',
    'ReturnEnabled',
    'ConstantReturnEnabled',
    'ElasticReturnEnabled',
    'OutputShapingEnabled',
    'ReturnShapingEnabled'
)
$keyboardControlRows = @(
    @{ Name = 'ThrottleUpKey'; Offset = 24 },
    @{ Name = 'ThrottleDownKey'; Offset = 58 },
    @{ Name = 'ThrottleCutKey'; Offset = 92 },
    @{ Name = 'ThrottleRate'; Offset = 126 },
    @{ Name = 'ThrottleReturnRate'; Offset = 160 },
    @{ Name = 'YawLeftKey'; Offset = 194 },
    @{ Name = 'YawRightKey'; Offset = 228 },
    @{ Name = 'YawPulse'; Offset = 262 },
    @{ Name = 'YawSlewRate'; Offset = 296 }
)
$keyboardCheckRows = @(
    @{ Name = 'KeyboardEnabled'; X = 270; Offset = 24 },
    @{ Name = 'BlockSelectedKeys'; X = 270; Offset = 58 },
    @{ Name = 'ThrottleReturnEnabled'; X = 270; Offset = 126 },
    @{ Name = 'InvertYaw'; X = 270; Offset = 194 }
)

function Set-DroneAimControlsVisible {
    param([bool]$Visible)

    if ($Visible) {
        $tabs.SelectedTab = $aimTab
    } elseif ($tabs.SelectedTab -eq $aimTab) {
        $tabs.SelectedTab = $directTab
    }
}

$script:tuningChecks.WarThunderMode.Add_CheckedChanged({
    if (-not $script:loadingProfile) {
        Set-DroneAimControlsVisible -Visible $script:tuningChecks.WarThunderMode.Checked
    }
})

$status = New-Object System.Windows.Forms.Label
$status.Location = New-Object System.Drawing.Point(16, 548)
$status.Size = New-Object System.Drawing.Size(918, 32)
$status.Anchor = 'Bottom,Left,Right'
$status.Text = 'Composite trainer is the normal path. It opens a console; press Esc in the gx12mouse run to stop and send neutral SBUS.'
$form.Controls.Add($status)

$startTrainer = New-Object System.Windows.Forms.Button
$startTrainer.Text = 'Start Composite Trainer'
$startTrainer.Location = New-Object System.Drawing.Point(16, 588)
$startTrainer.Size = New-Object System.Drawing.Size(170, 32)
$startTrainer.Anchor = 'Bottom,Left'
$form.Controls.Add($startTrainer)

$refresh = New-Object System.Windows.Forms.Button
$refresh.Text = 'Refresh'
$refresh.Location = New-Object System.Drawing.Point(340, 588)
$refresh.Size = New-Object System.Drawing.Size(90, 32)
$refresh.Anchor = 'Bottom,Left'
$form.Controls.Add($refresh)

$openProfiles = New-Object System.Windows.Forms.Button
$openProfiles.Text = 'Open Profiles'
$openProfiles.Location = New-Object System.Drawing.Point(442, 588)
$openProfiles.Size = New-Object System.Drawing.Size(110, 32)
$openProfiles.Anchor = 'Bottom,Left'
$form.Controls.Add($openProfiles)

$setDefault = New-Object System.Windows.Forms.Button
$setDefault.Text = 'Set Default'
$setDefault.Location = New-Object System.Drawing.Point(564, 588)
$setDefault.Size = New-Object System.Drawing.Size(110, 32)
$setDefault.Anchor = 'Bottom,Left'
$form.Controls.Add($setDefault)

$changeProfiles = New-Object System.Windows.Forms.Button
$changeProfiles.Text = 'Change Folder'
$changeProfiles.Location = New-Object System.Drawing.Point(686, 588)
$changeProfiles.Size = New-Object System.Drawing.Size(120, 32)
$changeProfiles.Anchor = 'Bottom,Left'
$form.Controls.Add($changeProfiles)

$close = New-Object System.Windows.Forms.Button
$close.Text = 'Close'
$close.Location = New-Object System.Drawing.Point(844, 588)
$close.Size = New-Object System.Drawing.Size(90, 32)
$close.Anchor = 'Bottom,Right'
$form.Controls.Add($close)

function Update-Details {
    if ((-not $script:loadingProfile) -and $saveTimer.Enabled) {
        $saveTimer.Stop()
        Save-SelectedProfile
    }

    $profile = $profilesList.SelectedItem
    $script:loadingProfile = $true
    try {
    if ($null -eq $profile) {
        $script:editingProfile = $null
        $profileTitle.Text = 'No profile selected'
        $profilePath.Text = ''
        foreach ($field in $script:tuningFields.Values) {
            $field.Text = ''
        }
        foreach ($check in $script:tuningChecks.Values) {
            $check.Checked = $false
        }
        return
    }

    $script:editingProfile = $profile
    $defaultSuffix = if ($profile.FileName -eq (Get-DefaultProfileFileName)) { ' [default]' } else { '' }
    $profileTitle.Text = "$($profile.Name) tuning$defaultSuffix"
    $profilePath.Text = "$($profile.FileName)`r`n$($profile.FullName)"
    $script:tuningFields.FrameRate.Text = $profile.FrameRate
    $script:tuningFields.StopKey.Text = $profile.StopKey
    $script:tuningFields.FreezeKey.Text = $profile.FreezeKey
    $script:tuningFields.RollGain.Text = $profile.RollGain
    $script:tuningFields.PitchGain.Text = $profile.PitchGain
    $script:tuningFields.MaxOutput.Text = $profile.MaxOutput
    $script:tuningFields.Deadband.Text = $profile.Deadband
    $script:tuningFields.Expo.Text = $profile.Expo
    $script:tuningFields.Smoothing.Text = $profile.Smoothing
    if ($script:tuningFields.ContainsKey('InputFilter')) { $script:tuningFields.InputFilter.Text = $profile.InputFilter }
    if ($script:tuningFields.ContainsKey('OneEuroMinCutoffHz')) { $script:tuningFields.OneEuroMinCutoffHz.Text = $profile.OneEuroMinCutoffHz }
    if ($script:tuningFields.ContainsKey('OneEuroBeta')) { $script:tuningFields.OneEuroBeta.Text = $profile.OneEuroBeta }
    if ($script:tuningFields.ContainsKey('OneEuroDcutoffHz')) { $script:tuningFields.OneEuroDcutoffHz.Text = $profile.OneEuroDcutoffHz }
    if ($script:tuningChecks.ContainsKey('DespikeEnabled')) { $script:tuningChecks.DespikeEnabled.Checked = ((ConvertTo-TomlBool $profile.DespikeEnabled) -eq 'true') }
    if ($script:tuningChecks.ContainsKey('DespikeCountEnabled')) { $script:tuningChecks.DespikeCountEnabled.Checked = ((ConvertTo-TomlBool $profile.DespikeCountEnabled) -eq 'true') }
    if ($script:tuningFields.ContainsKey('DespikeWindow')) { $script:tuningFields.DespikeWindow.Text = $profile.DespikeWindow }
    if ($script:tuningFields.ContainsKey('DespikeThresholdSigma')) { $script:tuningFields.DespikeThresholdSigma.Text = $profile.DespikeThresholdSigma }
    if ($script:tuningFields.ContainsKey('OutputCurve')) { $script:tuningFields.OutputCurve.Text = $profile.OutputCurve }
    if ($script:tuningFields.ContainsKey('ActualCenter')) { $script:tuningFields.ActualCenter.Text = $profile.ActualCenter }
    if ($script:tuningFields.ContainsKey('ActualMax')) { $script:tuningFields.ActualMax.Text = $profile.ActualMax }
    if ($script:tuningFields.ContainsKey('ActualExpo')) { $script:tuningFields.ActualExpo.Text = $profile.ActualExpo }
    $script:tuningFields.ReturnRate.Text = $profile.ReturnRate
    $script:tuningFields.ReturnIdle.Text = $profile.ReturnIdle
    $script:tuningFields.ConstantReturnRate.Text = $profile.ConstantReturnRate
    $script:tuningFields.ElasticReturnMode.Text = $profile.ElasticReturnMode
    $script:tuningFields.ElasticReturnCoefficient.Text = $profile.ElasticReturnCoefficient
    $script:tuningFields.ElasticReturnCurve.Text = $profile.ElasticReturnCurve
    $script:tuningFields.OutputShape.LoadFromTomlValue($profile.OutputShapeNodesText)
    $script:tuningFields.ReturnShape.LoadFromTomlValue($profile.ReturnShapeNodesText)
    $script:tuningChecks.WarThunderMode.Checked = ($profile.ControlMode -eq 'drone_mouse_aim')
    $script:tuningFields.AimSensitivityX.Text = $profile.AimSensitivityX
    $script:tuningFields.AimSensitivityY.Text = $profile.AimSensitivityY
    $script:tuningFields.AimReticleLimit.Text = $profile.AimReticleLimit
    $script:tuningFields.AimDeadband.Text = $profile.AimDeadband
    $script:tuningFields.AimReturnRate.Text = $profile.AimReturnRate
    $script:tuningFields.AimSmoothing.Text = $profile.AimSmoothing
    $script:tuningFields.AimRollGain.Text = $profile.AimRollGain
    $script:tuningFields.AimYawGain.Text = $profile.AimYawGain
    $script:tuningFields.AimPitchGain.Text = $profile.AimPitchGain
    $script:tuningFields.AimRollMax.Text = $profile.AimRollMax
    $script:tuningFields.AimYawMax.Text = $profile.AimYawMax
    $script:tuningFields.AimPitchMax.Text = $profile.AimPitchMax
    $script:tuningFields.AimSlewRate.Text = $profile.AimSlewRate
    $script:tuningFields.ThrottleUpKey.Text = $profile.ThrottleUpKey
    $script:tuningFields.ThrottleDownKey.Text = $profile.ThrottleDownKey
    $script:tuningFields.ThrottleCutKey.Text = $profile.ThrottleCutKey
    $script:tuningFields.ThrottleRate.Text = $profile.ThrottleRate
    $script:tuningFields.ThrottleReturnRate.Text = $profile.ThrottleReturnRate
    $script:tuningFields.YawLeftKey.Text = $profile.YawLeftKey
    $script:tuningFields.YawRightKey.Text = $profile.YawRightKey
    $script:tuningFields.YawPulse.Text = $profile.YawPulse
    $script:tuningFields.YawSlewRate.Text = $profile.YawSlewRate
    $script:tuningFields.MouseDeviceRight.Text = $profile.MouseDeviceRight
    $script:tuningFields.MouseDeviceLeft.Text = $profile.MouseDeviceLeft
    $script:tuningFields.MouseLeftThrottleRate.Text = $profile.MouseLeftThrottleRate
    $script:tuningFields.MouseLeftThrottleReturnRate.Text = $profile.MouseLeftThrottleReturnRate
    $script:tuningFields.MouseLeftYawGain.Text = $profile.MouseLeftYawGain
    $script:tuningFields.MouseLeftYawMax.Text = $profile.MouseLeftYawMax
    $script:tuningFields.MouseLeftYawDeadband.Text = $profile.MouseLeftYawDeadband
    $script:tuningFields.MouseLeftYawSlewRate.Text = $profile.MouseLeftYawSlewRate
    $script:tuningChecks.InvertRoll.Checked = ((ConvertTo-TomlBool $profile.InvertRoll) -eq 'true')
    $script:tuningChecks.InvertPitch.Checked = ((ConvertTo-TomlBool $profile.InvertPitch) -eq 'true')
    $script:tuningChecks.SwapAxes.Checked = ((ConvertTo-TomlBool $profile.SwapAxes) -eq 'true')
    $script:tuningChecks.ReturnEnabled.Checked = ((ConvertTo-TomlBool $profile.ReturnEnabled) -eq 'true')
    $script:tuningChecks.ConstantReturnEnabled.Checked = ((ConvertTo-TomlBool $profile.ConstantReturnEnabled) -eq 'true')
    $script:tuningChecks.ElasticReturnEnabled.Checked = ((ConvertTo-TomlBool $profile.ElasticReturnEnabled) -eq 'true')
    $script:tuningChecks.OutputShapingEnabled.Checked = ((ConvertTo-TomlBool $profile.OutputShapingEnabled) -eq 'true')
    $script:tuningChecks.ReturnShapingEnabled.Checked = ((ConvertTo-TomlBool $profile.ReturnShapingEnabled) -eq 'true')
    $script:tuningChecks.MouseRightStickEnabled.Checked = ((ConvertTo-TomlBool $profile.MouseRightStickEnabled) -eq 'true')
    $script:tuningChecks.AimInvertX.Checked = ((ConvertTo-TomlBool $profile.AimInvertX) -eq 'true')
    $script:tuningChecks.AimInvertY.Checked = ((ConvertTo-TomlBool $profile.AimInvertY) -eq 'true')
    $script:tuningChecks.KeyboardEnabled.Checked = ((ConvertTo-TomlBool $profile.KeyboardEnabled) -eq 'true')
    $script:tuningChecks.MouseLeftEnabled.Checked = ((ConvertTo-TomlBool $profile.MouseLeftEnabled) -eq 'true')
    $script:tuningChecks.MouseLeftRequireDevice.Checked = ((ConvertTo-TomlBool $profile.MouseLeftRequireDevice) -eq 'true')
    $script:tuningChecks.MouseLeftThrottleReturnEnabled.Checked = ((ConvertTo-TomlBool $profile.MouseLeftThrottleReturnEnabled) -eq 'true')
    $script:tuningChecks.MouseLeftInvertThrottle.Checked = ((ConvertTo-TomlBool $profile.MouseLeftInvertThrottle) -eq 'true')
    $script:tuningChecks.MouseLeftInvertYaw.Checked = ((ConvertTo-TomlBool $profile.MouseLeftInvertYaw) -eq 'true')
    $script:tuningChecks.MouseLeftSwapAxes.Checked = ((ConvertTo-TomlBool $profile.MouseLeftSwapAxes) -eq 'true')
    $script:tuningChecks.BlockSelectedKeys.Checked = ((ConvertTo-TomlBool $profile.BlockSelectedKeys) -eq 'true')
    $script:tuningChecks.ThrottleReturnEnabled.Checked = ((ConvertTo-TomlBool $profile.ThrottleReturnEnabled) -eq 'true')
    $script:tuningChecks.InvertYaw.Checked = ((ConvertTo-TomlBool $profile.InvertYaw) -eq 'true')
    Set-DroneAimControlsVisible -Visible $script:tuningChecks.WarThunderMode.Checked
    Set-MouseLeftControlsVisible -Visible $script:tuningChecks.MouseLeftEnabled.Checked
    [void](Validate-TuningControls)
    $status.Text = "Editing $($profile.FileName). Tuning changes auto-save to this profile."
    } finally {
        $script:loadingProfile = $false
    }
}

function Refresh-Profiles {
    $previousSelection = $null
    if ($null -ne $profilesList.SelectedItem) {
        $previousSelection = $profilesList.SelectedItem.FileName
    }
    $profilesList.Items.Clear()
    $profiles = Load-Profiles
    foreach ($profile in $profiles) {
        [void]$profilesList.Items.Add($profile)
    }

    if ($profilesList.Items.Count -gt 0) {
        $defaultIndex = 0
        $targetFileName = if ($previousSelection) { $previousSelection } else { Get-DefaultProfileFileName }
        for ($i = 0; $i -lt $profilesList.Items.Count; ++$i) {
            if ($profilesList.Items[$i].FileName -eq $targetFileName) {
                $defaultIndex = $i
                break
            }
        }
        $profilesList.SelectedIndex = $defaultIndex
    } else {
        $script:editingProfile = $null
        $profileTitle.Text = 'No profiles found'
        $profilePath.Text = $profilesDir
    }
}

function Choose-ProfileDirectory {
    Flush-PendingTuningSave

    $dialog = New-Object System.Windows.Forms.FolderBrowserDialog
    $dialog.Description = 'Choose the folder used for GX12 tuning profiles.'
    $dialog.ShowNewFolderButton = $true
    if (Test-Path -LiteralPath $profilesDir) {
        $dialog.SelectedPath = $profilesDir
    } else {
        $dialog.SelectedPath = $root
    }

    try {
        if ($dialog.ShowDialog($form) -ne [System.Windows.Forms.DialogResult]::OK) {
            return
        }

        $selected = Set-ProfileDirectory -Path $dialog.SelectedPath
        $script:editingProfile = $null
        $status.Text = "Profile folder set to $selected."
        Refresh-Profiles
    } finally {
        $dialog.Dispose()
    }
}

$profilesList.Add_SelectedIndexChanged({ Update-Details })
$refresh.Add_Click({ Refresh-Profiles })
$openProfiles.Add_Click({ Open-ProfileDirectory })
$changeProfiles.Add_Click({ Choose-ProfileDirectory })
$setDefault.Add_Click({
    if ($null -ne $profilesList.SelectedItem) {
        Flush-PendingTuningSave
        Set-DefaultProfileFileName -FileName $profilesList.SelectedItem.FileName
        $status.Text = "Default profile is now $($profilesList.SelectedItem.FileName)."
        $selectedFileName = $profilesList.SelectedItem.FileName
        Refresh-Profiles
        for ($i = 0; $i -lt $profilesList.Items.Count; ++$i) {
            if ($profilesList.Items[$i].FileName -eq $selectedFileName) {
                $profilesList.SelectedIndex = $i
                break
            }
        }
    }
})
$startTrainer.Add_Click({
    if ($null -ne $profilesList.SelectedItem) {
        Flush-PendingTuningSave
        Start-Gx12Process -ProfilePath $profilesList.SelectedItem.FullName -Mode 'CompositeTrainer'
    }
})
$close.Add_Click({ $form.Close() })
$form.Add_FormClosing({
    if ($saveTimer.Enabled) {
        $saveTimer.Stop()
        Save-SelectedProfile
    }
    Stop-ActiveGx12Run
})

Refresh-Profiles
[void]$form.ShowDialog()
