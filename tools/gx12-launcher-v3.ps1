param(
    [switch]$SelfTest,
    [string]$RenderPreview = '',
    [string]$PreviewTab = ''
)

Set-StrictMode -Version 3.0
$ErrorActionPreference = 'Stop'

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

# --- Backend import (shares profile parsing, save logic, process launch) ---
# Imports only the pre-form-build half of the backend so the older WinForms
# shell never builds.
$legacyLauncherPath = Join-Path $PSScriptRoot 'gx12-launcher-backend.ps1'
$legacySource = Get-Content -LiteralPath $legacyLauncherPath -Raw
$backendStart = $legacySource.IndexOf('Set-StrictMode -Version 3.0')
$backendEnd = $legacySource.IndexOf('if ($SelfTest) {')
if ($backendStart -lt 0 -or $backendEnd -le $backendStart) {
    throw "Could not locate backend section in $legacyLauncherPath"
}
$script:gx12LauncherEffectivePath = $PSCommandPath
$backendSource = $legacySource.Substring($backendStart, $backendEnd - $backendStart)
$backendSource = $backendSource.Replace(
    '$root = Split-Path -Parent (Split-Path -Parent $PSCommandPath)',
    '$root = Split-Path -Parent (Split-Path -Parent $script:gx12LauncherEffectivePath)'
)
Invoke-Expression $backendSource

# State the backend touches by name. V3 still owns the populate/save flow,
# but having these initialized keeps strict-mode happy if any backend
# helper (e.g. Validate-TuningControls) is reached during a slice rollout.
$script:tuningFields = @{}
$script:tuningChecks = @{}
$script:tuningLabels = @{}
$script:shapeExpandButtons = @{}
$script:editingProfile = $null
$script:loadingProfile = $false
$script:allProfiles = @()
$script:profilesList = $null
$script:profileSearch = $null
$script:statusText = $null
$script:profileTitle = $null
$script:profileFile = $null
$script:profilePath = $null
$script:editorScroll = $null
$script:editorTabStrip = $null
$script:editorTabNames = @()
$script:editorTabButtons = @{}
$script:editorPages = @{}
$script:selectedEditorTab = ''
$script:overviewSection = $null
$script:rightStickSection = $null
$script:rightStickAdvancedSection = $null
$script:leftStickSection = $null
$script:leftStickMouseSection = $null
$script:leftStickYawSection = $null
$script:droneAimSection = $null
$script:rightStickAdvancedPanel = $null
$script:leftStickYawAdvancedPanel = $null
$script:droneAimContentPanel = $null
$script:rightStickAdvancedExpanded = $false
$script:leftStickYawAdvancedExpanded = $false
$script:droneAimExpanded = $false
$script:leftStickOffRadio = $null
$script:leftStickKeyboardBox = $null
$script:leftStickMouseBox = $null
$script:defaultCheck = $null
$script:runStatusBadge = $null
$script:closeButton = $null
$script:profileDrawErrorReported = $false
$script:saveTimer = $null
$script:pendingTuningNames = New-Object System.Collections.Generic.HashSet[string]
$script:launcherLogPath = Join-Path $root 'logs\gx12-launcher-v3.log'
$script:editorSectionMinWidth = 776
$script:rightStickCoreFieldNames = @('RollGain', 'PitchGain', 'MaxOutput', 'Deadband', 'OutputCurve', 'Expo')
$script:rightStickReturnFieldNames = @('ReturnRate', 'ReturnIdle', 'ConstantReturnRate', 'ElasticReturnMode', 'ElasticReturnCoefficient', 'ElasticReturnCurve')
$script:rightStickCoreCheckNames = @('MouseRightStickEnabled', 'InvertRoll', 'InvertPitch', 'SwapAxes', 'ReturnEnabled', 'ConstantReturnEnabled', 'ElasticReturnEnabled')
$script:rightStickAdvancedFieldNames = @(
    'Smoothing', 'InputFilter', 'OneEuroMinCutoffHz', 'OneEuroBeta',
    'OneEuroDcutoffHz', 'DespikeWindow', 'DespikeThresholdSigma',
    'ActualCenter', 'ActualMax', 'ActualExpo',
    'PositionModel', 'GimbalFrequencyHz', 'GimbalDampingRatio',
    'GimbalInputImpulse', 'GimbalStaticFriction', 'GimbalDynamicFriction',
    'GimbalEdgeBumper', 'GimbalAntiwindupStart', 'GimbalAntiwindupMinGain',
    'InputGainMode', 'AdaptiveSlowGain', 'AdaptiveFastGain',
    'AdaptiveSpeedLow', 'AdaptiveSpeedHigh', 'AdaptiveCurve',
    'AdaptiveTrackerMs', 'GateShape', 'DiagonalScale',
    'OutputShape', 'ReturnShape'
)
$script:rightStickAdvancedCheckNames = @(
    'DespikeEnabled', 'DespikeCountEnabled', 'GimbalAntiwindupEnabled',
    'OutputShapingEnabled', 'ReturnShapingEnabled'
)
$script:fastBoolSaveMap = @{
    MouseRightStickEnabled = [pscustomobject]@{ Section = 'mouse_right_stick'; Key = 'enabled'; Property = 'MouseRightStickEnabled' }
    InvertRoll = [pscustomobject]@{ Section = 'mapper'; Key = 'invert_roll'; Property = 'InvertRoll' }
    InvertPitch = [pscustomobject]@{ Section = 'mapper'; Key = 'invert_pitch'; Property = 'InvertPitch' }
    SwapAxes = [pscustomobject]@{ Section = 'mapper'; Key = 'swap_axes'; Property = 'SwapAxes' }
    ReturnEnabled = [pscustomobject]@{ Section = 'mapper'; Key = 'return_enabled'; Property = 'ReturnEnabled' }
    ConstantReturnEnabled = [pscustomobject]@{ Section = 'mapper'; Key = 'constant_return_enabled'; Property = 'ConstantReturnEnabled' }
    ElasticReturnEnabled = [pscustomobject]@{ Section = 'mapper'; Key = 'elastic_return_enabled'; Property = 'ElasticReturnEnabled' }
    DespikeEnabled = [pscustomobject]@{ Section = 'mapper'; Key = 'despike_enabled'; Property = 'DespikeEnabled' }
    DespikeCountEnabled = [pscustomobject]@{ Section = 'mapper'; Key = 'despike_count_enabled'; Property = 'DespikeCountEnabled' }
    GimbalAntiwindupEnabled = [pscustomobject]@{ Section = 'mapper'; Key = 'gimbal_antiwindup_enabled'; Property = 'GimbalAntiwindupEnabled' }
    OutputShapingEnabled = [pscustomobject]@{ Section = 'mapper'; Key = 'output_shaping_enabled'; Property = 'OutputShapingEnabled' }
    ReturnShapingEnabled = [pscustomobject]@{ Section = 'mapper'; Key = 'return_shaping_enabled'; Property = 'ReturnShapingEnabled' }
}
$script:droneAimFieldNames = @(
    'AimSensitivityX', 'AimSensitivityY', 'AimReticleLimit', 'AimDeadband',
    'AimReturnRate', 'AimSmoothing', 'AimRollGain', 'AimYawGain',
    'AimPitchGain', 'AimRollMax', 'AimYawMax', 'AimPitchMax',
    'AimSlewRate'
)
$script:droneAimCheckNames = @('AimInvertX', 'AimInvertY')
$script:leftStickKeyboardFieldNames = @(
    'KeyboardInputSource', 'ThrottleUpKey', 'ThrottleDownKey', 'ThrottleCutKey',
    'ThrottleRate', 'ThrottleReturnRate', 'YawLeftKey', 'YawRightKey',
    'YawPulse', 'YawSlewRate', 'KeyboardAnalogKeycodeMode',
    'KeyboardAnalogDeadzone', 'KeyboardAnalogCurve', 'KeyboardAnalogMin',
    'KeyboardAnalogMax'
)
$script:leftStickKeyboardCheckNames = @(
    'KeyboardEnabled', 'KeyboardRequireAnalog', 'BlockSelectedKeys',
    'ThrottleReturnEnabled', 'InvertYaw'
)
$script:leftStickMouseFieldNames = @(
    'MouseDeviceLeft', 'MouseDeviceRight', 'MouseLeftThrottleRate',
    'MouseLeftThrottleReturnRate', 'MouseLeftYawGain', 'MouseLeftYawMax',
    'MouseLeftYawDeadband', 'MouseLeftYawSmoothing', 'MouseLeftYawSlewRate',
    'MouseLeftYawReturnRate', 'MouseLeftYawReturnIdle',
    'MouseLeftYawConstantReturnRate', 'MouseLeftYawElasticReturnMode',
    'MouseLeftYawElasticReturnCoefficient', 'MouseLeftYawElasticReturnCurve'
)
$script:leftStickMouseCheckNames = @(
    'MouseLeftEnabled', 'MouseLeftRequireDevice', 'MouseLeftThrottleReturnEnabled',
    'MouseLeftInvertThrottle', 'MouseLeftInvertYaw', 'MouseLeftSwapAxes',
    'MouseLeftYawReturnEnabled', 'MouseLeftYawConstantReturnEnabled',
    'MouseLeftYawElasticReturnEnabled'
)
$script:leftStickMouseAdvancedFieldNames = @(
    'MouseLeftYawInputFilter', 'MouseLeftYawOneEuroMinCutoffHz',
    'MouseLeftYawOneEuroBeta', 'MouseLeftYawOneEuroDcutoffHz',
    'MouseLeftYawDespikeWindow', 'MouseLeftYawDespikeThresholdSigma',
    'MouseLeftYawOutputCurve', 'MouseLeftYawExpo',
    'MouseLeftYawActualCenter', 'MouseLeftYawActualMax',
    'MouseLeftYawActualExpo', 'MouseLeftYawPositionModel',
    'MouseLeftYawGimbalFrequencyHz', 'MouseLeftYawGimbalDampingRatio',
    'MouseLeftYawGimbalInputImpulse', 'MouseLeftYawGimbalStaticFriction',
    'MouseLeftYawGimbalDynamicFriction', 'MouseLeftYawGimbalEdgeBumper',
    'MouseLeftYawGimbalAntiwindupStart', 'MouseLeftYawGimbalAntiwindupMinGain',
    'MouseLeftYawInputGainMode', 'MouseLeftYawAdaptiveSlowGain',
    'MouseLeftYawAdaptiveFastGain', 'MouseLeftYawAdaptiveSpeedLow',
    'MouseLeftYawAdaptiveSpeedHigh', 'MouseLeftYawAdaptiveCurve',
    'MouseLeftYawAdaptiveTrackerMs', 'MouseLeftYawGateShape',
    'MouseLeftYawDiagonalScale', 'MouseLeftYawOutputShape',
    'MouseLeftYawReturnShape'
)
$script:leftStickMouseAdvancedCheckNames = @(
    'MouseLeftYawShapingEnabled', 'MouseLeftYawDespikeEnabled',
    'MouseLeftYawDespikeCountEnabled', 'MouseLeftYawGimbalAntiwindupEnabled',
    'MouseLeftYawOutputShapingEnabled', 'MouseLeftYawReturnShapingEnabled'
)

function Write-LauncherError {
    param(
        [string]$Context,
        [System.Exception]$Exception
    )

    try {
        $logDir = Split-Path -Parent $script:launcherLogPath
        if (-not (Test-Path -LiteralPath $logDir)) {
            New-Item -ItemType Directory -Path $logDir -Force | Out-Null
        }
        $message = '[{0}] {1}: {2}' -f (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'), $Context, $Exception.Message
        Add-Content -LiteralPath $script:launcherLogPath -Value $message -Encoding UTF8
        if ($Exception.StackTrace) {
            Add-Content -LiteralPath $script:launcherLogPath -Value $Exception.StackTrace -Encoding UTF8
        }
    } catch {
    }

    if ($null -ne $script:statusText) {
        $script:statusText.Text = "$Context`: $($Exception.Message)"
    }
}

function Initialize-LauncherExceptionHandlers {
    [System.Windows.Forms.Application]::SetUnhandledExceptionMode([System.Windows.Forms.UnhandledExceptionMode]::CatchException)
    [System.Windows.Forms.Application]::add_ThreadException({
        param($sender, $eventArgs)
        Write-LauncherError -Context 'Launcher UI error' -Exception $eventArgs.Exception
    })
    [System.AppDomain]::CurrentDomain.add_UnhandledException({
        param($sender, $eventArgs)
        $exception = $eventArgs.ExceptionObject -as [System.Exception]
        if ($null -ne $exception) {
            Write-LauncherError -Context 'Launcher fatal error' -Exception $exception
        }
    })
}

# --- Dark theme ---
$script:dark = @{
    BgBase       = [System.Drawing.ColorTranslator]::FromHtml('#0F1115')
    BgSurface    = [System.Drawing.ColorTranslator]::FromHtml('#161922')
    BgElev       = [System.Drawing.ColorTranslator]::FromHtml('#1C202B')
    BgElevHover  = [System.Drawing.ColorTranslator]::FromHtml('#232836')
    BgElevActive = [System.Drawing.ColorTranslator]::FromHtml('#2C3243')
    Border       = [System.Drawing.ColorTranslator]::FromHtml('#262A36')
    BorderStrong = [System.Drawing.ColorTranslator]::FromHtml('#323849')
    Text         = [System.Drawing.ColorTranslator]::FromHtml('#E5E7EB')
    TextMuted    = [System.Drawing.ColorTranslator]::FromHtml('#94A0B0')
    TextFaint    = [System.Drawing.ColorTranslator]::FromHtml('#5F6878')
    Accent       = [System.Drawing.ColorTranslator]::FromHtml('#4F8DFD')
    AccentHover  = [System.Drawing.ColorTranslator]::FromHtml('#67A0FF')
    AccentPress  = [System.Drawing.ColorTranslator]::FromHtml('#3973D9')
    AccentSoft   = [System.Drawing.ColorTranslator]::FromHtml('#1F2A44')
    Success      = [System.Drawing.ColorTranslator]::FromHtml('#36C28E')
    Warn         = [System.Drawing.ColorTranslator]::FromHtml('#F0B85B')
    Danger       = [System.Drawing.ColorTranslator]::FromHtml('#E25C5C')
    DangerHover  = [System.Drawing.ColorTranslator]::FromHtml('#EE7676')
}

if (-not ('Gx12LauncherV3.DarkTheme' -as [type])) {
Add-Type -ReferencedAssemblies @('System.Windows.Forms', 'System.Drawing') -TypeDefinition @'
using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Windows.Forms;

namespace Gx12LauncherV3 {
    public static class DarkTheme {
        public static Color BgBase       = Color.FromArgb(15, 17, 21);
        public static Color BgSurface    = Color.FromArgb(22, 25, 34);
        public static Color BgElev       = Color.FromArgb(28, 32, 43);
        public static Color BgElevHover  = Color.FromArgb(35, 40, 54);
        public static Color BgElevActive = Color.FromArgb(44, 50, 67);
        public static Color Border       = Color.FromArgb(38, 42, 54);
        public static Color BorderStrong = Color.FromArgb(50, 56, 73);
        public static Color Text         = Color.FromArgb(229, 231, 235);
        public static Color TextMuted    = Color.FromArgb(148, 160, 176);
        public static Color TextFaint    = Color.FromArgb(95, 104, 120);
        public static Color Accent       = Color.FromArgb(79, 141, 253);
        public static Color AccentHover  = Color.FromArgb(103, 160, 255);
        public static Color AccentPress  = Color.FromArgb(57, 115, 217);
        public static Color AccentSoft   = Color.FromArgb(31, 42, 68);
        public static Color Success      = Color.FromArgb(54, 194, 142);
        public static Color Warn         = Color.FromArgb(240, 184, 91);
        public static Color Danger       = Color.FromArgb(226, 92, 92);
    }

    public class DarkPanel : Panel {
        private int radius = 8;
        private Color borderColor = DarkTheme.Border;
        private int borderSize = 1;

        public DarkPanel() {
            SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.UserPaint | ControlStyles.OptimizedDoubleBuffer | ControlStyles.ResizeRedraw, true);
            BackColor = DarkTheme.BgSurface;
            ForeColor = DarkTheme.Text;
        }

        public int Radius {
            get { return radius; }
            set { radius = Math.Max(0, value); Invalidate(); }
        }

        public Color BorderColor {
            get { return borderColor; }
            set { borderColor = value; Invalidate(); }
        }

        public int BorderSize {
            get { return borderSize; }
            set { borderSize = Math.Max(0, value); Invalidate(); }
        }

        protected override void OnPaintBackground(PaintEventArgs e) {
            Color bg = Parent != null ? Parent.BackColor : DarkTheme.BgBase;
            using (SolidBrush brush = new SolidBrush(bg)) {
                e.Graphics.FillRectangle(brush, ClientRectangle);
            }
        }

        protected override void OnPaint(PaintEventArgs e) {
            e.Graphics.SmoothingMode = SmoothingMode.AntiAlias;
            Rectangle rect = new Rectangle(0, 0, Width - 1, Height - 1);
            using (GraphicsPath path = RoundedRect(rect, radius)) {
                using (SolidBrush brush = new SolidBrush(BackColor)) {
                    e.Graphics.FillPath(brush, path);
                }
                if (borderSize > 0) {
                    using (Pen pen = new Pen(borderColor, borderSize)) {
                        e.Graphics.DrawPath(pen, path);
                    }
                }
            }
        }

        public static GraphicsPath RoundedRect(Rectangle rect, int radius) {
            GraphicsPath path = new GraphicsPath();
            int d = Math.Max(1, radius * 2);
            if (radius <= 0 || rect.Width <= 0 || rect.Height <= 0) {
                path.AddRectangle(rect);
                path.CloseFigure();
                return path;
            }
            path.AddArc(rect.X, rect.Y, d, d, 180, 90);
            path.AddArc(rect.Right - d, rect.Y, d, d, 270, 90);
            path.AddArc(rect.Right - d, rect.Bottom - d, d, d, 0, 90);
            path.AddArc(rect.X, rect.Bottom - d, d, d, 90, 90);
            path.CloseFigure();
            return path;
        }
    }

    public enum DarkButtonKind {
        Subtle = 0,
        Primary = 1,
        Danger = 2,
        Ghost = 3
    }

    public class DarkButton : Button {
        private bool hover;
        private bool pressed;
        private int radius = 6;
        private int iconCode;
        private DarkButtonKind kind = DarkButtonKind.Subtle;

        public DarkButton() {
            SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.UserPaint | ControlStyles.OptimizedDoubleBuffer | ControlStyles.ResizeRedraw, true);
            FlatStyle = FlatStyle.Flat;
            FlatAppearance.BorderSize = 0;
            TabStop = false;
            Cursor = Cursors.Hand;
            BackColor = DarkTheme.BgElev;
            ForeColor = DarkTheme.Text;
        }

        protected override bool ShowFocusCues { get { return false; } }

        public int Radius { get { return radius; } set { radius = Math.Max(0, value); Invalidate(); } }
        public int IconCode { get { return iconCode; } set { iconCode = value; Invalidate(); } }
        public DarkButtonKind Kind { get { return kind; } set { kind = value; Invalidate(); } }

        protected override void OnMouseEnter(EventArgs e) { hover = true; Invalidate(); base.OnMouseEnter(e); }
        protected override void OnMouseLeave(EventArgs e) { hover = false; pressed = false; Invalidate(); base.OnMouseLeave(e); }
        protected override void OnMouseDown(MouseEventArgs e) { if (e.Button == MouseButtons.Left) { pressed = true; Invalidate(); } base.OnMouseDown(e); }
        protected override void OnMouseUp(MouseEventArgs e) { pressed = false; Invalidate(); base.OnMouseUp(e); }

        protected override void OnPaint(PaintEventArgs e) {
            e.Graphics.SmoothingMode = SmoothingMode.AntiAlias;
            e.Graphics.PixelOffsetMode = PixelOffsetMode.HighQuality;
            Color parent = Parent != null ? Parent.BackColor : DarkTheme.BgBase;
            using (SolidBrush clear = new SolidBrush(parent)) {
                e.Graphics.FillRectangle(clear, ClientRectangle);
            }

            Color fill, border, text;
            switch (kind) {
                case DarkButtonKind.Primary:
                    fill = pressed ? DarkTheme.AccentPress : (hover ? DarkTheme.AccentHover : DarkTheme.Accent);
                    border = fill;
                    text = Color.White;
                    break;
                case DarkButtonKind.Danger:
                    fill = pressed ? DarkTheme.AccentPress : (hover ? Color.FromArgb(238, 118, 118) : DarkTheme.Danger);
                    border = fill;
                    text = Color.White;
                    break;
                case DarkButtonKind.Ghost:
                    fill = hover ? DarkTheme.BgElevHover : parent;
                    border = hover ? DarkTheme.Border : parent;
                    text = DarkTheme.Text;
                    break;
                default:
                    fill = pressed ? DarkTheme.BgElevActive : (hover ? DarkTheme.BgElevHover : DarkTheme.BgElev);
                    border = DarkTheme.Border;
                    text = DarkTheme.Text;
                    break;
            }
            if (!Enabled) {
                fill = DarkTheme.BgElev;
                border = DarkTheme.Border;
                text = DarkTheme.TextFaint;
            }

            Rectangle rect = new Rectangle(0, 0, Math.Max(1, Width - 1), Math.Max(1, Height - 1));
            using (GraphicsPath path = DarkPanel.RoundedRect(rect, radius)) {
                using (SolidBrush brush = new SolidBrush(fill)) {
                    e.Graphics.FillPath(brush, path);
                }
                using (Pen pen = new Pen(border, 1)) {
                    e.Graphics.DrawPath(pen, path);
                }
            }

            string label = Text == null ? string.Empty : Text;
            Size textSize = TextRenderer.MeasureText(label, Font, new Size(1000, 1000), TextFormatFlags.NoPadding);
            int iconWidth = iconCode == 0 ? 0 : 18;
            int gap = (iconCode != 0 && label.Length > 0) ? 8 : 0;
            int totalWidth = iconWidth + gap + textSize.Width;
            int x = Math.Max(10, (Width - totalWidth) / 2);
            int y = (Height - textSize.Height) / 2;

            if (iconCode != 0) {
                using (SolidBrush textBrush = new SolidBrush(text))
                using (Font iconFont = new Font("Segoe MDL2 Assets", 11.0f, FontStyle.Regular, GraphicsUnit.Point)) {
                    string icon = char.ConvertFromUtf32(iconCode);
                    RectangleF iconRect = new RectangleF(x, (Height - 18) / 2 - 1, 18, 18);
                    e.Graphics.DrawString(icon, iconFont, textBrush, iconRect);
                }
                x += iconWidth + gap;
            }

            Rectangle textRect = new Rectangle(x, y, Math.Max(1, Width - x - 8), textSize.Height + 2);
            TextRenderer.DrawText(e.Graphics, label, Font, textRect, text,
                TextFormatFlags.Left | TextFormatFlags.VerticalCenter | TextFormatFlags.NoPadding | TextFormatFlags.EndEllipsis);
        }
    }

    // Wrapper that paints a dark border around a child TextBox so the system
    // textbox border stops glaring white on the dark surface.
    public class DarkInputFrame : Panel {
        private int radius = 6;
        private Color borderColor = DarkTheme.Border;
        private bool focused;

        public DarkInputFrame() {
            SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.UserPaint | ControlStyles.OptimizedDoubleBuffer | ControlStyles.ResizeRedraw, true);
            BackColor = DarkTheme.BgElev;
        }

        public int Radius { get { return radius; } set { radius = Math.Max(0, value); Invalidate(); } }
        public bool ChildFocused { get { return focused; } set { focused = value; Invalidate(); } }

        protected override void OnPaintBackground(PaintEventArgs e) {
            Color bg = Parent != null ? Parent.BackColor : DarkTheme.BgBase;
            using (SolidBrush brush = new SolidBrush(bg)) {
                e.Graphics.FillRectangle(brush, ClientRectangle);
            }
        }

        protected override void OnPaint(PaintEventArgs e) {
            e.Graphics.SmoothingMode = SmoothingMode.AntiAlias;
            Rectangle rect = new Rectangle(0, 0, Width - 1, Height - 1);
            Color border = focused ? DarkTheme.Accent : borderColor;
            using (GraphicsPath path = DarkPanel.RoundedRect(rect, radius)) {
                using (SolidBrush brush = new SolidBrush(BackColor)) {
                    e.Graphics.FillPath(brush, path);
                }
                using (Pen pen = new Pen(border, 1)) {
                    e.Graphics.DrawPath(pen, path);
                }
            }
        }
    }

    public class DarkComboBox : ComboBox {
        private const int WmPaint = 0x000F;
        private const int WmPrintClient = 0x0318;

        public DarkComboBox() {
            SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.UserPaint | ControlStyles.OptimizedDoubleBuffer | ControlStyles.ResizeRedraw, true);
            DropDownStyle = ComboBoxStyle.DropDownList;
            FlatStyle = FlatStyle.Flat;
            DrawMode = DrawMode.OwnerDrawFixed;
            ItemHeight = 22;
            BackColor = DarkTheme.BgElev;
            ForeColor = DarkTheme.Text;
        }

        protected override void OnDrawItem(DrawItemEventArgs e) {
            if (e.Index < 0) {
                return;
            }

            bool selected = (e.State & DrawItemState.Selected) == DrawItemState.Selected;
            Color back = selected ? DarkTheme.AccentSoft : DarkTheme.BgElev;
            Color fore = Enabled ? DarkTheme.Text : DarkTheme.TextFaint;
            using (SolidBrush backBrush = new SolidBrush(back))
            using (SolidBrush foreBrush = new SolidBrush(fore)) {
                e.Graphics.FillRectangle(backBrush, e.Bounds);
                string text = Convert.ToString(Items[e.Index]);
                Rectangle textRect = new Rectangle(e.Bounds.X + 4, e.Bounds.Y, Math.Max(1, e.Bounds.Width - 8), e.Bounds.Height);
                TextRenderer.DrawText(e.Graphics, text, Font, textRect, fore,
                    TextFormatFlags.Left | TextFormatFlags.VerticalCenter | TextFormatFlags.EndEllipsis);
            }
        }

        protected override void OnSelectedIndexChanged(EventArgs e) {
            base.OnSelectedIndexChanged(e);
            Invalidate();
        }

        protected override void OnEnabledChanged(EventArgs e) {
            base.OnEnabledChanged(e);
            Invalidate();
        }

        protected override void OnGotFocus(EventArgs e) {
            base.OnGotFocus(e);
            Invalidate();
        }

        protected override void OnLostFocus(EventArgs e) {
            base.OnLostFocus(e);
            Invalidate();
        }

        protected override void WndProc(ref Message m) {
            base.WndProc(ref m);
            if (m.Msg == WmPaint || m.Msg == WmPrintClient) {
                PaintClosedCombo();
            }
        }

        protected override void OnPaintBackground(PaintEventArgs e) {
            using (SolidBrush brush = new SolidBrush(DarkTheme.BgElev)) {
                e.Graphics.FillRectangle(brush, ClientRectangle);
            }
        }

        protected override void OnPaint(PaintEventArgs e) {
            PaintClosedCombo(e.Graphics);
        }

        private void PaintClosedCombo() {
            if (Width <= 0 || Height <= 0 || !IsHandleCreated) {
                return;
            }

            using (Graphics g = CreateGraphics()) {
                PaintClosedCombo(g);
            }
        }

        private void PaintClosedCombo(Graphics g) {
            Rectangle rect = new Rectangle(0, 0, Width, Height);
            Color fill = Enabled ? DarkTheme.BgElev : DarkTheme.BgSurface;
            Color border = Focused ? DarkTheme.Accent : DarkTheme.Border;
            Color fore = Enabled ? DarkTheme.Text : DarkTheme.TextFaint;
            using (SolidBrush brush = new SolidBrush(fill)) {
                g.FillRectangle(brush, rect);
            }

            Rectangle textRect = new Rectangle(8, 0, Math.Max(1, Width - 30), Height);
            TextRenderer.DrawText(g, Text ?? string.Empty, Font, textRect, fore,
                TextFormatFlags.Left | TextFormatFlags.VerticalCenter | TextFormatFlags.EndEllipsis);

            int midX = Width - 15;
            int midY = Height / 2;
            Point[] arrow = new Point[] {
                new Point(midX - 4, midY - 2),
                new Point(midX + 4, midY - 2),
                new Point(midX, midY + 3)
            };
            using (SolidBrush arrowBrush = new SolidBrush(fore)) {
                g.FillPolygon(arrowBrush, arrow);
            }
            using (Pen pen = new Pen(border, 1)) {
                g.DrawRectangle(pen, 0, 0, Math.Max(1, Width - 1), Math.Max(1, Height - 1));
            }
        }
    }

    public class DarkCheckBox : CheckBox {
        private bool hover;
        private bool pressed;

        public DarkCheckBox() {
            SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.UserPaint | ControlStyles.OptimizedDoubleBuffer | ControlStyles.ResizeRedraw | ControlStyles.SupportsTransparentBackColor, true);
            Cursor = Cursors.Hand;
            BackColor = Color.Transparent;
            ForeColor = DarkTheme.Text;
            TabStop = false;
        }

        protected override bool ShowFocusCues { get { return false; } }
        protected override void OnPaintBackground(PaintEventArgs pevent) {
            ClearBackground(pevent.Graphics);
        }
        protected override void OnMouseEnter(EventArgs e) { hover = true; Invalidate(); base.OnMouseEnter(e); }
        protected override void OnMouseLeave(EventArgs e) { hover = false; pressed = false; Invalidate(); base.OnMouseLeave(e); }
        protected override void OnMouseDown(MouseEventArgs e) { if (e.Button == MouseButtons.Left) { pressed = true; Invalidate(); } base.OnMouseDown(e); }
        protected override void OnMouseUp(MouseEventArgs e) { pressed = false; Invalidate(); base.OnMouseUp(e); }
        protected override void OnCheckedChanged(EventArgs e) { Invalidate(); base.OnCheckedChanged(e); }
        protected override void OnEnabledChanged(EventArgs e) { Invalidate(); base.OnEnabledChanged(e); }

        private void ClearBackground(Graphics g) {
            Color back = Parent != null ? Parent.BackColor : DarkTheme.BgBase;
            using (SolidBrush brush = new SolidBrush(back)) {
                g.FillRectangle(brush, ClientRectangle);
            }
        }

        protected override void OnPaint(PaintEventArgs e) {
            PaintDarkToggle(e.Graphics, false);
        }

        protected void PaintDarkToggle(Graphics g, bool radio) {
            ClearBackground(g);
            g.SmoothingMode = SmoothingMode.AntiAlias;
            int boxSize = 16;
            Rectangle box = new Rectangle(1, Math.Max(1, (Height - boxSize) / 2), boxSize, boxSize);
            Color fill = Enabled ? (pressed ? DarkTheme.BgElevActive : (hover ? DarkTheme.BgElevHover : DarkTheme.BgElev)) : DarkTheme.BgSurface;
            Color border = Checked && Enabled ? DarkTheme.Accent : (hover && Enabled ? DarkTheme.BorderStrong : DarkTheme.Border);
            Color text = Enabled ? DarkTheme.Text : DarkTheme.TextFaint;

            if (Checked && Enabled) {
                fill = DarkTheme.Accent;
            }

            using (SolidBrush brush = new SolidBrush(fill))
            using (Pen pen = new Pen(border, 1)) {
                if (radio) {
                    g.FillEllipse(brush, box);
                    g.DrawEllipse(pen, box);
                } else {
                    using (GraphicsPath path = DarkPanel.RoundedRect(box, 4)) {
                        g.FillPath(brush, path);
                        g.DrawPath(pen, path);
                    }
                }
            }

            if (Checked) {
                if (radio) {
                    Rectangle dot = new Rectangle(box.X + 5, box.Y + 5, 6, 6);
                    using (SolidBrush dotBrush = new SolidBrush(Enabled ? Color.White : DarkTheme.TextFaint)) {
                        g.FillEllipse(dotBrush, dot);
                    }
                } else {
                    Point[] check = new Point[] {
                        new Point(box.X + 4, box.Y + 8),
                        new Point(box.X + 7, box.Y + 11),
                        new Point(box.X + 12, box.Y + 5)
                    };
                    using (Pen checkPen = new Pen(Enabled ? Color.White : DarkTheme.TextFaint, 2)) {
                        checkPen.StartCap = LineCap.Round;
                        checkPen.EndCap = LineCap.Round;
                        checkPen.LineJoin = LineJoin.Round;
                        g.DrawLines(checkPen, check);
                    }
                }
            }

            Rectangle textRect = new Rectangle(24, 0, Math.Max(1, Width - 25), Height);
            TextRenderer.DrawText(g, Text ?? string.Empty, Font, textRect, text,
                TextFormatFlags.Left | TextFormatFlags.VerticalCenter | TextFormatFlags.EndEllipsis);
        }
    }

    public class DarkRadioButton : RadioButton {
        private bool hover;
        private bool pressed;

        public DarkRadioButton() {
            SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.UserPaint | ControlStyles.OptimizedDoubleBuffer | ControlStyles.ResizeRedraw | ControlStyles.SupportsTransparentBackColor, true);
            Cursor = Cursors.Hand;
            BackColor = Color.Transparent;
            ForeColor = DarkTheme.Text;
            TabStop = false;
        }

        protected override bool ShowFocusCues { get { return false; } }
        protected override void OnPaintBackground(PaintEventArgs pevent) {
            ClearBackground(pevent.Graphics);
        }
        protected override void OnMouseEnter(EventArgs e) { hover = true; Invalidate(); base.OnMouseEnter(e); }
        protected override void OnMouseLeave(EventArgs e) { hover = false; pressed = false; Invalidate(); base.OnMouseLeave(e); }
        protected override void OnMouseDown(MouseEventArgs e) { if (e.Button == MouseButtons.Left) { pressed = true; Invalidate(); } base.OnMouseDown(e); }
        protected override void OnMouseUp(MouseEventArgs e) { pressed = false; Invalidate(); base.OnMouseUp(e); }
        protected override void OnCheckedChanged(EventArgs e) { Invalidate(); base.OnCheckedChanged(e); }
        protected override void OnEnabledChanged(EventArgs e) { Invalidate(); base.OnEnabledChanged(e); }

        private void ClearBackground(Graphics g) {
            Color back = Parent != null ? Parent.BackColor : DarkTheme.BgBase;
            using (SolidBrush brush = new SolidBrush(back)) {
                g.FillRectangle(brush, ClientRectangle);
            }
        }

        protected override void OnPaint(PaintEventArgs e) {
            ClearBackground(e.Graphics);
            e.Graphics.SmoothingMode = SmoothingMode.AntiAlias;
            int boxSize = 16;
            Rectangle box = new Rectangle(1, Math.Max(1, (Height - boxSize) / 2), boxSize, boxSize);
            Color fill = Enabled ? (pressed ? DarkTheme.BgElevActive : (hover ? DarkTheme.BgElevHover : DarkTheme.BgElev)) : DarkTheme.BgSurface;
            Color border = Checked && Enabled ? DarkTheme.Accent : (hover && Enabled ? DarkTheme.BorderStrong : DarkTheme.Border);
            Color text = Enabled ? DarkTheme.Text : DarkTheme.TextFaint;

            using (SolidBrush brush = new SolidBrush(fill))
            using (Pen pen = new Pen(border, 1)) {
                e.Graphics.FillEllipse(brush, box);
                e.Graphics.DrawEllipse(pen, box);
            }

            if (Checked) {
                Rectangle dot = new Rectangle(box.X + 5, box.Y + 5, 6, 6);
                using (SolidBrush dotBrush = new SolidBrush(Enabled ? DarkTheme.Accent : DarkTheme.TextFaint)) {
                    e.Graphics.FillEllipse(dotBrush, dot);
                }
            }

            Rectangle textRect = new Rectangle(24, 0, Math.Max(1, Width - 25), Height);
            TextRenderer.DrawText(e.Graphics, Text ?? string.Empty, Font, textRect, text,
                TextFormatFlags.Left | TextFormatFlags.VerticalCenter | TextFormatFlags.EndEllipsis);
        }
    }
}
'@
}

# --- PowerShell construction helpers ---
function New-DarkFont {
    param(
        [double]$Size = 9.5,
        [System.Drawing.FontStyle]$Style = [System.Drawing.FontStyle]::Regular
    )
    New-Object System.Drawing.Font('Segoe UI', $Size, $Style)
}

function New-IconFont {
    param([double]$Size = 12.0)
    New-Object System.Drawing.Font('Segoe MDL2 Assets', $Size)
}

function New-DarkLabel {
    param(
        [string]$Text,
        [int]$X,
        [int]$Y,
        [int]$W,
        [int]$H,
        [double]$Size = 9.5,
        [System.Drawing.FontStyle]$Style = [System.Drawing.FontStyle]::Regular,
        [System.Drawing.Color]$Color = $script:dark.Text,
        [System.Drawing.ContentAlignment]$Align = [System.Drawing.ContentAlignment]::MiddleLeft
    )
    $label = New-Object System.Windows.Forms.Label
    $label.Text = $Text
    $label.Location = New-Object System.Drawing.Point($X, $Y)
    $label.Size = New-Object System.Drawing.Size($W, $H)
    $label.Font = New-DarkFont -Size $Size -Style $Style
    $label.ForeColor = $Color
    $label.BackColor = [System.Drawing.Color]::Transparent
    $label.TextAlign = $Align
    $label.AutoEllipsis = $true
    return $label
}

function New-DarkPanel {
    param(
        [int]$X = 0,
        [int]$Y = 0,
        [int]$W = 100,
        [int]$H = 100,
        [int]$Radius = 8,
        [System.Drawing.Color]$BackColor = $script:dark.BgSurface,
        [System.Drawing.Color]$BorderColor = $script:dark.Border,
        [int]$BorderSize = 1
    )
    $panel = New-Object Gx12LauncherV3.DarkPanel
    $panel.Location = New-Object System.Drawing.Point($X, $Y)
    $panel.Size = New-Object System.Drawing.Size($W, $H)
    $panel.Radius = $Radius
    $panel.BackColor = $BackColor
    $panel.BorderColor = $BorderColor
    $panel.BorderSize = $BorderSize
    return $panel
}

function New-DarkButton {
    param(
        [string]$Text,
        [int]$Icon = 0,
        [int]$Width = 130,
        [int]$Height = 34,
        [ValidateSet('Subtle', 'Primary', 'Danger', 'Ghost')]
        [string]$Kind = 'Subtle'
    )
    $button = New-Object Gx12LauncherV3.DarkButton
    $button.Text = $Text
    $button.IconCode = $Icon
    $button.Kind = [Gx12LauncherV3.DarkButtonKind]::$Kind
    $button.Size = New-Object System.Drawing.Size($Width, $Height)
    $button.Font = New-DarkFont -Size 9.5
    return $button
}

function New-DarkInput {
    param(
        [int]$X,
        [int]$Y,
        [int]$W,
        [int]$H = 30,
        [int]$LeftPad = 10,
        [int]$RightPad = 10
    )
    $frame = New-Object Gx12LauncherV3.DarkInputFrame
    $frame.Location = New-Object System.Drawing.Point($X, $Y)
    $frame.Size = New-Object System.Drawing.Size($W, $H)
    $frame.Radius = 6

    $box = New-Object System.Windows.Forms.TextBox
    $box.BorderStyle = [System.Windows.Forms.BorderStyle]::None
    $box.BackColor = $frame.BackColor
    $box.ForeColor = $script:dark.Text
    $box.Font = New-DarkFont -Size 10.0
    $boxHeight = [int]([Math]::Round($box.PreferredHeight))
    $box.Size = New-Object System.Drawing.Size(($W - $LeftPad - $RightPad), $boxHeight)
    $box.Location = New-Object System.Drawing.Point($LeftPad, ([int](($H - $boxHeight) / 2)))
    $box.Anchor = 'Top,Left,Right'
    $box.Add_Enter({ $this.Parent.ChildFocused = $true })
    $box.Add_Leave({ $this.Parent.ChildFocused = $false })
    $frame.Controls.Add($box)

    return [pscustomobject]@{ Frame = $frame; TextBox = $box }
}

function ConvertTo-V3TomlString {
    param([AllowNull()][string]$Text)

    $safe = if ($null -eq $Text) { '' } else { $Text }
    $safe = $safe.Replace('\', '\\').Replace('"', '\"')
    return ('"{0}"' -f $safe)
}

function Set-V3TuningFieldValid {
    param(
        [string]$Name,
        [bool]$Valid
    )

    if (-not $script:tuningFields.ContainsKey($Name)) {
        return
    }

    $control = $script:tuningFields[$Name]
    if ($null -eq $control) {
        return
    }

    $bg = if ($Valid) {
        if ($control.Enabled) { $script:dark.BgElev } else { $script:dark.BgSurface }
    } else {
        [System.Drawing.ColorTranslator]::FromHtml('#3A2028')
    }
    if ($control.BackColor -ne $bg) {
        $control.BackColor = $bg
    }
    if ($control.Parent -is [Gx12LauncherV3.DarkInputFrame]) {
        if ($control.Parent.BackColor -ne $bg) {
            $control.Parent.BackColor = $bg
            $control.Parent.Invalidate()
        }
    }
}

function Set-OverviewControlsEnabled {
    param([bool]$Enabled)

    foreach ($name in @('ProfileName', 'FrameRate', 'StopKey', 'FreezeKey')) {
        if ($script:tuningFields.ContainsKey($name)) {
            $script:tuningFields[$name].Enabled = $Enabled
            if ($script:tuningFields[$name].Parent -is [Gx12LauncherV3.DarkInputFrame]) {
                $script:tuningFields[$name].Parent.Enabled = $Enabled
            }
        }
    }
    if ($script:tuningChecks.ContainsKey('WarThunderMode')) {
        $script:tuningChecks.WarThunderMode.Enabled = $Enabled
    }
    if ($null -ne $script:defaultCheck) {
        $script:defaultCheck.Enabled = $Enabled
    }
}

function Set-V3NamedControlEnabled {
    param(
        [string]$Name,
        [bool]$Enabled
    )

    if ($script:tuningFields.ContainsKey($Name)) {
        $field = $script:tuningFields[$Name]
        if ($field.Enabled -ne $Enabled) {
            $field.Enabled = $Enabled
        }
        if ($field -is [Gx12Launcher.StickShapeEditor]) {
            if ($field.ShapingEnabled -ne $Enabled) {
                $field.ShapingEnabled = $Enabled
            }
        }
        if ($field.Parent -is [Gx12LauncherV3.DarkInputFrame]) {
            if ($field.Parent.Enabled -ne $Enabled) {
                $field.Parent.Enabled = $Enabled
            }
            $frameBack = if ($Enabled) { $script:dark.BgElev } else { $script:dark.BgSurface }
            if ($field.Parent.BackColor -ne $frameBack) {
                $field.Parent.BackColor = $frameBack
                $field.Parent.Invalidate()
            }
        }
    }
    if ($script:tuningLabels.ContainsKey($Name)) {
        $labelColor = if ($Enabled) { $script:dark.TextMuted } else { $script:dark.TextFaint }
        if ($script:tuningLabels[$Name].ForeColor -ne $labelColor) {
            $script:tuningLabels[$Name].ForeColor = $labelColor
        }
    }
    if ($script:tuningChecks.ContainsKey($Name)) {
        if ($script:tuningChecks[$Name].Enabled -ne $Enabled) {
            $script:tuningChecks[$Name].Enabled = $Enabled
        }
        $checkColor = if ($Enabled) { $script:dark.Text } else { $script:dark.TextFaint }
        if ($script:tuningChecks[$Name].ForeColor -ne $checkColor) {
            $script:tuningChecks[$Name].ForeColor = $checkColor
        }
    }
    if ($script:shapeExpandButtons.ContainsKey($Name)) {
        if ($script:shapeExpandButtons[$Name].Enabled -ne $Enabled) {
            $script:shapeExpandButtons[$Name].Enabled = $Enabled
        }
    }
}

function Set-V3ShapeEditorState {
    param(
        [string]$Name,
        [bool]$Available,
        [bool]$Active
    )

    if ($script:tuningFields.ContainsKey($Name)) {
        $field = $script:tuningFields[$Name]
        if ($field.Enabled -ne $Available) {
            $field.Enabled = $Available
        }
        if ($field -is [Gx12Launcher.StickShapeEditor]) {
            if ($field.ShapingEnabled -ne $Active) {
                $field.ShapingEnabled = $Active
            }
        }
    }
    if ($script:tuningLabels.ContainsKey($Name)) {
        $labelColor = if ($Available) { $script:dark.TextMuted } else { $script:dark.TextFaint }
        if ($script:tuningLabels[$Name].ForeColor -ne $labelColor) {
            $script:tuningLabels[$Name].ForeColor = $labelColor
        }
    }
    if ($script:shapeExpandButtons.ContainsKey($Name)) {
        if ($script:shapeExpandButtons[$Name].Enabled -ne $Available) {
            $script:shapeExpandButtons[$Name].Enabled = $Available
        }
    }
}

function Test-V3ChoiceField {
    param(
        [string]$Text,
        [string[]]$Choices
    )

    $value = $Text.Trim().Trim('"').ToLowerInvariant()
    return ($Choices -contains $value)
}

function Show-V3StickShapeEditorDialog {
    param(
        [string]$Name,
        [string]$Title
    )

    if (-not $script:tuningFields.ContainsKey($Name)) {
        return
    }
    $source = $script:tuningFields[$Name]
    if (-not ($source -is [Gx12Launcher.StickShapeEditor])) {
        return
    }

    $dialog = New-Object System.Windows.Forms.Form
    $dialog.Text = $Title
    $dialog.StartPosition = 'CenterParent'
    $dialog.Size = New-Object System.Drawing.Size(920, 660)
    $dialog.MinimumSize = New-Object System.Drawing.Size(720, 520)
    $dialog.Font = New-DarkFont -Size 9.5
    $dialog.BackColor = $script:dark.BgBase
    $dialog.ForeColor = $script:dark.Text

    $label = New-DarkLabel -Text $Title -X 18 -Y 16 -W 760 -H 30 -Size 11.2 -Style ([System.Drawing.FontStyle]::Bold)
    $label.Anchor = 'Top,Left,Right'
    $dialog.Controls.Add($label)

    $editor = New-Object Gx12Launcher.StickShapeEditor
    $editor.Location = New-Object System.Drawing.Point(18, 58)
    $editor.Size = New-Object System.Drawing.Size(866, 506)
    $editor.Anchor = 'Top,Bottom,Left,Right'
    $editor.CurveColor = $source.CurveColor
    $editor.UseDarkTheme = $true
    $editor.Hint = "Click to add a node. Drag to move. Scroll to widen.`nRight-click to remove."
    $editor.LoadFromTomlValue($source.SaveToTomlValue())
    $editor.Add_NodesChanged({
        $source.LoadFromTomlValue($editor.SaveToTomlValue())
        Queue-TuningSave
    })
    $dialog.Controls.Add($editor)

    $close = New-DarkButton -Text 'Close' -Icon 0xE8BB -Width 110 -Height 34 -Kind 'Subtle'
    $close.Location = New-Object System.Drawing.Point(774, 578)
    $close.Anchor = 'Right,Bottom'
    $close.Add_Click({ $dialog.Close() })
    $dialog.Controls.Add($close)

    [void]$dialog.ShowDialog($script:form)
}

function New-V3StickShapeEditor {
    param(
        [string]$Name,
        [string]$Label,
        [int]$X,
        [int]$Y,
        [System.Windows.Forms.Control]$Parent,
        [System.Drawing.Color]$CurveColor,
        [string]$Title,
        [string]$Tip = '',
        [int]$Width = 220,
        [int]$Height = 96
    )

    $labelControl = New-DarkLabel -Text $Label -X $X -Y $Y -W ([Math]::Max(100, $Width - 100)) -H 24 -Size 9.6 -Color $script:dark.TextMuted
    $Parent.Controls.Add($labelControl)
    $script:tuningLabels[$Name] = $labelControl

    $expand = New-DarkButton -Text 'Expand' -Icon 0xE740 -Width 88 -Height 28 -Kind 'Ghost'
    $expand.Location = New-Object System.Drawing.Point(($X + $Width - 88), ($Y - 2))
    $expand.Tag = [pscustomobject]@{ Name = $Name; Title = $Title }
    $expand.Add_Click({
        param($sender, $eventArgs)
        Show-V3StickShapeEditorDialog -Name ([string]$sender.Tag.Name) -Title ([string]$sender.Tag.Title)
    })
    $Parent.Controls.Add($expand)
    $script:shapeExpandButtons[$Name] = $expand

    $editor = New-Object Gx12Launcher.StickShapeEditor
    $editor.Location = New-Object System.Drawing.Point($X, ($Y + 28))
    $editor.Size = New-Object System.Drawing.Size($Width, $Height)
    $editor.CurveColor = $CurveColor
    $editor.UseDarkTheme = $true
    $editor.Hint = "Click to add. Drag to move.`nRight-click removes."
    $editor.Add_NodesChanged({ Queue-TuningSave })
    $Parent.Controls.Add($editor)
    $script:tuningFields[$Name] = $editor

    if (-not [string]::IsNullOrWhiteSpace($Tip)) {
        $script:toolTip.SetToolTip($labelControl, $Tip)
        $script:toolTip.SetToolTip($editor, $Tip)
        $script:toolTip.SetToolTip($expand, $Tip)
    }
    return $editor
}

function New-V3EditorTabPage {
    param(
        [string]$Name,
        [string]$Text
    )

    $button = New-DarkButton -Text $Text -Width 104 -Height 34 -Kind 'Ghost'
    $button.Tag = $Name
    $button.Add_Click({
        param($sender, $eventArgs)
        Select-V3EditorTab -Name ([string]$sender.Tag)
    })
    $script:editorTabStrip.Controls.Add($button)
    $script:editorTabButtons[$Name] = $button
    $script:editorTabNames += $Name

    $page = New-Object System.Windows.Forms.Panel
    $page.BackColor = $script:dark.BgSurface
    $page.Dock = [System.Windows.Forms.DockStyle]::Fill
    $page.AutoScroll = $true
    $page.Visible = $false
    $script:editorScroll.Controls.Add($page)
    $script:editorPages[$Name] = $page
    return $page
}

function Select-V3EditorTab {
    param([string]$Name)

    if (-not $script:editorPages.ContainsKey($Name)) {
        return
    }

    $script:selectedEditorTab = $Name
    foreach ($tabName in $script:editorTabNames) {
        $isSelected = ($tabName -eq $Name)
        $script:editorPages[$tabName].Visible = $isSelected
        $script:editorTabButtons[$tabName].Kind = if ($isSelected) {
            [Gx12LauncherV3.DarkButtonKind]::Subtle
        } else {
            [Gx12LauncherV3.DarkButtonKind]::Ghost
        }
        $script:editorTabButtons[$tabName].Invalidate()
    }
}

function Update-V3EditorTabsLayout {
    if ($null -eq $script:editorTabStrip -or $script:editorTabNames.Count -eq 0) {
        return
    }

    $gap = 6
    $count = $script:editorTabNames.Count
    $available = [Math]::Max(1, $script:editorTabStrip.ClientSize.Width - (($count - 1) * $gap))
    $tabWidth = [Math]::Max(84, [Math]::Floor($available / $count))
    $tabWidth = [Math]::Min(118, $tabWidth)
    $x = 0
    foreach ($tabName in $script:editorTabNames) {
        $button = $script:editorTabButtons[$tabName]
        $button.Location = New-Object System.Drawing.Point($x, 0)
        $button.Size = New-Object System.Drawing.Size($tabWidth, 34)
        $x += $tabWidth + $gap
    }
}

function New-V3TabSection {
    param(
        [System.Windows.Forms.Control]$Parent,
        [string]$Title,
        [int]$W,
        [int]$H
    )

    $section = New-DarkPanel -X 0 -Y 0 -W $W -H $H -Radius 8 -BackColor $script:dark.BgElev -BorderColor $script:dark.Border
    $section.Anchor = 'Top,Left,Right'
    $Parent.Controls.Add($section)
    $titleLabel = New-DarkLabel -Text $Title -X 20 -Y 14 -W 300 -H 26 -Size 11.0 -Style ([System.Drawing.FontStyle]::Bold)
    $section.Controls.Add($titleLabel)
    return $section
}

function Hide-V3DirectSectionButton {
    param(
        [System.Windows.Forms.Control]$Section,
        [string]$Text
    )

    if ($null -eq $Section) {
        return
    }
    foreach ($control in @($Section.Controls)) {
        if (($control -is [Gx12LauncherV3.DarkButton]) -and ([string]$control.Text -eq $Text)) {
            $control.Visible = $false
            $control.Enabled = $false
        }
    }
}

function Move-V3Field {
    param(
        [string]$Name,
        [int]$X,
        [int]$Y
    )

    if ($script:tuningLabels.ContainsKey($Name)) {
        $script:tuningLabels[$Name].Location = New-Object System.Drawing.Point($X, $Y)
    }
    if ($script:tuningFields.ContainsKey($Name)) {
        $field = $script:tuningFields[$Name]
        if ($field.Parent -is [Gx12LauncherV3.DarkInputFrame]) {
            $labelWidth = if ($script:tuningLabels.ContainsKey($Name)) { $script:tuningLabels[$Name].Width } else { 100 }
            $field.Parent.Location = New-Object System.Drawing.Point(($X + $labelWidth + 12), $Y)
        } elseif ($field -is [Gx12Launcher.StickShapeEditor]) {
            Move-V3ShapeEditor -Name $Name -X $X -Y $Y
        } else {
            $field.Location = New-Object System.Drawing.Point($X, $Y)
        }
    }
}

function Move-V3Check {
    param(
        [string]$Name,
        [int]$X,
        [int]$Y
    )

    if ($script:tuningChecks.ContainsKey($Name)) {
        $script:tuningChecks[$Name].Location = New-Object System.Drawing.Point($X, $Y)
    }
}

function Set-V3TuningLabelText {
    param(
        [string]$Name,
        [string]$Text
    )

    if ($script:tuningLabels.ContainsKey($Name)) {
        $script:tuningLabels[$Name].Text = $Text
    }
}

function Move-V3ShapeEditor {
    param(
        [string]$Name,
        [int]$X,
        [int]$Y,
        [int]$Height = -1
    )

    if ($script:tuningLabels.ContainsKey($Name)) {
        $script:tuningLabels[$Name].Location = New-Object System.Drawing.Point($X, $Y)
    }
    if ($script:shapeExpandButtons.ContainsKey($Name) -and $script:tuningFields.ContainsKey($Name)) {
        $editorWidth = $script:tuningFields[$Name].Width
        $script:shapeExpandButtons[$Name].Location = New-Object System.Drawing.Point(($X + $editorWidth - 88), ($Y - 2))
    }
    if ($script:tuningFields.ContainsKey($Name) -and ($script:tuningFields[$Name] -is [Gx12Launcher.StickShapeEditor])) {
        $editor = $script:tuningFields[$Name]
        $editor.Location = New-Object System.Drawing.Point($X, ($Y + 28))
        if ($Height -gt 0) {
            $editor.Height = $Height
        }
    }
}

function Compress-V3LeftYawAdvancedLayout {
    if ($null -eq $script:leftStickYawAdvancedPanel) {
        return
    }

    if ($script:tuningChecks.ContainsKey('MouseLeftYawDespikeCountEnabled')) {
        $script:tuningChecks.MouseLeftYawDespikeCountEnabled.Text = 'Count'
        $script:tuningChecks.MouseLeftYawDespikeCountEnabled.Width = 88
    }
    Set-V3TuningLabelText -Name 'MouseLeftYawAdaptiveSpeedLow' -Text 'Low'
    Set-V3TuningLabelText -Name 'MouseLeftYawAdaptiveSpeedHigh' -Text 'High'
    Set-V3TuningLabelText -Name 'MouseLeftYawOutputShape' -Text 'Output shape'
    Set-V3TuningLabelText -Name 'MouseLeftYawReturnShape' -Text 'Return shape'

    Move-V3ShapeEditor -Name 'MouseLeftYawOutputShape' -X 268 -Y 314 -Height 58
    Move-V3Check -Name 'MouseLeftYawReturnShapingEnabled' -X 268 -Y 404
    Move-V3ShapeEditor -Name 'MouseLeftYawReturnShape' -X 268 -Y 438 -Height 58

    Move-V3Field -Name 'MouseLeftYawGimbalFrequencyHz' -X 512 -Y 120
    Move-V3Field -Name 'MouseLeftYawGimbalDampingRatio' -X 512 -Y 156
    Move-V3Field -Name 'MouseLeftYawGimbalInputImpulse' -X 512 -Y 192
    Move-V3Field -Name 'MouseLeftYawGimbalStaticFriction' -X 512 -Y 228
    Move-V3Field -Name 'MouseLeftYawGimbalDynamicFriction' -X 512 -Y 264
    Move-V3Field -Name 'MouseLeftYawGimbalEdgeBumper' -X 512 -Y 300
    Move-V3Check -Name 'MouseLeftYawGimbalAntiwindupEnabled' -X 512 -Y 338
    Move-V3Field -Name 'MouseLeftYawGimbalAntiwindupStart' -X 512 -Y 374
    Move-V3Field -Name 'MouseLeftYawGimbalAntiwindupMinGain' -X 512 -Y 410
    Move-V3Field -Name 'MouseLeftYawInputGainMode' -X 512 -Y 446
    Move-V3Field -Name 'MouseLeftYawAdaptiveSlowGain' -X 512 -Y 482
    Move-V3Field -Name 'MouseLeftYawAdaptiveFastGain' -X 628 -Y 482
    Move-V3Field -Name 'MouseLeftYawAdaptiveSpeedLow' -X 512 -Y 518
    Move-V3Field -Name 'MouseLeftYawAdaptiveSpeedHigh' -X 628 -Y 518
    $script:leftStickYawAdvancedPanel.Height = 566
}

function Refresh-V3EditorSectionLayout {
    if ($null -ne $script:rightStickAdvancedPanel) { $script:rightStickAdvancedPanel.Visible = $true }
    if ($null -ne $script:leftStickYawAdvancedPanel) { $script:leftStickYawAdvancedPanel.Visible = $true }
    if ($null -ne $script:droneAimContentPanel) { $script:droneAimContentPanel.Visible = $true }

    if ($null -ne $script:overviewSection) { $script:overviewSection.Height = 294 }
    if ($null -ne $script:rightStickSection) { $script:rightStickSection.Height = 462 }
    if ($null -ne $script:rightStickAdvancedSection) { $script:rightStickAdvancedSection.Height = 662 }
    if ($null -ne $script:leftStickSection) { $script:leftStickSection.Height = 530 }
    if ($null -ne $script:leftStickMouseSection) { $script:leftStickMouseSection.Height = 480 }
    if ($null -ne $script:leftStickYawSection) { $script:leftStickYawSection.Height = 632 }
    if ($null -ne $script:droneAimSection) { $script:droneAimSection.Height = 436 }

    $sectionWidth = $script:editorSectionMinWidth
    if ($null -ne $script:editorScroll -and $script:editorScroll.ClientSize.Width -gt 0) {
        $sectionWidth = [Math]::Max($script:editorSectionMinWidth, ($script:editorScroll.ClientSize.Width - 18))
    }
    foreach ($section in @(
        $script:overviewSection,
        $script:rightStickSection,
        $script:rightStickAdvancedSection,
        $script:leftStickSection,
        $script:leftStickMouseSection,
        $script:leftStickYawSection,
        $script:droneAimSection
    )) {
        if ($null -eq $section) {
            continue
        }
        $section.Location = New-Object System.Drawing.Point(0, 0)
        $section.Width = $sectionWidth
    }
    foreach ($tabName in $script:editorTabNames) {
        $page = $script:editorPages[$tabName]
        if ($page.Controls.Count -gt 0) {
            $section = $page.Controls[0]
            $page.AutoScrollMinSize = New-Object System.Drawing.Size($section.Width, $section.Height)
        }
    }
    Update-V3EditorTabsLayout
}

function Update-RightStickControlState {
    if (-not $script:tuningChecks.ContainsKey('MouseRightStickEnabled')) {
        return
    }

    $mouseEnabled = $script:tuningChecks.MouseRightStickEnabled.Checked
    foreach ($name in @('RollGain', 'PitchGain', 'MaxOutput', 'Deadband', 'OutputCurve', 'InvertRoll', 'InvertPitch', 'SwapAxes', 'ReturnEnabled', 'ConstantReturnEnabled', 'ElasticReturnEnabled')) {
        Set-V3NamedControlEnabled -Name $name -Enabled $mouseEnabled
    }

    $outputCurve = if ($script:tuningFields.ContainsKey('OutputCurve')) { [string]$script:tuningFields.OutputCurve.Text } else { 'expo' }
    Set-V3NamedControlEnabled -Name 'Expo' -Enabled ($mouseEnabled -and $outputCurve -eq 'expo')
    foreach ($name in @('ActualCenter', 'ActualMax', 'ActualExpo')) {
        Set-V3NamedControlEnabled -Name $name -Enabled ($mouseEnabled -and $outputCurve -eq 'actual')
    }

    $idleEnabled = $mouseEnabled -and $script:tuningChecks.ContainsKey('ReturnEnabled') -and $script:tuningChecks.ReturnEnabled.Checked
    foreach ($name in @('ReturnRate', 'ReturnIdle')) {
        Set-V3NamedControlEnabled -Name $name -Enabled $idleEnabled
    }

    $constantEnabled = $mouseEnabled -and $script:tuningChecks.ContainsKey('ConstantReturnEnabled') -and $script:tuningChecks.ConstantReturnEnabled.Checked
    Set-V3NamedControlEnabled -Name 'ConstantReturnRate' -Enabled $constantEnabled

    $elasticEnabled = $mouseEnabled -and $script:tuningChecks.ContainsKey('ElasticReturnEnabled') -and $script:tuningChecks.ElasticReturnEnabled.Checked
    foreach ($name in @('ElasticReturnMode', 'ElasticReturnCoefficient', 'ElasticReturnCurve')) {
        Set-V3NamedControlEnabled -Name $name -Enabled $elasticEnabled
    }

    foreach ($name in @('InputFilter', 'PositionModel', 'InputGainMode', 'GateShape', 'DiagonalScale', 'DespikeEnabled', 'DespikeCountEnabled', 'OutputShapingEnabled', 'ReturnShapingEnabled')) {
        Set-V3NamedControlEnabled -Name $name -Enabled $mouseEnabled
    }

    $inputFilter = if ($script:tuningFields.ContainsKey('InputFilter')) { [string]$script:tuningFields.InputFilter.Text } else { 'off' }
    Set-V3NamedControlEnabled -Name 'Smoothing' -Enabled ($mouseEnabled -and $inputFilter -eq 'smoothing')
    foreach ($name in @('OneEuroMinCutoffHz', 'OneEuroBeta', 'OneEuroDcutoffHz')) {
        Set-V3NamedControlEnabled -Name $name -Enabled ($mouseEnabled -and $inputFilter -eq 'one_euro')
    }

    $despikeEnabled = $mouseEnabled -and $script:tuningChecks.ContainsKey('DespikeEnabled') -and $script:tuningChecks.DespikeEnabled.Checked
    foreach ($name in @('DespikeWindow', 'DespikeThresholdSigma')) {
        Set-V3NamedControlEnabled -Name $name -Enabled $despikeEnabled
    }

    $dynamicEnabled = $mouseEnabled -and $script:tuningFields.ContainsKey('PositionModel') -and ([string]$script:tuningFields.PositionModel.Text -eq 'dynamic_gimbal')
    foreach ($name in @('GimbalFrequencyHz', 'GimbalDampingRatio', 'GimbalInputImpulse', 'GimbalStaticFriction', 'GimbalDynamicFriction', 'GimbalEdgeBumper', 'GimbalAntiwindupEnabled')) {
        Set-V3NamedControlEnabled -Name $name -Enabled $dynamicEnabled
    }
    $antiwindupEnabled = $dynamicEnabled -and $script:tuningChecks.ContainsKey('GimbalAntiwindupEnabled') -and $script:tuningChecks.GimbalAntiwindupEnabled.Checked
    foreach ($name in @('GimbalAntiwindupStart', 'GimbalAntiwindupMinGain')) {
        Set-V3NamedControlEnabled -Name $name -Enabled $antiwindupEnabled
    }

    $adaptiveEnabled = $mouseEnabled -and $script:tuningFields.ContainsKey('InputGainMode') -and ([string]$script:tuningFields.InputGainMode.Text -eq 'adaptive')
    foreach ($name in @('AdaptiveSlowGain', 'AdaptiveFastGain', 'AdaptiveSpeedLow', 'AdaptiveSpeedHigh', 'AdaptiveCurve', 'AdaptiveTrackerMs')) {
        Set-V3NamedControlEnabled -Name $name -Enabled $adaptiveEnabled
    }

    $outputShapeActive = $mouseEnabled -and (
        ($script:tuningChecks.ContainsKey('OutputShapingEnabled') -and $script:tuningChecks.OutputShapingEnabled.Checked) -or
        ($outputCurve -eq 'nodes')
    )
    Set-V3ShapeEditorState -Name 'OutputShape' -Available $mouseEnabled -Active $outputShapeActive
    $returnShapeActive = $elasticEnabled -and $script:tuningChecks.ContainsKey('ReturnShapingEnabled') -and $script:tuningChecks.ReturnShapingEnabled.Checked
    Set-V3ShapeEditorState -Name 'ReturnShape' -Available $mouseEnabled -Active $returnShapeActive
}

function Update-DroneAimControlState {
    if (-not $script:tuningChecks.ContainsKey('WarThunderMode')) {
        return
    }

    $aimEnabled = $script:tuningChecks.WarThunderMode.Checked
    foreach ($name in $script:droneAimFieldNames) {
        Set-V3NamedControlEnabled -Name $name -Enabled $aimEnabled
    }
    foreach ($name in $script:droneAimCheckNames) {
        Set-V3NamedControlEnabled -Name $name -Enabled $aimEnabled
    }
    if ($null -ne $script:droneAimSection) {
        $script:droneAimSection.BorderColor = if ($aimEnabled) { $script:dark.AccentSoft } else { $script:dark.Border }
        $script:droneAimSection.Invalidate()
    }
}

function Update-LeftStickControlState {
    if (-not $script:tuningChecks.ContainsKey('KeyboardEnabled') -or
        -not $script:tuningChecks.ContainsKey('MouseLeftEnabled')) {
        return
    }

    $keyboardEnabled = $script:tuningChecks.KeyboardEnabled.Checked
    $mouseEnabled = $script:tuningChecks.MouseLeftEnabled.Checked
    if ($null -ne $script:leftStickOffRadio -and -not $keyboardEnabled -and -not $mouseEnabled) {
        $script:leftStickOffRadio.Checked = $true
    }

    $source = if ($script:tuningFields.ContainsKey('KeyboardInputSource')) {
        [string]$script:tuningFields.KeyboardInputSource.Text
    } else {
        'gameinput'
    }
    $analogEnabled = $keyboardEnabled -and ($source -in @('wooting_analog', 'auto'))

    foreach ($name in $script:leftStickKeyboardFieldNames) {
        Set-V3NamedControlEnabled -Name $name -Enabled $keyboardEnabled
    }
    foreach ($name in $script:leftStickKeyboardCheckNames) {
        if ($name -ne 'KeyboardEnabled') {
            Set-V3NamedControlEnabled -Name $name -Enabled $keyboardEnabled
        }
    }
    foreach ($name in @('KeyboardRequireAnalog', 'KeyboardAnalogKeycodeMode', 'KeyboardAnalogDeadzone', 'KeyboardAnalogCurve', 'KeyboardAnalogMin', 'KeyboardAnalogMax')) {
        Set-V3NamedControlEnabled -Name $name -Enabled $analogEnabled
    }
    $throttleReturnEnabled = $keyboardEnabled -and $script:tuningChecks.ContainsKey('ThrottleReturnEnabled') -and $script:tuningChecks.ThrottleReturnEnabled.Checked
    Set-V3NamedControlEnabled -Name 'ThrottleReturnRate' -Enabled $throttleReturnEnabled

    foreach ($name in $script:leftStickMouseFieldNames) {
        Set-V3NamedControlEnabled -Name $name -Enabled $mouseEnabled
    }
    foreach ($name in $script:leftStickMouseCheckNames) {
        if ($name -ne 'MouseLeftEnabled') {
            Set-V3NamedControlEnabled -Name $name -Enabled $mouseEnabled
        }
    }
    $mouseThrottleReturnEnabled = $mouseEnabled -and $script:tuningChecks.ContainsKey('MouseLeftThrottleReturnEnabled') -and $script:tuningChecks.MouseLeftThrottleReturnEnabled.Checked
    Set-V3NamedControlEnabled -Name 'MouseLeftThrottleReturnRate' -Enabled $mouseThrottleReturnEnabled

    $mouseYawReturnEnabled = $mouseEnabled -and $script:tuningChecks.ContainsKey('MouseLeftYawReturnEnabled') -and $script:tuningChecks.MouseLeftYawReturnEnabled.Checked
    foreach ($name in @('MouseLeftYawReturnRate', 'MouseLeftYawReturnIdle')) {
        Set-V3NamedControlEnabled -Name $name -Enabled $mouseYawReturnEnabled
    }
    $mouseYawConstantReturnEnabled = $mouseEnabled -and $script:tuningChecks.ContainsKey('MouseLeftYawConstantReturnEnabled') -and $script:tuningChecks.MouseLeftYawConstantReturnEnabled.Checked
    Set-V3NamedControlEnabled -Name 'MouseLeftYawConstantReturnRate' -Enabled $mouseYawConstantReturnEnabled

    $mouseYawElasticReturnEnabled = $mouseEnabled -and $script:tuningChecks.ContainsKey('MouseLeftYawElasticReturnEnabled') -and $script:tuningChecks.MouseLeftYawElasticReturnEnabled.Checked
    foreach ($name in @('MouseLeftYawElasticReturnMode', 'MouseLeftYawElasticReturnCoefficient', 'MouseLeftYawElasticReturnCurve')) {
        Set-V3NamedControlEnabled -Name $name -Enabled $mouseYawElasticReturnEnabled
    }

    foreach ($name in $script:leftStickMouseAdvancedCheckNames) {
        Set-V3NamedControlEnabled -Name $name -Enabled $mouseEnabled
    }
    $yawShapingEnabled = $mouseEnabled -and $script:tuningChecks.ContainsKey('MouseLeftYawShapingEnabled') -and $script:tuningChecks.MouseLeftYawShapingEnabled.Checked
    foreach ($name in @('MouseLeftYawInputFilter', 'MouseLeftYawOutputCurve', 'MouseLeftYawPositionModel', 'MouseLeftYawInputGainMode', 'MouseLeftYawGateShape', 'MouseLeftYawDiagonalScale')) {
        Set-V3NamedControlEnabled -Name $name -Enabled $yawShapingEnabled
    }

    $yawInputFilter = if ($script:tuningFields.ContainsKey('MouseLeftYawInputFilter')) { [string]$script:tuningFields.MouseLeftYawInputFilter.Text } else { 'off' }
    foreach ($name in @('MouseLeftYawOneEuroMinCutoffHz', 'MouseLeftYawOneEuroBeta', 'MouseLeftYawOneEuroDcutoffHz')) {
        Set-V3NamedControlEnabled -Name $name -Enabled ($yawShapingEnabled -and $yawInputFilter -eq 'one_euro')
    }
    $yawDespikeEnabled = $yawShapingEnabled -and $script:tuningChecks.ContainsKey('MouseLeftYawDespikeEnabled') -and $script:tuningChecks.MouseLeftYawDespikeEnabled.Checked
    foreach ($name in @('MouseLeftYawDespikeWindow', 'MouseLeftYawDespikeThresholdSigma')) {
        Set-V3NamedControlEnabled -Name $name -Enabled $yawDespikeEnabled
    }

    $yawOutputCurve = if ($script:tuningFields.ContainsKey('MouseLeftYawOutputCurve')) { [string]$script:tuningFields.MouseLeftYawOutputCurve.Text } else { 'expo' }
    Set-V3NamedControlEnabled -Name 'MouseLeftYawExpo' -Enabled ($yawShapingEnabled -and $yawOutputCurve -eq 'expo')
    foreach ($name in @('MouseLeftYawActualCenter', 'MouseLeftYawActualMax', 'MouseLeftYawActualExpo')) {
        Set-V3NamedControlEnabled -Name $name -Enabled ($yawShapingEnabled -and $yawOutputCurve -eq 'actual')
    }

    $yawDynamicEnabled = $yawShapingEnabled -and $script:tuningFields.ContainsKey('MouseLeftYawPositionModel') -and ([string]$script:tuningFields.MouseLeftYawPositionModel.Text -eq 'dynamic_gimbal')
    foreach ($name in @('MouseLeftYawGimbalFrequencyHz', 'MouseLeftYawGimbalDampingRatio', 'MouseLeftYawGimbalInputImpulse', 'MouseLeftYawGimbalStaticFriction', 'MouseLeftYawGimbalDynamicFriction', 'MouseLeftYawGimbalEdgeBumper', 'MouseLeftYawGimbalAntiwindupEnabled')) {
        Set-V3NamedControlEnabled -Name $name -Enabled $yawDynamicEnabled
    }
    $yawAntiwindupEnabled = $yawDynamicEnabled -and $script:tuningChecks.ContainsKey('MouseLeftYawGimbalAntiwindupEnabled') -and $script:tuningChecks.MouseLeftYawGimbalAntiwindupEnabled.Checked
    foreach ($name in @('MouseLeftYawGimbalAntiwindupStart', 'MouseLeftYawGimbalAntiwindupMinGain')) {
        Set-V3NamedControlEnabled -Name $name -Enabled $yawAntiwindupEnabled
    }

    $yawAdaptiveEnabled = $yawShapingEnabled -and $script:tuningFields.ContainsKey('MouseLeftYawInputGainMode') -and ([string]$script:tuningFields.MouseLeftYawInputGainMode.Text -eq 'adaptive')
    foreach ($name in @('MouseLeftYawAdaptiveSlowGain', 'MouseLeftYawAdaptiveFastGain', 'MouseLeftYawAdaptiveSpeedLow', 'MouseLeftYawAdaptiveSpeedHigh', 'MouseLeftYawAdaptiveCurve', 'MouseLeftYawAdaptiveTrackerMs')) {
        Set-V3NamedControlEnabled -Name $name -Enabled $yawAdaptiveEnabled
    }

    $yawOutputShapeActive = $yawShapingEnabled -and (
        ($script:tuningChecks.ContainsKey('MouseLeftYawOutputShapingEnabled') -and $script:tuningChecks.MouseLeftYawOutputShapingEnabled.Checked) -or
        ($yawOutputCurve -eq 'nodes')
    )
    Set-V3ShapeEditorState -Name 'MouseLeftYawOutputShape' -Available $yawShapingEnabled -Active $yawOutputShapeActive
    $yawReturnShapeActive = $yawShapingEnabled -and $mouseYawElasticReturnEnabled -and $script:tuningChecks.ContainsKey('MouseLeftYawReturnShapingEnabled') -and $script:tuningChecks.MouseLeftYawReturnShapingEnabled.Checked
    Set-V3ShapeEditorState -Name 'MouseLeftYawReturnShape' -Available $yawShapingEnabled -Active $yawReturnShapeActive

    if ($null -ne $script:leftStickKeyboardBox) {
        $script:leftStickKeyboardBox.BorderColor = if ($keyboardEnabled) { $script:dark.AccentSoft } else { $script:dark.Border }
        $script:leftStickKeyboardBox.Invalidate()
    }
    if ($null -ne $script:leftStickMouseBox) {
        $script:leftStickMouseBox.BorderColor = if ($mouseEnabled) { $script:dark.AccentSoft } else { $script:dark.Border }
        $script:leftStickMouseBox.Invalidate()
    }
}

function New-V3TuningTextBox {
    param(
        [string]$Name,
        [string]$Label,
        [int]$X,
        [int]$Y,
        [System.Windows.Forms.Control]$Parent,
        [switch]$KeyBinding,
        [string]$Tip = '',
        [int]$LabelWidth = 150,
        [int]$Width = 190
    )

    $labelControl = New-DarkLabel -Text $Label -X $X -Y $Y -W $LabelWidth -H 30 -Size 9.6 -Color $script:dark.TextMuted
    $Parent.Controls.Add($labelControl)
    $script:tuningLabels[$Name] = $labelControl

    $input = New-DarkInput -X ($X + $LabelWidth + 12) -Y $Y -W $Width -H 32
    $textControl = $input.TextBox
    $textControl.Tag = $Name
    $textControl.Add_TextChanged({
        param($sender, $eventArgs)
        Queue-TuningSave -ChangedName ([string]$sender.Tag)
    })

    if ($KeyBinding) {
        $textControl.ReadOnly = $true
        $textControl.Cursor = [System.Windows.Forms.Cursors]::Hand
        $textControl.Add_Click({
            $this.SelectAll()
            if ($null -ne $script:statusText) {
                $script:statusText.Text = 'Press a key to set this binding.'
            }
        })
        $textControl.Add_Enter({
            $this.SelectAll()
            if ($null -ne $script:statusText) {
                $script:statusText.Text = 'Press a key to set this binding.'
            }
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
            Queue-TuningSave -ChangedName ([string]$sender.Tag)
        })
    }

    $Parent.Controls.Add($input.Frame)
    if (-not [string]::IsNullOrWhiteSpace($Tip)) {
        $script:toolTip.SetToolTip($labelControl, $Tip)
        $script:toolTip.SetToolTip($textControl, $Tip)
    }
    $script:tuningFields[$Name] = $textControl
    return $textControl
}

function New-V3TuningComboBox {
    param(
        [string]$Name,
        [string]$Label,
        [int]$X,
        [int]$Y,
        [string[]]$Items,
        [System.Windows.Forms.Control]$Parent,
        [string]$Tip = '',
        [int]$LabelWidth = 150,
        [int]$Width = 190
    )

    $labelControl = New-DarkLabel -Text $Label -X $X -Y $Y -W $LabelWidth -H 30 -Size 9.6 -Color $script:dark.TextMuted
    $Parent.Controls.Add($labelControl)
    $script:tuningLabels[$Name] = $labelControl

    $combo = New-Object Gx12LauncherV3.DarkComboBox
    $combo.BackColor = $script:dark.BgElev
    $combo.ForeColor = $script:dark.Text
    $combo.Font = New-DarkFont -Size 9.8
    $combo.Location = New-Object System.Drawing.Point(($X + $LabelWidth + 12), $Y)
    $combo.Size = New-Object System.Drawing.Size($Width, 32)
    $combo.Tag = $Name
    [void]$combo.Items.AddRange([object[]]$Items)
    $combo.Add_SelectedIndexChanged({
        param($sender, $eventArgs)
        Queue-TuningSave -ChangedName ([string]$sender.Tag)
    })
    $Parent.Controls.Add($combo)

    if (-not [string]::IsNullOrWhiteSpace($Tip)) {
        $script:toolTip.SetToolTip($labelControl, $Tip)
        $script:toolTip.SetToolTip($combo, $Tip)
    }
    $script:tuningFields[$Name] = $combo
    return $combo
}

function New-V3TuningCheckBox {
    param(
        [AllowEmptyString()][string]$Name = '',
        [string]$Label,
        [int]$X,
        [int]$Y,
        [System.Windows.Forms.Control]$Parent,
        [int]$Width = 230,
        [string]$Tip = ''
    )

    $check = New-Object Gx12LauncherV3.DarkCheckBox
    $check.Text = $Label
    $check.Location = New-Object System.Drawing.Point($X, $Y)
    $check.Size = New-Object System.Drawing.Size($Width, 28)
    $check.Font = New-DarkFont -Size 9.6
    $check.ForeColor = $script:dark.Text
    $Parent.Controls.Add($check)

    if (-not [string]::IsNullOrWhiteSpace($Name)) {
        $check.Tag = $Name
        $check.Add_CheckedChanged({
            param($sender, $eventArgs)
            Queue-TuningSave -ChangedName ([string]$sender.Tag)
        })
        $script:tuningChecks[$Name] = $check
    }
    if (-not [string]::IsNullOrWhiteSpace($Tip)) {
        $script:toolTip.SetToolTip($check, $Tip)
    }
    return $check
}

function New-V3SourceRadioButton {
    param(
        [AllowEmptyString()][string]$Name = '',
        [string]$Label,
        [int]$X,
        [int]$Y,
        [System.Windows.Forms.Control]$Parent,
        [int]$Width = 180,
        [string]$Tip = ''
    )

    $radio = New-Object Gx12LauncherV3.DarkRadioButton
    $radio.Text = $Label
    $radio.Location = New-Object System.Drawing.Point($X, $Y)
    $radio.Size = New-Object System.Drawing.Size($Width, 28)
    $radio.Font = New-DarkFont -Size 9.8
    $radio.ForeColor = $script:dark.Text
    $radio.Tag = $Name
    $radio.Add_CheckedChanged({
        param($sender, $eventArgs)
        Queue-TuningSave -ChangedName ([string]$sender.Tag)
    })
    $Parent.Controls.Add($radio)

    if (-not [string]::IsNullOrWhiteSpace($Name)) {
        $script:tuningChecks[$Name] = $radio
    }
    if (-not [string]::IsNullOrWhiteSpace($Tip)) {
        $script:toolTip.SetToolTip($radio, $Tip)
    }
    return $radio
}

function New-V3OverviewSection {
    param(
        [System.Windows.Forms.Control]$Parent,
        [int]$X,
        [int]$Y,
        [int]$W
    )

    $section = New-DarkPanel -X $X -Y $Y -W $W -H 294 -Radius 8 -BackColor $script:dark.BgElev -BorderColor $script:dark.Border
    $section.Anchor = 'Top,Left,Right'
    $Parent.Controls.Add($section)

    $title = New-DarkLabel -Text 'Overview' -X 20 -Y 14 -W 240 -H 26 -Size 11.0 -Style ([System.Drawing.FontStyle]::Bold)
    $section.Controls.Add($title)

    New-V3TuningTextBox -Name 'ProfileName' -Label 'Profile name' -X 22 -Y 58 -Parent $section -Width 250 | Out-Null
    New-V3TuningTextBox -Name 'FrameRate' -Label 'Frame rate Hz' -X 22 -Y 104 -Parent $section -Width 112 | Out-Null
    New-V3TuningTextBox -Name 'StopKey' -Label 'Stop key' -X 22 -Y 156 -Parent $section -Width 112 -KeyBinding -Tip 'Key that stops the active run and triggers cleanup.' | Out-Null
    New-V3TuningTextBox -Name 'FreezeKey' -Label 'Mouse freeze key' -X 22 -Y 204 -Parent $section -Width 112 -KeyBinding -Tip 'Key that toggles cursor freeze during an active run.' | Out-Null

    $script:defaultCheck = New-V3TuningCheckBox -Name '' -Label 'Default profile' -X 448 -Y 62 -Parent $section -Width 220
    $script:defaultCheck.Add_CheckedChanged({
        if ($script:loadingProfile) {
            return
        }
        if ($null -eq $script:profilesList -or $null -eq $script:profilesList.SelectedItem) {
            return
        }
        if ($script:defaultCheck.Checked) {
            Flush-PendingTuningSave
            $selectedFileName = $script:profilesList.SelectedItem.FileName
            Set-DefaultProfileFileName -FileName $selectedFileName
            Set-RunStatus -State 'Idle' -Text "Default profile is now $selectedFileName."
            Refresh-Profiles
            Apply-ProfileFilter -TargetFileName $selectedFileName
        } else {
            $script:loadingProfile = $true
            try {
                $script:defaultCheck.Checked = ($script:profilesList.SelectedItem.FileName -eq (Get-DefaultProfileFileName))
            } finally {
                $script:loadingProfile = $false
            }
        }
    })

    New-V3TuningCheckBox -Name 'WarThunderMode' -Label 'Reticle aim mode' -X 448 -Y 108 -Parent $section -Width 260 -Tip 'Unchecked profiles use the normal right-stick mapper.' | Out-Null
    $script:tuningChecks.WarThunderMode.Add_CheckedChanged({
        if ($script:tuningChecks.WarThunderMode.Checked -and -not $script:loadingProfile) {
            $script:droneAimExpanded = $true
            Select-V3EditorTab -Name 'DroneAim'
            Refresh-V3EditorSectionLayout
        }
        Update-DroneAimControlState
    })

    $modeHint = New-DarkLabel -Text 'Right-stick mapper when unchecked' -X 472 -Y 136 -W 280 -H 20 -Size 8.6 -Color $script:dark.TextMuted
    $section.Controls.Add($modeHint)

    return $section
}

function New-V3RightStickSection {
    param(
        [System.Windows.Forms.Control]$Parent,
        [int]$X,
        [int]$Y,
        [int]$W
    )

    $section = New-DarkPanel -X $X -Y $Y -W $W -H 462 -Radius 8 -BackColor $script:dark.BgElev -BorderColor $script:dark.Border
    $section.Anchor = 'Top,Left,Right'
    $Parent.Controls.Add($section)

    $title = New-DarkLabel -Text 'Right Stick' -X 20 -Y 14 -W 240 -H 26 -Size 11.0 -Style ([System.Drawing.FontStyle]::Bold)
    $section.Controls.Add($title)

    New-V3TuningCheckBox -Name 'MouseRightStickEnabled' -Label 'Mouse stick' -X 468 -Y 20 -Parent $section -Width 220 -Tip 'Enable mouse as right stick input.' | Out-Null
    New-V3TuningCheckBox -Name 'InvertRoll' -Label 'Invert roll' -X 468 -Y 58 -Parent $section -Width 150 -Tip 'Reverse mouse X direction before it becomes right-stick roll.' | Out-Null
    New-V3TuningCheckBox -Name 'InvertPitch' -Label 'Invert pitch' -X 620 -Y 58 -Parent $section -Width 150 -Tip 'Reverse mouse Y direction before it becomes right-stick pitch.' | Out-Null
    New-V3TuningCheckBox -Name 'SwapAxes' -Label 'Swap axes' -X 468 -Y 94 -Parent $section -Width 150 -Tip 'Route mouse X to pitch and mouse Y to roll before inversion is applied.' | Out-Null

    New-V3TuningTextBox -Name 'RollGain' -Label 'Mouse roll sens' -X 22 -Y 58 -Parent $section -Width 112 -Tip 'Mouse X sensitivity for right-stick roll. Higher values move the virtual stick farther for the same mouse movement.' | Out-Null
    New-V3TuningTextBox -Name 'PitchGain' -Label 'Mouse pitch sens' -X 22 -Y 104 -Parent $section -Width 112 -Tip 'Mouse Y sensitivity for right-stick pitch. Higher values move the virtual stick farther for the same mouse movement.' | Out-Null
    New-V3TuningTextBox -Name 'MaxOutput' -Label 'Max output' -X 22 -Y 150 -Parent $section -Width 112 -Tip 'Maximum right-stick trainer output. 512 is full stick; lower values cap roll and pitch authority.' | Out-Null
    New-V3TuningTextBox -Name 'Deadband' -Label 'Deadband' -X 22 -Y 196 -Parent $section -Width 112 -Tip 'Small right-stick outputs below this trainer-unit value are suppressed to avoid drift or jitter near center.' | Out-Null
    New-V3TuningComboBox -Name 'OutputCurve' -Label 'Output curve' -X 22 -Y 242 -Parent $section -Width 150 -Items @('expo', 'nodes', 'actual') -Tip 'Selects the right-stick output curve.' | Out-Null
    New-V3TuningTextBox -Name 'Expo' -Label 'Expo' -X 22 -Y 288 -Parent $section -Width 112 -Tip 'Response curve for roll/pitch. 0 is linear; higher values soften small movements while keeping edge authority.' | Out-Null

    $returnTitle = New-DarkLabel -Text 'Return' -X 20 -Y 344 -W 240 -H 24 -Size 10.4 -Style ([System.Drawing.FontStyle]::Bold)
    $section.Controls.Add($returnTitle)

    $idleBox = New-DarkPanel -X 22 -Y 374 -W 258 -H 76 -Radius 7 -BackColor $script:dark.BgSurface -BorderColor $script:dark.Border
    $section.Controls.Add($idleBox)
    New-V3TuningCheckBox -Name 'ReturnEnabled' -Label 'Idle' -X 12 -Y 8 -Parent $idleBox -Width 88 -Tip 'When enabled, the virtual right stick recenters after mouse movement stops.' | Out-Null
    New-V3TuningTextBox -Name 'ReturnRate' -Label 'Speed' -X 12 -Y 40 -Parent $idleBox -LabelWidth 58 -Width 74 -Tip 'Right-stick recenter speed when idle stick return is enabled.' | Out-Null
    New-V3TuningTextBox -Name 'ReturnIdle' -Label 'Delay' -X 126 -Y 40 -Parent $idleBox -LabelWidth 50 -Width 58 -Tip 'Delay before idle right-stick return starts after mouse movement stops.' | Out-Null

    $constantBox = New-DarkPanel -X 296 -Y 374 -W 206 -H 76 -Radius 7 -BackColor $script:dark.BgSurface -BorderColor $script:dark.Border
    $section.Controls.Add($constantBox)
    New-V3TuningCheckBox -Name 'ConstantReturnEnabled' -Label 'Constant' -X 12 -Y 8 -Parent $constantBox -Width 126 -Tip 'When enabled, the virtual right stick is urged toward center on every mapper tick.' | Out-Null
    New-V3TuningTextBox -Name 'ConstantReturnRate' -Label 'Speed' -X 12 -Y 40 -Parent $constantBox -LabelWidth 58 -Width 82 -Tip 'Right-stick recenter speed applied continuously, including while mouse input is moving the virtual stick.' | Out-Null

    $elasticBox = New-DarkPanel -X 494 -Y 148 -W 270 -H 296 -Radius 7 -BackColor $script:dark.BgSurface -BorderColor $script:dark.Border
    $section.Controls.Add($elasticBox)
    New-V3TuningCheckBox -Name 'ElasticReturnEnabled' -Label 'Elastic return' -X 12 -Y 10 -Parent $elasticBox -Width 180 -Tip 'When enabled, return strength scales with current stick deflection.' | Out-Null

    $script:elasticModeExplain = @{
        'progressive' = "Progressive - original behavior.`r`nReturn force scales with deflection raised to the curve exponent.`r`nCurve = 0 acts purely linear; curve > 0 makes the spring soft near center and stronger near the edges."
        'linear'      = "Linear - pure proportional spring.`r`nReturn force is directly proportional to deflection; the curve field is ignored."
        'smoothstep'  = "Smoothstep - eased S-curve return.`r`nUses 3x^2 - 2x^3 shaping so the pull is gentle at both ends and firmest in the middle."
        'expo'        = "Expo - exponential ramp.`r`nReturn force grows exponentially with deflection. Light near center, stronger toward full stick."
    }
    New-V3TuningComboBox -Name 'ElasticReturnMode' -Label 'Mode' -X 12 -Y 54 -Parent $elasticBox -LabelWidth 80 -Width 150 -Items @('progressive', 'linear', 'smoothstep', 'expo') -Tip $script:elasticModeExplain['progressive'] | Out-Null
    New-V3TuningTextBox -Name 'ElasticReturnCoefficient' -Label 'Coeff' -X 12 -Y 104 -Parent $elasticBox -LabelWidth 80 -Width 82 -Tip 'Proportional center spring in 1/s. Higher pulls harder far from center but stays gentle near center.' | Out-Null
    New-V3TuningTextBox -Name 'ElasticReturnCurve' -Label 'Curve' -X 12 -Y 154 -Parent $elasticBox -LabelWidth 80 -Width 82 -Tip '0 keeps the current linear spring. Higher values make return lighter near center while preserving full-stick pull.' | Out-Null

    $advancedButton = New-DarkButton -Text 'Advanced' -Icon 0xE70D -Width 112 -Height 30 -Kind 'Ghost'
    $advancedButton.Location = New-Object System.Drawing.Point(650, 18)
    $advancedButton.Add_Click({
        $script:rightStickAdvancedExpanded = -not $script:rightStickAdvancedExpanded
        Refresh-V3EditorSectionLayout
    })
    $section.Controls.Add($advancedButton)

    $advancedPanel = New-DarkPanel -X 22 -Y 462 -W 740 -H 592 -Radius 7 -BackColor $script:dark.BgSurface -BorderColor $script:dark.Border
    $advancedPanel.Anchor = 'Top,Left,Right'
    $section.Controls.Add($advancedPanel)
    $script:rightStickAdvancedPanel = $advancedPanel

    $filterTitle = New-DarkLabel -Text 'Input' -X 16 -Y 14 -W 160 -H 22 -Size 10.2 -Style ([System.Drawing.FontStyle]::Bold)
    $advancedPanel.Controls.Add($filterTitle)
    New-V3TuningComboBox -Name 'InputFilter' -Label 'Input filter' -X 16 -Y 48 -Parent $advancedPanel -LabelWidth 104 -Width 118 -Items @('off', 'smoothing', 'one_euro') -Tip 'Pre-integrator input filtering. Off is immediate; smoothing preserves the legacy low-pass; one_euro reduces jitter with less fast-motion lag.' | Out-Null
    New-V3TuningTextBox -Name 'Smoothing' -Label 'Smoothing' -X 16 -Y 90 -Parent $advancedPanel -LabelWidth 104 -Width 70 -Tip 'Legacy output smoothing for right-stick roll/pitch. Use input_filter = smoothing to enable it.' | Out-Null
    New-V3TuningTextBox -Name 'OneEuroMinCutoffHz' -Label '1-euro min Hz' -X 16 -Y 132 -Parent $advancedPanel -LabelWidth 104 -Width 70 -Tip 'Minimum 1-euro cutoff at low mouse speed.' | Out-Null
    New-V3TuningTextBox -Name 'OneEuroBeta' -Label '1-euro beta' -X 16 -Y 174 -Parent $advancedPanel -LabelWidth 104 -Width 70 -Tip 'How strongly 1-euro cutoff rises with speed.' | Out-Null
    New-V3TuningTextBox -Name 'OneEuroDcutoffHz' -Label '1-euro dcut' -X 16 -Y 216 -Parent $advancedPanel -LabelWidth 104 -Width 70 -Tip 'Derivative low-pass cutoff for the 1-euro speed estimate.' | Out-Null
    New-V3TuningCheckBox -Name 'DespikeEnabled' -Label 'Mouse despike' -X 16 -Y 258 -Parent $advancedPanel -Width 140 -Tip 'Hampel filter that replaces isolated mouse-delta spikes with the local median before shaping.' | Out-Null
    New-V3TuningCheckBox -Name 'DespikeCountEnabled' -Label 'Count despikes' -X 16 -Y 286 -Parent $advancedPanel -Width 160 -Tip 'Report Hampel discard counts while running.' | Out-Null
    New-V3TuningTextBox -Name 'DespikeWindow' -Label 'Window' -X 16 -Y 320 -Parent $advancedPanel -LabelWidth 104 -Width 70 -Tip 'Odd sample window for mouse despiking. 5 is a conservative default.' | Out-Null
    New-V3TuningTextBox -Name 'DespikeThresholdSigma' -Label 'Sigma' -X 16 -Y 362 -Parent $advancedPanel -LabelWidth 104 -Width 70 -Tip 'How many MAD-derived sigmas a mouse sample must exceed before it is replaced.' | Out-Null

    $shapeTitle = New-DarkLabel -Text 'Output / Return' -X 268 -Y 14 -W 180 -H 22 -Size 10.2 -Style ([System.Drawing.FontStyle]::Bold)
    $advancedPanel.Controls.Add($shapeTitle)
    New-V3TuningTextBox -Name 'ActualCenter' -Label 'Actual center' -X 268 -Y 48 -Parent $advancedPanel -LabelWidth 102 -Width 66 -Tip 'Actual Rates center sensitivity as a fraction of max output.' | Out-Null
    New-V3TuningTextBox -Name 'ActualMax' -Label 'Actual max' -X 268 -Y 90 -Parent $advancedPanel -LabelWidth 102 -Width 66 -Tip 'Actual Rates maximum output fraction at full stick.' | Out-Null
    New-V3TuningTextBox -Name 'ActualExpo' -Label 'Actual expo' -X 268 -Y 132 -Parent $advancedPanel -LabelWidth 102 -Width 66 -Tip 'Actual Rates bend control.' | Out-Null
    New-V3TuningCheckBox -Name 'OutputShapingEnabled' -Label 'Output shaping' -X 268 -Y 176 -Parent $advancedPanel -Width 150 -Tip 'Enable free-form right-stick output shaping nodes.' | Out-Null
    New-V3StickShapeEditor -Name 'OutputShape' -Label 'Output shape' -X 268 -Y 210 -Parent $advancedPanel -Width 206 -Height 96 -CurveColor ([System.Drawing.Color]::FromArgb(79, 141, 253)) -Title 'Stick Output Shaping' -Tip 'Click empty area to add a node, drag to move, scroll to widen, right-click to remove. Empty list = linear.' | Out-Null
    New-V3TuningCheckBox -Name 'ReturnShapingEnabled' -Label 'Return shaping' -X 268 -Y 342 -Parent $advancedPanel -Width 150 -Tip 'Enable free-form elastic return-rate shaping nodes.' | Out-Null
    New-V3StickShapeEditor -Name 'ReturnShape' -Label 'Return shape' -X 268 -Y 376 -Parent $advancedPanel -Width 206 -Height 96 -CurveColor ([System.Drawing.Color]::FromArgb(240, 184, 91)) -Title 'Stick Return Shaping' -Tip 'Click empty area to add a node, drag to move, scroll to widen, right-click to remove. Empty list = linear.' | Out-Null
    $previewButton = New-DarkButton -Text 'Preview response' -Icon 0xE9D9 -Width 180 -Height 34 -Kind 'Subtle'
    $previewButton.Location = New-Object System.Drawing.Point(268, 512)
    $previewButton.Add_Click({ Show-ElasticReturnPreview })
    $advancedPanel.Controls.Add($previewButton)

    $modelTitle = New-DarkLabel -Text 'Model / Gain' -X 512 -Y 14 -W 180 -H 22 -Size 10.2 -Style ([System.Drawing.FontStyle]::Bold)
    $advancedPanel.Controls.Add($modelTitle)
    New-V3TuningComboBox -Name 'PositionModel' -Label 'Position' -X 512 -Y 48 -Parent $advancedPanel -LabelWidth 92 -Width 116 -Items @('integrator', 'dynamic_gimbal') -Tip 'Integrator keeps the current virtual-position mapper. Dynamic gimbal treats mouse input as force on a damped virtual stick.' | Out-Null
    New-V3TuningTextBox -Name 'GimbalFrequencyHz' -Label 'Gimbal Hz' -X 512 -Y 90 -Parent $advancedPanel -LabelWidth 92 -Width 58 -Tip 'Dynamic gimbal spring frequency.' | Out-Null
    New-V3TuningTextBox -Name 'GimbalDampingRatio' -Label 'Damping' -X 512 -Y 132 -Parent $advancedPanel -LabelWidth 92 -Width 58 -Tip 'Dynamic gimbal damping ratio. 1.0 is critically damped.' | Out-Null
    New-V3TuningTextBox -Name 'GimbalInputImpulse' -Label 'Impulse' -X 512 -Y 174 -Parent $advancedPanel -LabelWidth 92 -Width 58 -Tip 'How strongly each mouse delta kicks the dynamic gimbal velocity.' | Out-Null
    New-V3TuningTextBox -Name 'GimbalStaticFriction' -Label 'Static fric' -X 512 -Y 216 -Parent $advancedPanel -LabelWidth 92 -Width 58 -Tip 'Tiny center detent in trainer units.' | Out-Null
    New-V3TuningTextBox -Name 'GimbalDynamicFriction' -Label 'Dyn fric' -X 512 -Y 258 -Parent $advancedPanel -LabelWidth 92 -Width 58 -Tip 'Velocity friction for the dynamic gimbal.' | Out-Null
    New-V3TuningTextBox -Name 'GimbalEdgeBumper' -Label 'Edge' -X 512 -Y 300 -Parent $advancedPanel -LabelWidth 92 -Width 58 -Tip 'Extra inward force near the edge of throw.' | Out-Null
    New-V3TuningCheckBox -Name 'GimbalAntiwindupEnabled' -Label 'Anti-windup' -X 512 -Y 340 -Parent $advancedPanel -Width 150 -Tip 'Taper input that pushes farther into the dynamic-gimbal edge.' | Out-Null
    New-V3TuningTextBox -Name 'GimbalAntiwindupStart' -Label 'AW start' -X 512 -Y 376 -Parent $advancedPanel -LabelWidth 92 -Width 58 -Tip 'Fraction of max stick where anti-windup begins.' | Out-Null
    New-V3TuningTextBox -Name 'GimbalAntiwindupMinGain' -Label 'AW min' -X 512 -Y 418 -Parent $advancedPanel -LabelWidth 92 -Width 58 -Tip 'Smallest outward input gain at the edge.' | Out-Null
    New-V3TuningComboBox -Name 'InputGainMode' -Label 'Gain mode' -X 512 -Y 460 -Parent $advancedPanel -LabelWidth 92 -Width 116 -Items @('flat', 'adaptive') -Tip 'Flat keeps current sensitivity. Adaptive changes gain with recent mouse speed.' | Out-Null
    New-V3TuningTextBox -Name 'AdaptiveSlowGain' -Label 'Slow' -X 512 -Y 506 -Parent $advancedPanel -LabelWidth 40 -Width 58 -Tip 'Multiplier for slow mouse movement.' | Out-Null
    New-V3TuningTextBox -Name 'AdaptiveFastGain' -Label 'Fast' -X 632 -Y 506 -Parent $advancedPanel -LabelWidth 34 -Width 52 -Tip 'Multiplier reached by fast mouse movement.' | Out-Null
    New-V3TuningTextBox -Name 'AdaptiveSpeedLow' -Label 'Low spd' -X 512 -Y 548 -Parent $advancedPanel -LabelWidth 40 -Width 58 -Tip 'Mouse speed where adaptive gain starts blending.' | Out-Null
    New-V3TuningTextBox -Name 'AdaptiveSpeedHigh' -Label 'High' -X 632 -Y 548 -Parent $advancedPanel -LabelWidth 34 -Width 52 -Tip 'Mouse speed where adaptive gain reaches fast gain.' | Out-Null
    New-V3TuningTextBox -Name 'AdaptiveCurve' -Label 'Curve' -X 16 -Y 404 -Parent $advancedPanel -LabelWidth 104 -Width 70 -Tip 'Blend curve for adaptive input gain.' | Out-Null
    New-V3TuningTextBox -Name 'AdaptiveTrackerMs' -Label 'Tracker ms' -X 16 -Y 446 -Parent $advancedPanel -LabelWidth 104 -Width 70 -Tip 'Speed tracker smoothing window in milliseconds.' | Out-Null
    New-V3TuningComboBox -Name 'GateShape' -Label 'Gate' -X 16 -Y 488 -Parent $advancedPanel -LabelWidth 104 -Width 104 -Items @('axis', 'circle', 'octagon', 'square') -Tip '2D roll/pitch gate. Axis preserves independent-axis behavior.' | Out-Null
    New-V3TuningTextBox -Name 'DiagonalScale' -Label 'Diagonal' -X 16 -Y 530 -Parent $advancedPanel -LabelWidth 104 -Width 70 -Tip 'Diagonal authority for square/octagon gates.' | Out-Null

    $script:tuningFields.OutputCurve.Add_SelectedIndexChanged({ Update-RightStickControlState })
    foreach ($name in @('InputFilter', 'PositionModel', 'InputGainMode')) {
        $script:tuningFields[$name].Add_SelectedIndexChanged({ Update-RightStickControlState })
    }
    $script:tuningFields.ElasticReturnMode.Add_SelectedIndexChanged({
        $sel = [string]$script:tuningFields.ElasticReturnMode.SelectedItem
        $text = $script:elasticModeExplain[$sel]
        if ([string]::IsNullOrEmpty($text)) { $text = 'Named elastic return algorithm.' }
        $script:toolTip.SetToolTip($script:tuningFields.ElasticReturnMode, $text)
        if ($script:tuningLabels.ContainsKey('ElasticReturnMode')) {
            $script:toolTip.SetToolTip($script:tuningLabels.ElasticReturnMode, $text)
        }
    })
    foreach ($name in @('MouseRightStickEnabled', 'ReturnEnabled', 'ConstantReturnEnabled', 'ElasticReturnEnabled', 'DespikeEnabled', 'GimbalAntiwindupEnabled', 'OutputShapingEnabled', 'ReturnShapingEnabled')) {
        $script:tuningChecks[$name].Add_CheckedChanged({ Update-RightStickControlState })
    }

    return $section
}

function New-V3LeftStickSection {
    param(
        [System.Windows.Forms.Control]$Parent,
        [int]$X,
        [int]$Y,
        [int]$W
    )

    $section = New-DarkPanel -X $X -Y $Y -W $W -H 964 -Radius 8 -BackColor $script:dark.BgElev -BorderColor $script:dark.Border
    $section.Anchor = 'Top,Left,Right'
    $Parent.Controls.Add($section)

    $title = New-DarkLabel -Text 'Left Stick' -X 20 -Y 14 -W 240 -H 26 -Size 11.0 -Style ([System.Drawing.FontStyle]::Bold)
    $section.Controls.Add($title)

    $sourceBox = New-DarkPanel -X 22 -Y 52 -W 740 -H 64 -Radius 7 -BackColor $script:dark.BgSurface -BorderColor $script:dark.Border
    $sourceBox.Anchor = 'Top,Left,Right'
    $section.Controls.Add($sourceBox)
    $sourceLabel = New-DarkLabel -Text 'Source' -X 14 -Y 10 -W 80 -H 20 -Size 9.2 -Color $script:dark.TextMuted
    $sourceBox.Controls.Add($sourceLabel)
    $script:leftStickOffRadio = New-V3SourceRadioButton -Label 'Off' -X 86 -Y 18 -Parent $sourceBox -Width 96 -Tip 'Leave left-stick throttle low and yaw centered from the PC side.'
    [void](New-V3SourceRadioButton -Name 'KeyboardEnabled' -Label 'Keyboard / Wooting' -X 196 -Y 18 -Parent $sourceBox -Width 190 -Tip 'Use keyboard keys or Wooting analog key depth for throttle and yaw.')
    [void](New-V3SourceRadioButton -Name 'MouseLeftEnabled' -Label 'Second mouse' -X 402 -Y 18 -Parent $sourceBox -Width 160 -Tip 'Use a bound second mouse for left-stick throttle and yaw.')

    $keyboardBox = New-DarkPanel -X 22 -Y 132 -W 740 -H 380 -Radius 7 -BackColor $script:dark.BgSurface -BorderColor $script:dark.Border
    $keyboardBox.Anchor = 'Top,Left,Right'
    $section.Controls.Add($keyboardBox)
    $script:leftStickKeyboardBox = $keyboardBox
    $keyboardTitle = New-DarkLabel -Text 'Keyboard / Wooting' -X 18 -Y 14 -W 240 -H 24 -Size 10.4 -Style ([System.Drawing.FontStyle]::Bold)
    $keyboardBox.Controls.Add($keyboardTitle)

    New-V3TuningComboBox -Name 'KeyboardInputSource' -Label 'Input source' -X 18 -Y 48 -Parent $keyboardBox -LabelWidth 122 -Width 150 -Items @('gameinput', 'wooting_analog', 'auto') -Tip 'gameinput is digital keyboard input. wooting_analog reads analog key depth. auto falls back to digital if analog is unavailable.' | Out-Null
    New-V3TuningCheckBox -Name 'KeyboardRequireAnalog' -Label 'Require analog' -X 558 -Y 50 -Parent $keyboardBox -Width 150 -Tip 'Stop safely instead of falling back to digital keys if Wooting analog input fails.' | Out-Null

    New-V3TuningTextBox -Name 'ThrottleUpKey' -Label 'Throttle up' -X 18 -Y 92 -Parent $keyboardBox -LabelWidth 122 -Width 92 -KeyBinding -Tip 'Key that raises left-stick throttle.' | Out-Null
    New-V3TuningTextBox -Name 'ThrottleDownKey' -Label 'Throttle down' -X 18 -Y 132 -Parent $keyboardBox -LabelWidth 122 -Width 92 -KeyBinding -Tip 'Key that lowers left-stick throttle.' | Out-Null
    New-V3TuningTextBox -Name 'ThrottleCutKey' -Label 'Throttle cut' -X 18 -Y 172 -Parent $keyboardBox -LabelWidth 122 -Width 92 -KeyBinding -Tip 'Key that immediately drives throttle low.' | Out-Null
    New-V3TuningTextBox -Name 'ThrottleRate' -Label 'Throttle speed' -X 18 -Y 212 -Parent $keyboardBox -LabelWidth 122 -Width 92 -Tip 'Keyboard/Wooting throttle integration speed in trainer units per second.' | Out-Null
    New-V3TuningTextBox -Name 'ThrottleReturnRate' -Label 'Return speed' -X 18 -Y 252 -Parent $keyboardBox -LabelWidth 122 -Width 92 -Tip 'Throttle return speed when keyboard/Wooting throttle return is enabled.' | Out-Null

    New-V3TuningTextBox -Name 'YawLeftKey' -Label 'Yaw left' -X 382 -Y 92 -Parent $keyboardBox -LabelWidth 118 -Width 92 -KeyBinding -Tip 'Key that commands left-stick yaw left.' | Out-Null
    New-V3TuningTextBox -Name 'YawRightKey' -Label 'Yaw right' -X 382 -Y 132 -Parent $keyboardBox -LabelWidth 118 -Width 92 -KeyBinding -Tip 'Key that commands left-stick yaw right.' | Out-Null
    New-V3TuningTextBox -Name 'YawPulse' -Label 'Max yaw' -X 382 -Y 172 -Parent $keyboardBox -LabelWidth 118 -Width 92 -Tip 'Maximum keyboard/Wooting yaw output. 512 is full left-stick yaw.' | Out-Null
    New-V3TuningTextBox -Name 'YawSlewRate' -Label 'Yaw response' -X 382 -Y 212 -Parent $keyboardBox -LabelWidth 118 -Width 92 -Tip 'How quickly keyboard/Wooting yaw moves toward its target.' | Out-Null

    New-V3TuningCheckBox -Name 'ThrottleReturnEnabled' -Label 'Throttle return' -X 382 -Y 252 -Parent $keyboardBox -Width 140 -Tip 'Return keyboard/Wooting throttle toward low when no throttle key or analog pressure is active.' | Out-Null
    New-V3TuningCheckBox -Name 'BlockSelectedKeys' -Label 'Block keys' -X 522 -Y 252 -Parent $keyboardBox -Width 112 -Tip 'Prevent selected keyboard control keys from also reaching the foreground app while the trainer is running.' | Out-Null
    New-V3TuningCheckBox -Name 'InvertYaw' -Label 'Invert yaw' -X 630 -Y 252 -Parent $keyboardBox -Width 104 -Tip 'Reverse keyboard/Wooting yaw direction.' | Out-Null

    New-V3TuningComboBox -Name 'KeyboardAnalogKeycodeMode' -Label 'Analog key mode' -X 18 -Y 296 -Parent $keyboardBox -LabelWidth 122 -Width 190 -Items @('virtual_key_translate', 'virtual_key', 'hid', 'scancode1') -Tip 'Keycode translation mode passed to the Wooting Analog SDK.' | Out-Null
    New-V3TuningTextBox -Name 'KeyboardAnalogDeadzone' -Label 'Deadzone' -X 382 -Y 296 -Parent $keyboardBox -LabelWidth 82 -Width 64 -Tip 'Analog depth below this fraction is ignored.' | Out-Null
    New-V3TuningTextBox -Name 'KeyboardAnalogCurve' -Label 'Curve' -X 552 -Y 296 -Parent $keyboardBox -LabelWidth 54 -Width 64 -Tip 'Analog response curve. 1.0 is linear.' | Out-Null
    New-V3TuningTextBox -Name 'KeyboardAnalogMin' -Label 'Min' -X 382 -Y 336 -Parent $keyboardBox -LabelWidth 82 -Width 64 -Tip 'Analog depth mapped to zero after deadzone.' | Out-Null
    New-V3TuningTextBox -Name 'KeyboardAnalogMax' -Label 'Max' -X 552 -Y 336 -Parent $keyboardBox -LabelWidth 54 -Width 64 -Tip 'Analog depth mapped to full key pressure.' | Out-Null

    $mouseBox = New-DarkPanel -X 22 -Y 530 -W 740 -H 408 -Radius 7 -BackColor $script:dark.BgSurface -BorderColor $script:dark.Border
    $mouseBox.Anchor = 'Top,Left,Right'
    $section.Controls.Add($mouseBox)
    $script:leftStickMouseBox = $mouseBox
    $mouseTitle = New-DarkLabel -Text 'Second Mouse' -X 18 -Y 14 -W 240 -H 24 -Size 10.4 -Style ([System.Drawing.FontStyle]::Bold)
    $mouseBox.Controls.Add($mouseTitle)

    New-V3TuningTextBox -Name 'MouseDeviceLeft' -Label 'Left mouse token' -X 18 -Y 48 -Parent $mouseBox -LabelWidth 118 -Width 220 -Tip 'Use auto to bind the first distinct non-right mouse, or paste a GameInput root token.' | Out-Null
    New-V3TuningTextBox -Name 'MouseDeviceRight' -Label 'Right mouse token' -X 382 -Y 48 -Parent $mouseBox -LabelWidth 118 -Width 220 -Tip 'Use auto to route all mice except the bound left mouse to roll/pitch.' | Out-Null
    New-V3TuningTextBox -Name 'MouseLeftThrottleRate' -Label 'Throttle sens' -X 18 -Y 92 -Parent $mouseBox -LabelWidth 118 -Width 92 -Tip 'Left-mouse throttle integration speed.' | Out-Null
    New-V3TuningTextBox -Name 'MouseLeftYawGain' -Label 'Yaw sens' -X 382 -Y 92 -Parent $mouseBox -LabelWidth 118 -Width 92 -Tip 'Left-mouse X yaw sensitivity.' | Out-Null
    New-V3TuningTextBox -Name 'MouseLeftYawMax' -Label 'Max yaw' -X 18 -Y 136 -Parent $mouseBox -LabelWidth 118 -Width 92 -Tip 'Maximum second-mouse yaw output. 512 is full left-stick yaw.' | Out-Null
    New-V3TuningTextBox -Name 'MouseLeftYawDeadband' -Label 'Yaw deadband' -X 382 -Y 136 -Parent $mouseBox -LabelWidth 118 -Width 92 -Tip 'Small yaw outputs below this trainer-unit value are suppressed.' | Out-Null
    New-V3TuningTextBox -Name 'MouseLeftYawSlewRate' -Label 'Yaw response' -X 18 -Y 180 -Parent $mouseBox -LabelWidth 118 -Width 92 -Tip 'How quickly second-mouse yaw moves toward the target. 0 is instant.' | Out-Null
    New-V3TuningTextBox -Name 'MouseLeftYawSmoothing' -Label 'Yaw smoothing' -X 382 -Y 180 -Parent $mouseBox -LabelWidth 118 -Width 92 -Tip 'Legacy output smoothing for second-mouse yaw. 0 is immediate.' | Out-Null

    New-V3TuningCheckBox -Name 'MouseLeftThrottleReturnEnabled' -Label 'Throttle return' -X 18 -Y 224 -Parent $mouseBox -Width 160 -Tip 'Return second-mouse throttle toward low when the mouse is idle.' | Out-Null
    New-V3TuningTextBox -Name 'MouseLeftThrottleReturnRate' -Label 'Return speed' -X 214 -Y 224 -Parent $mouseBox -LabelWidth 104 -Width 76 -Tip 'Second-mouse throttle return speed.' | Out-Null
    New-V3TuningCheckBox -Name 'MouseLeftRequireDevice' -Label 'Require device' -X 18 -Y 262 -Parent $mouseBox -Width 150 -Tip 'Stop safely if the configured second mouse is not present.' | Out-Null
    New-V3TuningCheckBox -Name 'MouseLeftInvertThrottle' -Label 'Invert throttle' -X 182 -Y 262 -Parent $mouseBox -Width 150 -Tip 'Reverse second-mouse throttle direction.' | Out-Null
    New-V3TuningCheckBox -Name 'MouseLeftInvertYaw' -Label 'Invert yaw' -X 346 -Y 262 -Parent $mouseBox -Width 124 -Tip 'Reverse second-mouse yaw direction.' | Out-Null
    New-V3TuningCheckBox -Name 'MouseLeftSwapAxes' -Label 'Swap axes' -X 486 -Y 262 -Parent $mouseBox -Width 124 -Tip 'Route second-mouse X to throttle and Y to yaw before inversion is applied.' | Out-Null

    $idleBox = New-DarkPanel -X 18 -Y 312 -W 226 -H 78 -Radius 7 -BackColor $script:dark.BgElev -BorderColor $script:dark.Border
    $mouseBox.Controls.Add($idleBox)
    New-V3TuningCheckBox -Name 'MouseLeftYawReturnEnabled' -Label 'Yaw idle' -X 10 -Y 8 -Parent $idleBox -Width 104 -Tip 'Recenter left yaw after mouse movement stops.' | Out-Null
    New-V3TuningTextBox -Name 'MouseLeftYawReturnRate' -Label 'Rate' -X 10 -Y 40 -Parent $idleBox -LabelWidth 48 -Width 52 -Tip 'Left-yaw idle return speed.' | Out-Null
    New-V3TuningTextBox -Name 'MouseLeftYawReturnIdle' -Label 'Delay' -X 116 -Y 40 -Parent $idleBox -LabelWidth 48 -Width 44 -Tip 'Delay before idle yaw return starts.' | Out-Null

    $constantBox = New-DarkPanel -X 260 -Y 312 -W 154 -H 78 -Radius 7 -BackColor $script:dark.BgElev -BorderColor $script:dark.Border
    $mouseBox.Controls.Add($constantBox)
    New-V3TuningCheckBox -Name 'MouseLeftYawConstantReturnEnabled' -Label 'Yaw constant' -X 10 -Y 8 -Parent $constantBox -Width 132 -Tip 'Continuously recenter left yaw.' | Out-Null
    New-V3TuningTextBox -Name 'MouseLeftYawConstantReturnRate' -Label 'Rate' -X 10 -Y 40 -Parent $constantBox -LabelWidth 48 -Width 70 -Tip 'Left-yaw constant return speed.' | Out-Null

    $elasticBox = New-DarkPanel -X 424 -Y 312 -W 308 -H 78 -Radius 7 -BackColor $script:dark.BgElev -BorderColor $script:dark.Border
    $mouseBox.Controls.Add($elasticBox)
    New-V3TuningCheckBox -Name 'MouseLeftYawElasticReturnEnabled' -Label 'Yaw elastic' -X 10 -Y 8 -Parent $elasticBox -Width 128 -Tip 'Apply an elastic center spring to second-mouse yaw output.' | Out-Null
    New-V3TuningComboBox -Name 'MouseLeftYawElasticReturnMode' -Label 'Mode' -X 10 -Y 40 -Parent $elasticBox -LabelWidth 44 -Width 102 -Items @('progressive', 'linear', 'smoothstep', 'expo') -Tip 'Elastic return algorithm for second-mouse yaw.' | Out-Null
    New-V3TuningTextBox -Name 'MouseLeftYawElasticReturnCoefficient' -Label 'K' -X 182 -Y 40 -Parent $elasticBox -LabelWidth 18 -Width 42 -Tip 'Second-mouse yaw elastic spring coefficient.' | Out-Null
    New-V3TuningTextBox -Name 'MouseLeftYawElasticReturnCurve' -Label 'C' -X 248 -Y 40 -Parent $elasticBox -LabelWidth 16 -Width 32 -Tip 'Second-mouse yaw elastic curve.' | Out-Null

    $leftAdvancedButton = New-DarkButton -Text 'Yaw advanced' -Icon 0xE70D -Width 132 -Height 30 -Kind 'Ghost'
    $leftAdvancedButton.Location = New-Object System.Drawing.Point(630, 14)
    $leftAdvancedButton.Add_Click({
        $script:leftStickYawAdvancedExpanded = -not $script:leftStickYawAdvancedExpanded
        Refresh-V3EditorSectionLayout
    })
    $section.Controls.Add($leftAdvancedButton)

    $yawAdvanced = New-DarkPanel -X 22 -Y 956 -W 740 -H 630 -Radius 7 -BackColor $script:dark.BgSurface -BorderColor $script:dark.Border
    $yawAdvanced.Anchor = 'Top,Left,Right'
    $section.Controls.Add($yawAdvanced)
    $script:leftStickYawAdvancedPanel = $yawAdvanced

    $yawTitle = New-DarkLabel -Text 'Second Mouse Yaw Shaping' -X 16 -Y 14 -W 260 -H 22 -Size 10.2 -Style ([System.Drawing.FontStyle]::Bold)
    $yawAdvanced.Controls.Add($yawTitle)
    New-V3TuningCheckBox -Name 'MouseLeftYawShapingEnabled' -Label 'Enable yaw shaping' -X 510 -Y 14 -Parent $yawAdvanced -Width 178 -Tip 'Use the full left-yaw mapper stack with its own filters, gain, model, curves, and return settings.' | Out-Null

    $yawFilterTitle = New-DarkLabel -Text 'Input' -X 16 -Y 52 -W 160 -H 22 -Size 10.0 -Style ([System.Drawing.FontStyle]::Bold)
    $yawAdvanced.Controls.Add($yawFilterTitle)
    New-V3TuningComboBox -Name 'MouseLeftYawInputFilter' -Label 'Input filter' -X 16 -Y 84 -Parent $yawAdvanced -LabelWidth 106 -Width 118 -Items @('off', 'smoothing', 'one_euro') -Tip 'Pre-integrator filter for left-mouse yaw.' | Out-Null
    New-V3TuningTextBox -Name 'MouseLeftYawOneEuroMinCutoffHz' -Label '1-euro min' -X 16 -Y 126 -Parent $yawAdvanced -LabelWidth 106 -Width 68 -Tip 'Minimum 1-euro cutoff for yaw.' | Out-Null
    New-V3TuningTextBox -Name 'MouseLeftYawOneEuroBeta' -Label '1-euro beta' -X 16 -Y 168 -Parent $yawAdvanced -LabelWidth 106 -Width 68 -Tip 'How strongly yaw 1-euro cutoff rises with speed.' | Out-Null
    New-V3TuningTextBox -Name 'MouseLeftYawOneEuroDcutoffHz' -Label '1-euro dcut' -X 16 -Y 210 -Parent $yawAdvanced -LabelWidth 106 -Width 68 -Tip 'Derivative low-pass cutoff for yaw 1-euro speed estimate.' | Out-Null
    New-V3TuningCheckBox -Name 'MouseLeftYawDespikeEnabled' -Label 'Yaw despike' -X 16 -Y 252 -Parent $yawAdvanced -Width 132 -Tip 'Hampel filter that replaces isolated left-yaw mouse spikes.' | Out-Null
    New-V3TuningCheckBox -Name 'MouseLeftYawDespikeCountEnabled' -Label 'Count yaw despikes' -X 148 -Y 252 -Parent $yawAdvanced -Width 158 -Tip 'Report left-yaw Hampel discard counts while running.' | Out-Null
    New-V3TuningTextBox -Name 'MouseLeftYawDespikeWindow' -Label 'Window' -X 16 -Y 294 -Parent $yawAdvanced -LabelWidth 106 -Width 68 -Tip 'Odd sample window for yaw mouse despiking.' | Out-Null
    New-V3TuningTextBox -Name 'MouseLeftYawDespikeThresholdSigma' -Label 'Sigma' -X 16 -Y 336 -Parent $yawAdvanced -LabelWidth 106 -Width 68 -Tip 'MAD-derived sigma threshold for yaw despiking.' | Out-Null

    $yawOutputTitle = New-DarkLabel -Text 'Output / Return' -X 268 -Y 52 -W 180 -H 22 -Size 10.0 -Style ([System.Drawing.FontStyle]::Bold)
    $yawAdvanced.Controls.Add($yawOutputTitle)
    New-V3TuningComboBox -Name 'MouseLeftYawOutputCurve' -Label 'Output curve' -X 268 -Y 84 -Parent $yawAdvanced -LabelWidth 104 -Width 108 -Items @('expo', 'nodes', 'actual') -Tip 'Left-yaw output curve: expo, nodes, or Actual Rates style.' | Out-Null
    New-V3TuningTextBox -Name 'MouseLeftYawExpo' -Label 'Yaw expo' -X 268 -Y 126 -Parent $yawAdvanced -LabelWidth 104 -Width 64 -Tip 'Expo for left-mouse yaw output.' | Out-Null
    New-V3TuningTextBox -Name 'MouseLeftYawActualCenter' -Label 'Actual center' -X 268 -Y 168 -Parent $yawAdvanced -LabelWidth 104 -Width 64 -Tip 'Actual Rates center sensitivity for yaw.' | Out-Null
    New-V3TuningTextBox -Name 'MouseLeftYawActualMax' -Label 'Actual max' -X 268 -Y 210 -Parent $yawAdvanced -LabelWidth 104 -Width 64 -Tip 'Actual Rates max output fraction for yaw.' | Out-Null
    New-V3TuningTextBox -Name 'MouseLeftYawActualExpo' -Label 'Actual expo' -X 268 -Y 252 -Parent $yawAdvanced -LabelWidth 104 -Width 64 -Tip 'Actual Rates bend control for yaw.' | Out-Null
    New-V3TuningCheckBox -Name 'MouseLeftYawOutputShapingEnabled' -Label 'Output shaping' -X 268 -Y 294 -Parent $yawAdvanced -Width 150 -Tip 'Enable free-form left-yaw output shaping nodes.' | Out-Null
    New-V3StickShapeEditor -Name 'MouseLeftYawOutputShape' -Label 'Yaw output shape' -X 268 -Y 328 -Parent $yawAdvanced -Width 206 -Height 94 -CurveColor ([System.Drawing.Color]::FromArgb(79, 141, 253)) -Title 'Left Yaw Output Shaping' -Tip 'Click empty area to add a node, drag to move, scroll to widen, right-click to remove.' | Out-Null
    New-V3TuningCheckBox -Name 'MouseLeftYawReturnShapingEnabled' -Label 'Return shaping' -X 268 -Y 460 -Parent $yawAdvanced -Width 150 -Tip 'Enable free-form left-yaw return shaping nodes.' | Out-Null
    New-V3StickShapeEditor -Name 'MouseLeftYawReturnShape' -Label 'Yaw return shape' -X 268 -Y 494 -Parent $yawAdvanced -Width 206 -Height 94 -CurveColor ([System.Drawing.Color]::FromArgb(240, 184, 91)) -Title 'Left Yaw Return Shaping' -Tip 'Click empty area to add a node, drag to move, scroll to widen, right-click to remove.' | Out-Null

    $yawModelTitle = New-DarkLabel -Text 'Model / Gain' -X 512 -Y 52 -W 180 -H 22 -Size 10.0 -Style ([System.Drawing.FontStyle]::Bold)
    $yawAdvanced.Controls.Add($yawModelTitle)
    New-V3TuningComboBox -Name 'MouseLeftYawPositionModel' -Label 'Position' -X 512 -Y 84 -Parent $yawAdvanced -LabelWidth 92 -Width 116 -Items @('integrator', 'dynamic_gimbal') -Tip 'Left-yaw position model. Dynamic gimbal gives yaw its own damped virtual stick model.' | Out-Null
    New-V3TuningTextBox -Name 'MouseLeftYawGimbalFrequencyHz' -Label 'Gimbal Hz' -X 512 -Y 126 -Parent $yawAdvanced -LabelWidth 92 -Width 58 -Tip 'Dynamic yaw gimbal spring frequency.' | Out-Null
    New-V3TuningTextBox -Name 'MouseLeftYawGimbalDampingRatio' -Label 'Damping' -X 512 -Y 168 -Parent $yawAdvanced -LabelWidth 92 -Width 58 -Tip 'Dynamic yaw gimbal damping ratio.' | Out-Null
    New-V3TuningTextBox -Name 'MouseLeftYawGimbalInputImpulse' -Label 'Impulse' -X 512 -Y 210 -Parent $yawAdvanced -LabelWidth 92 -Width 58 -Tip 'How strongly each yaw mouse delta kicks the dynamic yaw gimbal.' | Out-Null
    New-V3TuningTextBox -Name 'MouseLeftYawGimbalStaticFriction' -Label 'Static fric' -X 512 -Y 252 -Parent $yawAdvanced -LabelWidth 92 -Width 58 -Tip 'Tiny yaw center detent in trainer units.' | Out-Null
    New-V3TuningTextBox -Name 'MouseLeftYawGimbalDynamicFriction' -Label 'Dyn fric' -X 512 -Y 294 -Parent $yawAdvanced -LabelWidth 92 -Width 58 -Tip 'Velocity friction for dynamic yaw gimbal.' | Out-Null
    New-V3TuningTextBox -Name 'MouseLeftYawGimbalEdgeBumper' -Label 'Edge' -X 512 -Y 336 -Parent $yawAdvanced -LabelWidth 92 -Width 58 -Tip 'Extra inward force near full yaw throw.' | Out-Null
    New-V3TuningCheckBox -Name 'MouseLeftYawGimbalAntiwindupEnabled' -Label 'Anti-windup' -X 512 -Y 376 -Parent $yawAdvanced -Width 150 -Tip 'Taper dynamic-yaw-gimbal input near the edge.' | Out-Null
    New-V3TuningTextBox -Name 'MouseLeftYawGimbalAntiwindupStart' -Label 'AW start' -X 512 -Y 412 -Parent $yawAdvanced -LabelWidth 92 -Width 58 -Tip 'Fraction of max yaw where anti-windup begins.' | Out-Null
    New-V3TuningTextBox -Name 'MouseLeftYawGimbalAntiwindupMinGain' -Label 'AW min' -X 512 -Y 454 -Parent $yawAdvanced -LabelWidth 92 -Width 58 -Tip 'Smallest outward yaw input gain at the edge.' | Out-Null
    New-V3TuningComboBox -Name 'MouseLeftYawInputGainMode' -Label 'Gain mode' -X 512 -Y 496 -Parent $yawAdvanced -LabelWidth 92 -Width 116 -Items @('flat', 'adaptive') -Tip 'Flat keeps yaw sensitivity constant. Adaptive changes yaw gain with mouse speed.' | Out-Null
    New-V3TuningTextBox -Name 'MouseLeftYawAdaptiveSlowGain' -Label 'Slow' -X 512 -Y 538 -Parent $yawAdvanced -LabelWidth 40 -Width 58 -Tip 'Yaw multiplier for slow mouse movement.' | Out-Null
    New-V3TuningTextBox -Name 'MouseLeftYawAdaptiveFastGain' -Label 'Fast' -X 628 -Y 538 -Parent $yawAdvanced -LabelWidth 34 -Width 66 -Tip 'Yaw multiplier reached by fast mouse movement.' | Out-Null
    New-V3TuningTextBox -Name 'MouseLeftYawAdaptiveSpeedLow' -Label 'Low spd' -X 512 -Y 578 -Parent $yawAdvanced -LabelWidth 40 -Width 58 -Tip 'Mouse speed where adaptive yaw begins blending.' | Out-Null
    New-V3TuningTextBox -Name 'MouseLeftYawAdaptiveSpeedHigh' -Label 'High' -X 628 -Y 578 -Parent $yawAdvanced -LabelWidth 34 -Width 66 -Tip 'Mouse speed where adaptive yaw reaches fast gain.' | Out-Null
    New-V3TuningTextBox -Name 'MouseLeftYawAdaptiveCurve' -Label 'Adaptive curve' -X 16 -Y 378 -Parent $yawAdvanced -LabelWidth 106 -Width 68 -Tip 'Blend curve for adaptive yaw input gain.' | Out-Null
    New-V3TuningTextBox -Name 'MouseLeftYawAdaptiveTrackerMs' -Label 'Tracker ms' -X 16 -Y 420 -Parent $yawAdvanced -LabelWidth 106 -Width 68 -Tip 'Yaw speed tracker smoothing window.' | Out-Null
    New-V3TuningComboBox -Name 'MouseLeftYawGateShape' -Label 'Yaw gate' -X 16 -Y 462 -Parent $yawAdvanced -LabelWidth 106 -Width 104 -Items @('axis', 'circle', 'octagon', 'square') -Tip 'Stored for mapper parity; current left yaw is one-axis.' | Out-Null
    New-V3TuningTextBox -Name 'MouseLeftYawDiagonalScale' -Label 'Diagonal' -X 16 -Y 504 -Parent $yawAdvanced -LabelWidth 106 -Width 68 -Tip 'Stored for mapper parity; currently no effect on yaw-only shaping.' | Out-Null

    $script:leftStickOffRadio.Add_CheckedChanged({
        if ($script:leftStickOffRadio.Checked) {
            Update-LeftStickControlState
        }
    })
    $script:tuningChecks.KeyboardEnabled.Add_CheckedChanged({
        if ($script:tuningChecks.KeyboardEnabled.Checked -and -not $script:loadingProfile) {
            if ([string]::IsNullOrWhiteSpace($script:tuningFields.KeyboardInputSource.Text)) {
                $script:tuningFields.KeyboardInputSource.Text = 'gameinput'
            }
        }
        Update-LeftStickControlState
    })
    $script:tuningChecks.MouseLeftEnabled.Add_CheckedChanged({
        if ($script:tuningChecks.MouseLeftEnabled.Checked -and -not $script:loadingProfile) {
            if ([string]::IsNullOrWhiteSpace($script:tuningFields.MouseDeviceLeft.Text)) {
                $script:tuningFields.MouseDeviceLeft.Text = 'auto'
            }
            if ([string]::IsNullOrWhiteSpace($script:tuningFields.MouseDeviceRight.Text)) {
                $script:tuningFields.MouseDeviceRight.Text = 'auto'
            }
        }
        Update-LeftStickControlState
    })
    foreach ($name in @('ThrottleReturnEnabled', 'MouseLeftThrottleReturnEnabled', 'MouseLeftYawReturnEnabled', 'MouseLeftYawConstantReturnEnabled', 'MouseLeftYawElasticReturnEnabled', 'MouseLeftYawShapingEnabled', 'MouseLeftYawDespikeEnabled', 'MouseLeftYawGimbalAntiwindupEnabled', 'MouseLeftYawOutputShapingEnabled', 'MouseLeftYawReturnShapingEnabled')) {
        $script:tuningChecks[$name].Add_CheckedChanged({ Update-LeftStickControlState })
    }
    $script:tuningFields.KeyboardInputSource.Add_SelectedIndexChanged({ Update-LeftStickControlState })
    foreach ($name in @('MouseLeftYawInputFilter', 'MouseLeftYawOutputCurve', 'MouseLeftYawPositionModel', 'MouseLeftYawInputGainMode')) {
        $script:tuningFields[$name].Add_SelectedIndexChanged({ Update-LeftStickControlState })
    }

    return $section
}

function New-V3DroneAimSection {
    param(
        [System.Windows.Forms.Control]$Parent,
        [int]$X,
        [int]$Y,
        [int]$W
    )

    $section = New-DarkPanel -X $X -Y $Y -W $W -H 74 -Radius 8 -BackColor $script:dark.BgElev -BorderColor $script:dark.Border
    $section.Anchor = 'Top,Left,Right'
    $Parent.Controls.Add($section)

    $title = New-DarkLabel -Text 'Drone Aim' -X 20 -Y 14 -W 240 -H 26 -Size 11.0 -Style ([System.Drawing.FontStyle]::Bold)
    $section.Controls.Add($title)

    $hint = New-DarkLabel -Text 'Sim / bench only until safety review' -X 132 -Y 18 -W 260 -H 20 -Size 8.6 -Color $script:dark.Warn
    $section.Controls.Add($hint)

    $toggle = New-DarkButton -Text 'Show' -Icon 0xE70D -Width 92 -Height 30 -Kind 'Ghost'
    $toggle.Location = New-Object System.Drawing.Point(670, 18)
    $toggle.Add_Click({
        $script:droneAimExpanded = -not $script:droneAimExpanded
        $toggle.Text = if ($script:droneAimExpanded) { 'Hide' } else { 'Show' }
        Refresh-V3EditorSectionLayout
    })
    $section.Controls.Add($toggle)

    $content = New-DarkPanel -X 22 -Y 58 -W 740 -H 356 -Radius 7 -BackColor $script:dark.BgSurface -BorderColor $script:dark.Border
    $content.Anchor = 'Top,Left,Right'
    $section.Controls.Add($content)
    $script:droneAimContentPanel = $content

    New-V3TuningTextBox -Name 'AimSensitivityX' -Label 'Aim sens X' -X 18 -Y 36 -Parent $content -LabelWidth 120 -Width 78 -Tip 'Horizontal mouse sensitivity for moving the virtual aim reticle.' | Out-Null
    New-V3TuningTextBox -Name 'AimSensitivityY' -Label 'Aim sens Y' -X 18 -Y 78 -Parent $content -LabelWidth 120 -Width 78 -Tip 'Vertical mouse sensitivity for moving the virtual aim reticle.' | Out-Null
    New-V3TuningTextBox -Name 'AimReticleLimit' -Label 'Reticle limit' -X 18 -Y 120 -Parent $content -LabelWidth 120 -Width 78 -Tip 'Maximum virtual reticle offset before aim input saturates.' | Out-Null
    New-V3TuningTextBox -Name 'AimDeadband' -Label 'Aim deadband' -X 18 -Y 162 -Parent $content -LabelWidth 120 -Width 78 -Tip 'Small virtual reticle offsets below this value produce no aim correction.' | Out-Null
    New-V3TuningTextBox -Name 'AimReturnRate' -Label 'Aim return' -X 18 -Y 204 -Parent $content -LabelWidth 120 -Width 78 -Tip 'How quickly the virtual reticle drifts back toward center when mouse input stops.' | Out-Null
    New-V3TuningTextBox -Name 'AimSmoothing' -Label 'Aim smoothing' -X 18 -Y 246 -Parent $content -LabelWidth 120 -Width 78 -Tip 'Filtering applied to aim outputs. 0 is immediate.' | Out-Null

    New-V3TuningTextBox -Name 'AimRollGain' -Label 'Roll gain' -X 382 -Y 36 -Parent $content -LabelWidth 112 -Width 78 -Tip 'How strongly horizontal aim error commands right-stick roll.' | Out-Null
    New-V3TuningTextBox -Name 'AimYawGain' -Label 'Yaw gain' -X 382 -Y 78 -Parent $content -LabelWidth 112 -Width 78 -Tip 'How strongly horizontal aim error commands left-stick yaw.' | Out-Null
    New-V3TuningTextBox -Name 'AimPitchGain' -Label 'Pitch gain' -X 382 -Y 120 -Parent $content -LabelWidth 112 -Width 78 -Tip 'How strongly vertical aim error commands right-stick pitch.' | Out-Null
    New-V3TuningTextBox -Name 'AimRollMax' -Label 'Roll max' -X 382 -Y 162 -Parent $content -LabelWidth 112 -Width 78 -Tip 'Maximum roll output drone aim can command. 512 is full right-stick roll.' | Out-Null
    New-V3TuningTextBox -Name 'AimYawMax' -Label 'Yaw max' -X 382 -Y 204 -Parent $content -LabelWidth 112 -Width 78 -Tip 'Maximum yaw output drone aim can command. 512 is full left-stick yaw.' | Out-Null
    New-V3TuningTextBox -Name 'AimPitchMax' -Label 'Pitch max' -X 382 -Y 246 -Parent $content -LabelWidth 112 -Width 78 -Tip 'Maximum pitch output drone aim can command. 512 is full right-stick pitch.' | Out-Null
    New-V3TuningTextBox -Name 'AimSlewRate' -Label 'Aim slew' -X 382 -Y 288 -Parent $content -LabelWidth 112 -Width 78 -Tip 'Rate limit for aim-driven stick changes. 0 disables the limit.' | Out-Null

    New-V3TuningCheckBox -Name 'AimInvertX' -Label 'Invert X' -X 18 -Y 300 -Parent $content -Width 110 -Tip 'Reverse horizontal mouse input in reticle aim mode.' | Out-Null
    New-V3TuningCheckBox -Name 'AimInvertY' -Label 'Invert Y' -X 134 -Y 300 -Parent $content -Width 110 -Tip 'Reverse vertical mouse input in reticle aim mode.' | Out-Null

    return $section
}

function Validate-TuningControls {
    foreach ($name in @('ProfileName', 'FrameRate', 'StopKey', 'FreezeKey')) {
        if (-not $script:tuningFields.ContainsKey($name)) {
            return $true
        }
    }

    $profileName = $script:tuningFields.ProfileName.Text.Trim()
    $checks = @(
        @{ Name = 'ProfileName'; Valid = ((-not [string]::IsNullOrWhiteSpace($profileName)) -and ($profileName -notmatch "[`r`n]")); Message = 'profile name is required' },
        @{ Name = 'FrameRate'; Valid = (Test-IntField -Text $script:tuningFields.FrameRate.Text -Min 1 -Max 8000); Message = 'frame rate must be 1..8000' },
        @{ Name = 'StopKey'; Valid = (Test-KeyField -Text $script:tuningFields.StopKey.Text); Message = 'stop key is not recognized' },
        @{ Name = 'FreezeKey'; Valid = (Test-KeyField -Text $script:tuningFields.FreezeKey.Text); Message = 'freeze key is not recognized' }
    )

    $rightStickFieldsReady = $true
    foreach ($name in @('RollGain', 'PitchGain', 'MaxOutput', 'Deadband', 'OutputCurve', 'Expo', 'ReturnRate', 'ReturnIdle', 'ConstantReturnRate', 'ElasticReturnMode', 'ElasticReturnCoefficient', 'ElasticReturnCurve')) {
        if (-not $script:tuningFields.ContainsKey($name)) {
            $rightStickFieldsReady = $false
        }
    }
    if ($rightStickFieldsReady) {
        $checks += @(
            @{ Name = 'RollGain'; Valid = (Test-DoubleField -Text $script:tuningFields.RollGain.Text -Min 0 -Max 5000); Message = 'roll gain must be 0..5000' },
            @{ Name = 'PitchGain'; Valid = (Test-DoubleField -Text $script:tuningFields.PitchGain.Text -Min 0 -Max 5000); Message = 'pitch gain must be 0..5000' },
            @{ Name = 'MaxOutput'; Valid = (Test-IntField -Text $script:tuningFields.MaxOutput.Text -Min 1 -Max 512); Message = 'max output must be 1..512' },
            @{ Name = 'Deadband'; Valid = (Test-IntField -Text $script:tuningFields.Deadband.Text -Min 0 -Max 511); Message = 'deadband must be 0..511' },
            @{ Name = 'OutputCurve'; Valid = (Test-OutputCurveField -Text $script:tuningFields.OutputCurve.Text); Message = 'output curve must be expo, nodes, or actual' },
            @{ Name = 'Expo'; Valid = (Test-DoubleField -Text $script:tuningFields.Expo.Text -Min 0 -Max 1); Message = 'expo must be 0.0..1.0' },
            @{ Name = 'ReturnRate'; Valid = (Test-DoubleField -Text $script:tuningFields.ReturnRate.Text -Min 0 -Max 20000); Message = 'idle return rate must be 0..20000' },
            @{ Name = 'ReturnIdle'; Valid = (Test-DoubleField -Text $script:tuningFields.ReturnIdle.Text -Min 0 -Max 1000); Message = 'idle return delay must be 0..1000 ms' },
            @{ Name = 'ConstantReturnRate'; Valid = (Test-DoubleField -Text $script:tuningFields.ConstantReturnRate.Text -Min 0 -Max 20000); Message = 'constant return rate must be 0..20000' },
            @{ Name = 'ElasticReturnMode'; Valid = (Test-ElasticReturnModeField -Text $script:tuningFields.ElasticReturnMode.Text); Message = 'elastic return mode must be linear, progressive, smoothstep, or expo' },
            @{ Name = 'ElasticReturnCoefficient'; Valid = (Test-DoubleField -Text $script:tuningFields.ElasticReturnCoefficient.Text -Min 0 -Max 100); Message = 'elastic return coefficient must be 0..100' },
            @{ Name = 'ElasticReturnCurve'; Valid = (Test-DoubleField -Text $script:tuningFields.ElasticReturnCurve.Text -Min 0 -Max 5); Message = 'elastic return curve must be 0..5' }
        )
    }

    $rightAdvancedReady = $true
    foreach ($name in ($script:rightStickAdvancedFieldNames + $script:rightStickAdvancedCheckNames)) {
        if (($script:rightStickAdvancedCheckNames -contains $name)) {
            if (-not $script:tuningChecks.ContainsKey($name)) { $rightAdvancedReady = $false }
        } else {
            if (-not $script:tuningFields.ContainsKey($name)) { $rightAdvancedReady = $false }
        }
    }
    if ($rightAdvancedReady) {
        $checks += @(
            @{ Name = 'Smoothing'; Valid = (Test-DoubleField -Text $script:tuningFields.Smoothing.Text -Min 0 -Max 1 -MaxExclusive); Message = 'smoothing must be >=0.0 and <1.0' },
            @{ Name = 'InputFilter'; Valid = (Test-InputFilterField -Text $script:tuningFields.InputFilter.Text); Message = 'input filter must be off, smoothing, or one_euro' },
            @{ Name = 'OneEuroMinCutoffHz'; Valid = (Test-DoubleField -Text $script:tuningFields.OneEuroMinCutoffHz.Text -Min 0.05 -Max 120); Message = 'one-euro min cutoff must be 0.05..120 Hz' },
            @{ Name = 'OneEuroBeta'; Valid = (Test-DoubleField -Text $script:tuningFields.OneEuroBeta.Text -Min 0 -Max 2); Message = 'one-euro beta must be 0..2' },
            @{ Name = 'OneEuroDcutoffHz'; Valid = (Test-DoubleField -Text $script:tuningFields.OneEuroDcutoffHz.Text -Min 0.1 -Max 120); Message = 'one-euro derivative cutoff must be 0.1..120 Hz' },
            @{ Name = 'DespikeWindow'; Valid = ((Test-IntField -Text $script:tuningFields.DespikeWindow.Text -Min 3 -Max 15) -and (([int]$script:tuningFields.DespikeWindow.Text.Trim() % 2) -eq 1)); Message = 'despike window must be odd and 3..15' },
            @{ Name = 'DespikeThresholdSigma'; Valid = (Test-DoubleField -Text $script:tuningFields.DespikeThresholdSigma.Text -Min 0.0001 -Max 20); Message = 'despike threshold sigma must be >0 and <=20' },
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
            @{ Name = 'DiagonalScale'; Valid = (Test-DoubleField -Text $script:tuningFields.DiagonalScale.Text -Min 0 -Max 1.5); Message = 'diagonal scale must be 0..1.5' }
        )
    }

    $leftStickFieldsReady = $true
    foreach ($name in ($script:leftStickKeyboardFieldNames + $script:leftStickMouseFieldNames)) {
        if (-not $script:tuningFields.ContainsKey($name)) {
            $leftStickFieldsReady = $false
        }
    }
    foreach ($name in ($script:leftStickKeyboardCheckNames + $script:leftStickMouseCheckNames)) {
        if (-not $script:tuningChecks.ContainsKey($name)) {
            $leftStickFieldsReady = $false
        }
    }
    if ($leftStickFieldsReady) {
        $checks += @(
            @{ Name = 'KeyboardInputSource'; Valid = (Test-V3ChoiceField -Text $script:tuningFields.KeyboardInputSource.Text -Choices @('gameinput', 'wooting_analog', 'auto')); Message = 'keyboard source must be gameinput, wooting_analog, or auto' },
            @{ Name = 'ThrottleUpKey'; Valid = (Test-KeyField -Text $script:tuningFields.ThrottleUpKey.Text); Message = 'throttle up key is not recognized' },
            @{ Name = 'ThrottleDownKey'; Valid = (Test-KeyField -Text $script:tuningFields.ThrottleDownKey.Text); Message = 'throttle down key is not recognized' },
            @{ Name = 'ThrottleCutKey'; Valid = (Test-KeyField -Text $script:tuningFields.ThrottleCutKey.Text); Message = 'throttle cut key is not recognized' },
            @{ Name = 'ThrottleRate'; Valid = (Test-DoubleField -Text $script:tuningFields.ThrottleRate.Text -Min 0 -Max 10000); Message = 'throttle rate must be 0..10000' },
            @{ Name = 'ThrottleReturnRate'; Valid = (Test-DoubleField -Text $script:tuningFields.ThrottleReturnRate.Text -Min 0 -Max 20000); Message = 'throttle return rate must be 0..20000' },
            @{ Name = 'YawLeftKey'; Valid = (Test-KeyField -Text $script:tuningFields.YawLeftKey.Text); Message = 'yaw left key is not recognized' },
            @{ Name = 'YawRightKey'; Valid = (Test-KeyField -Text $script:tuningFields.YawRightKey.Text); Message = 'yaw right key is not recognized' },
            @{ Name = 'YawPulse'; Valid = (Test-IntField -Text $script:tuningFields.YawPulse.Text -Min 0 -Max 512); Message = 'yaw pulse must be 0..512' },
            @{ Name = 'YawSlewRate'; Valid = (Test-DoubleField -Text $script:tuningFields.YawSlewRate.Text -Min 0 -Max 20000); Message = 'yaw slew must be 0..20000' },
            @{ Name = 'KeyboardAnalogKeycodeMode'; Valid = (Test-V3ChoiceField -Text $script:tuningFields.KeyboardAnalogKeycodeMode.Text -Choices @('virtual_key_translate', 'virtual_key', 'hid', 'scancode1')); Message = 'analog key mode must be virtual_key_translate, virtual_key, hid, or scancode1' },
            @{ Name = 'KeyboardAnalogDeadzone'; Valid = (Test-DoubleField -Text $script:tuningFields.KeyboardAnalogDeadzone.Text -Min 0 -Max 0.95 -MaxExclusive); Message = 'analog deadzone must be >=0 and <0.95' },
            @{ Name = 'KeyboardAnalogCurve'; Valid = (Test-DoubleField -Text $script:tuningFields.KeyboardAnalogCurve.Text -Min 0.0001 -Max 5); Message = 'analog curve must be >0 and <=5' },
            @{ Name = 'KeyboardAnalogMin'; Valid = (Test-DoubleField -Text $script:tuningFields.KeyboardAnalogMin.Text -Min 0 -Max 1 -MaxExclusive); Message = 'analog min must be >=0 and <1' },
            @{ Name = 'KeyboardAnalogMax'; Valid = (Test-DoubleField -Text $script:tuningFields.KeyboardAnalogMax.Text -Min 0.0001 -Max 1); Message = 'analog max must be >0 and <=1' },
            @{ Name = 'MouseLeftThrottleRate'; Valid = (Test-DoubleField -Text $script:tuningFields.MouseLeftThrottleRate.Text -Min 0 -Max 10000); Message = 'mouse throttle sensitivity must be 0..10000' },
            @{ Name = 'MouseLeftThrottleReturnRate'; Valid = (Test-DoubleField -Text $script:tuningFields.MouseLeftThrottleReturnRate.Text -Min 0 -Max 20000); Message = 'mouse throttle return must be 0..20000' },
            @{ Name = 'MouseLeftYawGain'; Valid = (Test-DoubleField -Text $script:tuningFields.MouseLeftYawGain.Text -Min 0 -Max 5000); Message = 'mouse yaw sensitivity must be 0..5000' },
            @{ Name = 'MouseLeftYawMax'; Valid = (Test-IntField -Text $script:tuningFields.MouseLeftYawMax.Text -Min 0 -Max 512); Message = 'mouse max yaw must be 0..512' },
            @{ Name = 'MouseLeftYawDeadband'; Valid = (Test-IntField -Text $script:tuningFields.MouseLeftYawDeadband.Text -Min 0 -Max 511); Message = 'mouse yaw deadband must be 0..511' },
            @{ Name = 'MouseLeftYawSmoothing'; Valid = (Test-DoubleField -Text $script:tuningFields.MouseLeftYawSmoothing.Text -Min 0 -Max 1 -MaxExclusive); Message = 'mouse yaw smoothing must be >=0 and <1' },
            @{ Name = 'MouseLeftYawSlewRate'; Valid = (Test-DoubleField -Text $script:tuningFields.MouseLeftYawSlewRate.Text -Min 0 -Max 20000); Message = 'mouse yaw response must be 0..20000' },
            @{ Name = 'MouseLeftYawReturnRate'; Valid = (Test-DoubleField -Text $script:tuningFields.MouseLeftYawReturnRate.Text -Min 0 -Max 20000); Message = 'mouse yaw idle return must be 0..20000' },
            @{ Name = 'MouseLeftYawReturnIdle'; Valid = (Test-DoubleField -Text $script:tuningFields.MouseLeftYawReturnIdle.Text -Min 0 -Max 60000); Message = 'mouse yaw idle delay must be 0..60000 ms' },
            @{ Name = 'MouseLeftYawConstantReturnRate'; Valid = (Test-DoubleField -Text $script:tuningFields.MouseLeftYawConstantReturnRate.Text -Min 0 -Max 20000); Message = 'mouse yaw constant return must be 0..20000' },
            @{ Name = 'MouseLeftYawElasticReturnMode'; Valid = (Test-ElasticReturnModeField -Text $script:tuningFields.MouseLeftYawElasticReturnMode.Text); Message = 'mouse yaw elastic mode must be linear, progressive, smoothstep, or expo' },
            @{ Name = 'MouseLeftYawElasticReturnCoefficient'; Valid = (Test-DoubleField -Text $script:tuningFields.MouseLeftYawElasticReturnCoefficient.Text -Min 0 -Max 100); Message = 'mouse yaw elastic coefficient must be 0..100' },
            @{ Name = 'MouseLeftYawElasticReturnCurve'; Valid = (Test-DoubleField -Text $script:tuningFields.MouseLeftYawElasticReturnCurve.Text -Min 0 -Max 5); Message = 'mouse yaw elastic curve must be 0..5' }
        )
    }

    $leftAdvancedReady = $true
    foreach ($name in ($script:leftStickMouseAdvancedFieldNames + $script:leftStickMouseAdvancedCheckNames)) {
        if ($script:leftStickMouseAdvancedCheckNames -contains $name) {
            if (-not $script:tuningChecks.ContainsKey($name)) { $leftAdvancedReady = $false }
        } else {
            if (-not $script:tuningFields.ContainsKey($name)) { $leftAdvancedReady = $false }
        }
    }
    if ($leftAdvancedReady) {
        $checks += @(
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
            @{ Name = 'MouseLeftYawDiagonalScale'; Valid = (Test-DoubleField -Text $script:tuningFields.MouseLeftYawDiagonalScale.Text -Min 0 -Max 1.5); Message = 'mouse yaw diagonal scale must be 0..1.5' }
        )
    }

    $droneAimReady = $true
    foreach ($name in ($script:droneAimFieldNames + $script:droneAimCheckNames)) {
        if ($script:droneAimCheckNames -contains $name) {
            if (-not $script:tuningChecks.ContainsKey($name)) { $droneAimReady = $false }
        } else {
            if (-not $script:tuningFields.ContainsKey($name)) { $droneAimReady = $false }
        }
    }
    if ($droneAimReady) {
        $checks += @(
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
            @{ Name = 'AimSlewRate'; Valid = (Test-DoubleField -Text $script:tuningFields.AimSlewRate.Text -Min 0 -Max 20000); Message = 'aim slew rate must be 0..20000' }
        )
    }

    $ok = $true
    $firstMessage = ''
    foreach ($check in $checks) {
        Set-V3TuningFieldValid -Name $check.Name -Valid $check.Valid
        if (-not $check.Valid) {
            $ok = $false
            if ([string]::IsNullOrWhiteSpace($firstMessage)) {
                $firstMessage = $check.Message
            }
        }
    }

    if ($rightStickFieldsReady) {
        $maxOutput = 0
        $deadband = 0
        if ([int]::TryParse($script:tuningFields.MaxOutput.Text, [ref]$maxOutput) -and
            [int]::TryParse($script:tuningFields.Deadband.Text, [ref]$deadband)) {
            $deadbandOk = ($deadband -ge 0 -and $deadband -lt $maxOutput)
            Set-V3TuningFieldValid -Name 'Deadband' -Valid $deadbandOk
            if (-not $deadbandOk) {
                $ok = $false
                if ([string]::IsNullOrWhiteSpace($firstMessage)) {
                    $firstMessage = 'deadband must be 0..max_output-1'
                }
            }
        }
    }

    if ($leftStickFieldsReady) {
        if ($script:tuningChecks.MouseLeftEnabled.Checked -and $script:tuningChecks.KeyboardEnabled.Checked) {
            $ok = $false
            if ([string]::IsNullOrWhiteSpace($firstMessage)) {
                $firstMessage = 'use one left-stick source'
            }
        }

        if ($script:tuningChecks.MouseLeftEnabled.Checked -and
            $script:tuningChecks.MouseLeftRequireDevice.Checked -and
            [string]::IsNullOrWhiteSpace($script:tuningFields.MouseDeviceLeft.Text)) {
            Set-V3TuningFieldValid -Name 'MouseDeviceLeft' -Valid $false
            $ok = $false
            if ([string]::IsNullOrWhiteSpace($firstMessage)) {
                $firstMessage = 'left mouse token is required'
            }
        }

        $analogMin = 0.0
        $analogMax = 0.0
        $culture = [System.Globalization.CultureInfo]::InvariantCulture
        $style = [System.Globalization.NumberStyles]::Float
        if ([double]::TryParse($script:tuningFields.KeyboardAnalogMin.Text, $style, $culture, [ref]$analogMin) -and
            [double]::TryParse($script:tuningFields.KeyboardAnalogMax.Text, $style, $culture, [ref]$analogMax)) {
            $analogRangeOk = ($analogMin -ge 0.0 -and $analogMin -lt $analogMax -and $analogMax -le 1.0)
            Set-V3TuningFieldValid -Name 'KeyboardAnalogMin' -Valid $analogRangeOk
            Set-V3TuningFieldValid -Name 'KeyboardAnalogMax' -Valid $analogRangeOk
            if (-not $analogRangeOk) {
                $ok = $false
                if ([string]::IsNullOrWhiteSpace($firstMessage)) {
                    $firstMessage = 'analog min/max must satisfy 0 <= min < max <= 1'
                }
            }
        }
    }

    if (-not $ok -and $null -ne $script:statusText) {
        $script:statusText.Text = "Fix profile: $firstMessage."
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

    try {
        $profileName = $script:tuningFields.ProfileName.Text.Trim()
        $controlMode = if ($script:tuningChecks.WarThunderMode.Checked) { 'drone_mouse_aim' } else { 'direct_mouse' }
        $lines = [string[]][System.IO.File]::ReadAllLines($profile.FullName)
        $lines = Set-TomlValue -Lines $lines -Section 'trainer' -Key 'name' -Value (ConvertTo-V3TomlString $profileName)
        $lines = Set-TomlValue -Lines $lines -Section 'trainer' -Key 'frame_rate_hz' -Value $script:tuningFields.FrameRate.Text.Trim()
        $lines = Set-TomlValue -Lines $lines -Section 'safety' -Key 'stop_key' -Value (ConvertTo-V3TomlString $script:tuningFields.StopKey.Text.Trim())
        $lines = Set-TomlValue -Lines $lines -Section 'safety' -Key 'freeze_key' -Value (ConvertTo-V3TomlString $script:tuningFields.FreezeKey.Text.Trim())
        $lines = Set-TomlValue -Lines $lines -Section 'control' -Key 'mode' -Value (ConvertTo-V3TomlString $controlMode)
        if ($script:tuningFields.ContainsKey('RollGain')) {
            $lines = Set-TomlValue -Lines $lines -Section 'mapper' -Key 'roll_gain' -Value $script:tuningFields.RollGain.Text.Trim()
            $lines = Set-TomlValue -Lines $lines -Section 'mapper' -Key 'pitch_gain' -Value $script:tuningFields.PitchGain.Text.Trim()
            $lines = Set-TomlValue -Lines $lines -Section 'mapper' -Key 'max_output' -Value $script:tuningFields.MaxOutput.Text.Trim()
            $lines = Set-TomlValue -Lines $lines -Section 'mapper' -Key 'deadband' -Value $script:tuningFields.Deadband.Text.Trim()
            $lines = Set-TomlValue -Lines $lines -Section 'mapper' -Key 'output_curve' -Value (ConvertTo-V3TomlString $script:tuningFields.OutputCurve.Text.Trim())
            $lines = Set-TomlValue -Lines $lines -Section 'mapper' -Key 'expo' -Value $script:tuningFields.Expo.Text.Trim()
            $lines = Set-TomlValue -Lines $lines -Section 'mapper' -Key 'return_enabled' -Value (ConvertTo-TomlBool $script:tuningChecks.ReturnEnabled.Checked)
            $lines = Set-TomlValue -Lines $lines -Section 'mapper' -Key 'return_rate' -Value $script:tuningFields.ReturnRate.Text.Trim()
            $lines = Set-TomlValue -Lines $lines -Section 'mapper' -Key 'return_idle_ms' -Value $script:tuningFields.ReturnIdle.Text.Trim()
            $lines = Set-TomlValue -Lines $lines -Section 'mapper' -Key 'constant_return_enabled' -Value (ConvertTo-TomlBool $script:tuningChecks.ConstantReturnEnabled.Checked)
            $lines = Set-TomlValue -Lines $lines -Section 'mapper' -Key 'constant_return_rate' -Value $script:tuningFields.ConstantReturnRate.Text.Trim()
            $lines = Set-TomlValue -Lines $lines -Section 'mapper' -Key 'elastic_return_enabled' -Value (ConvertTo-TomlBool $script:tuningChecks.ElasticReturnEnabled.Checked)
            $lines = Set-TomlValue -Lines $lines -Section 'mapper' -Key 'elastic_return_mode' -Value (ConvertTo-V3TomlString $script:tuningFields.ElasticReturnMode.Text.Trim())
            $lines = Set-TomlValue -Lines $lines -Section 'mapper' -Key 'elastic_return_coefficient' -Value $script:tuningFields.ElasticReturnCoefficient.Text.Trim()
            $lines = Set-TomlValue -Lines $lines -Section 'mapper' -Key 'elastic_return_curve' -Value $script:tuningFields.ElasticReturnCurve.Text.Trim()
            if ($script:tuningFields.ContainsKey('InputFilter')) {
                $lines = Set-TomlValue -Lines $lines -Section 'mapper' -Key 'smoothing' -Value $script:tuningFields.Smoothing.Text.Trim()
                $lines = Set-TomlValue -Lines $lines -Section 'mapper' -Key 'input_filter' -Value (ConvertTo-V3TomlString $script:tuningFields.InputFilter.Text.Trim())
                $lines = Set-TomlValue -Lines $lines -Section 'mapper' -Key 'one_euro_min_cutoff_hz' -Value $script:tuningFields.OneEuroMinCutoffHz.Text.Trim()
                $lines = Set-TomlValue -Lines $lines -Section 'mapper' -Key 'one_euro_beta' -Value $script:tuningFields.OneEuroBeta.Text.Trim()
                $lines = Set-TomlValue -Lines $lines -Section 'mapper' -Key 'one_euro_dcutoff_hz' -Value $script:tuningFields.OneEuroDcutoffHz.Text.Trim()
                $lines = Set-TomlValue -Lines $lines -Section 'mapper' -Key 'despike_enabled' -Value (ConvertTo-TomlBool $script:tuningChecks.DespikeEnabled.Checked)
                $lines = Set-TomlValue -Lines $lines -Section 'mapper' -Key 'despike_count_enabled' -Value (ConvertTo-TomlBool $script:tuningChecks.DespikeCountEnabled.Checked)
                $lines = Set-TomlValue -Lines $lines -Section 'mapper' -Key 'despike_window' -Value $script:tuningFields.DespikeWindow.Text.Trim()
                $lines = Set-TomlValue -Lines $lines -Section 'mapper' -Key 'despike_threshold_sigma' -Value $script:tuningFields.DespikeThresholdSigma.Text.Trim()
                $lines = Set-TomlValue -Lines $lines -Section 'mapper' -Key 'actual_center' -Value $script:tuningFields.ActualCenter.Text.Trim()
                $lines = Set-TomlValue -Lines $lines -Section 'mapper' -Key 'actual_max' -Value $script:tuningFields.ActualMax.Text.Trim()
                $lines = Set-TomlValue -Lines $lines -Section 'mapper' -Key 'actual_expo' -Value $script:tuningFields.ActualExpo.Text.Trim()
                $lines = Set-TomlValue -Lines $lines -Section 'mapper' -Key 'position_model' -Value (ConvertTo-V3TomlString $script:tuningFields.PositionModel.Text.Trim())
                $lines = Set-TomlValue -Lines $lines -Section 'mapper' -Key 'gimbal_frequency_hz' -Value $script:tuningFields.GimbalFrequencyHz.Text.Trim()
                $lines = Set-TomlValue -Lines $lines -Section 'mapper' -Key 'gimbal_damping_ratio' -Value $script:tuningFields.GimbalDampingRatio.Text.Trim()
                $lines = Set-TomlValue -Lines $lines -Section 'mapper' -Key 'gimbal_input_impulse' -Value $script:tuningFields.GimbalInputImpulse.Text.Trim()
                $lines = Set-TomlValue -Lines $lines -Section 'mapper' -Key 'gimbal_static_friction' -Value $script:tuningFields.GimbalStaticFriction.Text.Trim()
                $lines = Set-TomlValue -Lines $lines -Section 'mapper' -Key 'gimbal_dynamic_friction' -Value $script:tuningFields.GimbalDynamicFriction.Text.Trim()
                $lines = Set-TomlValue -Lines $lines -Section 'mapper' -Key 'gimbal_edge_bumper' -Value $script:tuningFields.GimbalEdgeBumper.Text.Trim()
                $lines = Set-TomlValue -Lines $lines -Section 'mapper' -Key 'gimbal_antiwindup_enabled' -Value (ConvertTo-TomlBool $script:tuningChecks.GimbalAntiwindupEnabled.Checked)
                $lines = Set-TomlValue -Lines $lines -Section 'mapper' -Key 'gimbal_antiwindup_start' -Value $script:tuningFields.GimbalAntiwindupStart.Text.Trim()
                $lines = Set-TomlValue -Lines $lines -Section 'mapper' -Key 'gimbal_antiwindup_min_gain' -Value $script:tuningFields.GimbalAntiwindupMinGain.Text.Trim()
                $lines = Set-TomlValue -Lines $lines -Section 'mapper' -Key 'input_gain_mode' -Value (ConvertTo-V3TomlString $script:tuningFields.InputGainMode.Text.Trim())
                $lines = Set-TomlValue -Lines $lines -Section 'mapper' -Key 'adaptive_slow_gain' -Value $script:tuningFields.AdaptiveSlowGain.Text.Trim()
                $lines = Set-TomlValue -Lines $lines -Section 'mapper' -Key 'adaptive_fast_gain' -Value $script:tuningFields.AdaptiveFastGain.Text.Trim()
                $lines = Set-TomlValue -Lines $lines -Section 'mapper' -Key 'adaptive_speed_low' -Value $script:tuningFields.AdaptiveSpeedLow.Text.Trim()
                $lines = Set-TomlValue -Lines $lines -Section 'mapper' -Key 'adaptive_speed_high' -Value $script:tuningFields.AdaptiveSpeedHigh.Text.Trim()
                $lines = Set-TomlValue -Lines $lines -Section 'mapper' -Key 'adaptive_curve' -Value $script:tuningFields.AdaptiveCurve.Text.Trim()
                $lines = Set-TomlValue -Lines $lines -Section 'mapper' -Key 'adaptive_tracker_ms' -Value $script:tuningFields.AdaptiveTrackerMs.Text.Trim()
                $lines = Set-TomlValue -Lines $lines -Section 'mapper' -Key 'gate_shape' -Value (ConvertTo-V3TomlString $script:tuningFields.GateShape.Text.Trim())
                $lines = Set-TomlValue -Lines $lines -Section 'mapper' -Key 'diagonal_scale' -Value $script:tuningFields.DiagonalScale.Text.Trim()
                $lines = Set-TomlValue -Lines $lines -Section 'mapper' -Key 'output_shaping_enabled' -Value (ConvertTo-TomlBool $script:tuningChecks.OutputShapingEnabled.Checked)
                $lines = Set-TomlValue -Lines $lines -Section 'mapper' -Key 'return_shaping_enabled' -Value (ConvertTo-TomlBool $script:tuningChecks.ReturnShapingEnabled.Checked)
                $lines = Set-TomlArrayValue -Lines $lines -Section 'mapper' -Key 'output_shape_nodes' -Value (ConvertTo-StickShapeNodesText -Nodes (Get-StickShapeEditorNodes -Name 'OutputShape'))
                $lines = Set-TomlArrayValue -Lines $lines -Section 'mapper' -Key 'return_shape_nodes' -Value (ConvertTo-StickShapeNodesText -Nodes (Get-StickShapeEditorNodes -Name 'ReturnShape'))
            }
            $lines = Set-TomlValue -Lines $lines -Section 'mapper' -Key 'invert_roll' -Value (ConvertTo-TomlBool $script:tuningChecks.InvertRoll.Checked)
            $lines = Set-TomlValue -Lines $lines -Section 'mapper' -Key 'invert_pitch' -Value (ConvertTo-TomlBool $script:tuningChecks.InvertPitch.Checked)
            $lines = Set-TomlValue -Lines $lines -Section 'mapper' -Key 'swap_axes' -Value (ConvertTo-TomlBool $script:tuningChecks.SwapAxes.Checked)
            $lines = Set-TomlValue -Lines $lines -Section 'mouse_right_stick' -Key 'enabled' -Value (ConvertTo-TomlBool $script:tuningChecks.MouseRightStickEnabled.Checked)
        }
        if ($script:tuningFields.ContainsKey('AimSensitivityX') -and $script:tuningChecks.WarThunderMode.Checked) {
            $lines = Set-TomlValue -Lines $lines -Section 'mouse_aim' -Key 'sensitivity_x' -Value $script:tuningFields.AimSensitivityX.Text.Trim()
            $lines = Set-TomlValue -Lines $lines -Section 'mouse_aim' -Key 'sensitivity_y' -Value $script:tuningFields.AimSensitivityY.Text.Trim()
            $lines = Set-TomlValue -Lines $lines -Section 'mouse_aim' -Key 'reticle_limit' -Value $script:tuningFields.AimReticleLimit.Text.Trim()
            $lines = Set-TomlValue -Lines $lines -Section 'mouse_aim' -Key 'reticle_deadband' -Value $script:tuningFields.AimDeadband.Text.Trim()
            $lines = Set-TomlValue -Lines $lines -Section 'mouse_aim' -Key 'reticle_return_rate' -Value $script:tuningFields.AimReturnRate.Text.Trim()
            $lines = Set-TomlValue -Lines $lines -Section 'mouse_aim' -Key 'output_smoothing' -Value $script:tuningFields.AimSmoothing.Text.Trim()
            $lines = Set-TomlValue -Lines $lines -Section 'mouse_aim' -Key 'roll_gain' -Value $script:tuningFields.AimRollGain.Text.Trim()
            $lines = Set-TomlValue -Lines $lines -Section 'mouse_aim' -Key 'yaw_gain' -Value $script:tuningFields.AimYawGain.Text.Trim()
            $lines = Set-TomlValue -Lines $lines -Section 'mouse_aim' -Key 'pitch_gain' -Value $script:tuningFields.AimPitchGain.Text.Trim()
            $lines = Set-TomlValue -Lines $lines -Section 'mouse_aim' -Key 'roll_max' -Value $script:tuningFields.AimRollMax.Text.Trim()
            $lines = Set-TomlValue -Lines $lines -Section 'mouse_aim' -Key 'yaw_max' -Value $script:tuningFields.AimYawMax.Text.Trim()
            $lines = Set-TomlValue -Lines $lines -Section 'mouse_aim' -Key 'pitch_max' -Value $script:tuningFields.AimPitchMax.Text.Trim()
            $lines = Set-TomlValue -Lines $lines -Section 'mouse_aim' -Key 'slew_rate' -Value $script:tuningFields.AimSlewRate.Text.Trim()
            $lines = Set-TomlValue -Lines $lines -Section 'mouse_aim' -Key 'invert_x' -Value (ConvertTo-TomlBool $script:tuningChecks.AimInvertX.Checked)
            $lines = Set-TomlValue -Lines $lines -Section 'mouse_aim' -Key 'invert_y' -Value (ConvertTo-TomlBool $script:tuningChecks.AimInvertY.Checked)
        }
        if ($script:tuningFields.ContainsKey('ThrottleUpKey')) {
            $lines = Set-TomlValue -Lines $lines -Section 'keyboard_left_stick' -Key 'enabled' -Value (ConvertTo-TomlBool $script:tuningChecks.KeyboardEnabled.Checked)
            $lines = Set-TomlValue -Lines $lines -Section 'keyboard_left_stick' -Key 'input_source' -Value (ConvertTo-V3TomlString $script:tuningFields.KeyboardInputSource.Text.Trim())
            $lines = Set-TomlValue -Lines $lines -Section 'keyboard_left_stick' -Key 'require_analog' -Value (ConvertTo-TomlBool $script:tuningChecks.KeyboardRequireAnalog.Checked)
            $lines = Set-TomlValue -Lines $lines -Section 'keyboard_left_stick' -Key 'block_selected_keys' -Value (ConvertTo-TomlBool $script:tuningChecks.BlockSelectedKeys.Checked)
            $lines = Set-TomlValue -Lines $lines -Section 'keyboard_left_stick' -Key 'throttle_up_key' -Value (ConvertTo-V3TomlString $script:tuningFields.ThrottleUpKey.Text.Trim())
            $lines = Set-TomlValue -Lines $lines -Section 'keyboard_left_stick' -Key 'throttle_down_key' -Value (ConvertTo-V3TomlString $script:tuningFields.ThrottleDownKey.Text.Trim())
            $lines = Set-TomlValue -Lines $lines -Section 'keyboard_left_stick' -Key 'throttle_cut_key' -Value (ConvertTo-V3TomlString $script:tuningFields.ThrottleCutKey.Text.Trim())
            $lines = Set-TomlValue -Lines $lines -Section 'keyboard_left_stick' -Key 'throttle_rate' -Value $script:tuningFields.ThrottleRate.Text.Trim()
            $lines = Set-TomlValue -Lines $lines -Section 'keyboard_left_stick' -Key 'throttle_return_enabled' -Value (ConvertTo-TomlBool $script:tuningChecks.ThrottleReturnEnabled.Checked)
            $lines = Set-TomlValue -Lines $lines -Section 'keyboard_left_stick' -Key 'throttle_return_rate' -Value $script:tuningFields.ThrottleReturnRate.Text.Trim()
            $lines = Set-TomlValue -Lines $lines -Section 'keyboard_left_stick' -Key 'yaw_left_key' -Value (ConvertTo-V3TomlString $script:tuningFields.YawLeftKey.Text.Trim())
            $lines = Set-TomlValue -Lines $lines -Section 'keyboard_left_stick' -Key 'yaw_right_key' -Value (ConvertTo-V3TomlString $script:tuningFields.YawRightKey.Text.Trim())
            $lines = Set-TomlValue -Lines $lines -Section 'keyboard_left_stick' -Key 'yaw_pulse' -Value $script:tuningFields.YawPulse.Text.Trim()
            $lines = Set-TomlValue -Lines $lines -Section 'keyboard_left_stick' -Key 'yaw_slew_rate' -Value $script:tuningFields.YawSlewRate.Text.Trim()
            $lines = Set-TomlValue -Lines $lines -Section 'keyboard_left_stick' -Key 'invert_yaw' -Value (ConvertTo-TomlBool $script:tuningChecks.InvertYaw.Checked)
            $lines = Set-TomlValue -Lines $lines -Section 'keyboard_left_stick' -Key 'analog_keycode_mode' -Value (ConvertTo-V3TomlString $script:tuningFields.KeyboardAnalogKeycodeMode.Text.Trim())
            $lines = Set-TomlValue -Lines $lines -Section 'keyboard_left_stick' -Key 'analog_deadzone' -Value $script:tuningFields.KeyboardAnalogDeadzone.Text.Trim()
            $lines = Set-TomlValue -Lines $lines -Section 'keyboard_left_stick' -Key 'analog_curve' -Value $script:tuningFields.KeyboardAnalogCurve.Text.Trim()
            $lines = Set-TomlValue -Lines $lines -Section 'keyboard_left_stick' -Key 'analog_min' -Value $script:tuningFields.KeyboardAnalogMin.Text.Trim()
            $lines = Set-TomlValue -Lines $lines -Section 'keyboard_left_stick' -Key 'analog_max' -Value $script:tuningFields.KeyboardAnalogMax.Text.Trim()

            $lines = Set-TomlValue -Lines $lines -Section 'mouse_devices' -Key 'right' -Value (ConvertTo-V3TomlString $script:tuningFields.MouseDeviceRight.Text.Trim())
            $lines = Set-TomlValue -Lines $lines -Section 'mouse_devices' -Key 'left' -Value (ConvertTo-V3TomlString $script:tuningFields.MouseDeviceLeft.Text.Trim())
            $lines = Set-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'enabled' -Value (ConvertTo-TomlBool $script:tuningChecks.MouseLeftEnabled.Checked)
            $lines = Set-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'require_device' -Value (ConvertTo-TomlBool $script:tuningChecks.MouseLeftRequireDevice.Checked)
            $lines = Set-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'throttle_rate' -Value $script:tuningFields.MouseLeftThrottleRate.Text.Trim()
            $lines = Set-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'throttle_return_enabled' -Value (ConvertTo-TomlBool $script:tuningChecks.MouseLeftThrottleReturnEnabled.Checked)
            $lines = Set-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'throttle_return_rate' -Value $script:tuningFields.MouseLeftThrottleReturnRate.Text.Trim()
            $lines = Set-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_gain' -Value $script:tuningFields.MouseLeftYawGain.Text.Trim()
            $lines = Set-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_pulse' -Value $script:tuningFields.MouseLeftYawMax.Text.Trim()
            $lines = Set-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_deadband' -Value $script:tuningFields.MouseLeftYawDeadband.Text.Trim()
            $lines = Set-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_smoothing' -Value $script:tuningFields.MouseLeftYawSmoothing.Text.Trim()
            $lines = Set-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_slew_rate' -Value $script:tuningFields.MouseLeftYawSlewRate.Text.Trim()
            $lines = Set-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_return_enabled' -Value (ConvertTo-TomlBool $script:tuningChecks.MouseLeftYawReturnEnabled.Checked)
            $lines = Set-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_return_rate' -Value $script:tuningFields.MouseLeftYawReturnRate.Text.Trim()
            $lines = Set-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_return_idle_ms' -Value $script:tuningFields.MouseLeftYawReturnIdle.Text.Trim()
            $lines = Set-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_constant_return_enabled' -Value (ConvertTo-TomlBool $script:tuningChecks.MouseLeftYawConstantReturnEnabled.Checked)
            $lines = Set-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_constant_return_rate' -Value $script:tuningFields.MouseLeftYawConstantReturnRate.Text.Trim()
            $lines = Set-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_elastic_return_enabled' -Value (ConvertTo-TomlBool $script:tuningChecks.MouseLeftYawElasticReturnEnabled.Checked)
            $lines = Set-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_elastic_return_mode' -Value (ConvertTo-V3TomlString $script:tuningFields.MouseLeftYawElasticReturnMode.Text.Trim())
            $lines = Set-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_elastic_return_coefficient' -Value $script:tuningFields.MouseLeftYawElasticReturnCoefficient.Text.Trim()
            $lines = Set-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_elastic_return_curve' -Value $script:tuningFields.MouseLeftYawElasticReturnCurve.Text.Trim()
            if ($script:tuningFields.ContainsKey('MouseLeftYawInputFilter')) {
                $lines = Set-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_shaping_enabled' -Value (ConvertTo-TomlBool $script:tuningChecks.MouseLeftYawShapingEnabled.Checked)
                $lines = Set-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_input_filter' -Value (ConvertTo-V3TomlString $script:tuningFields.MouseLeftYawInputFilter.Text.Trim())
                $lines = Set-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_one_euro_min_cutoff_hz' -Value $script:tuningFields.MouseLeftYawOneEuroMinCutoffHz.Text.Trim()
                $lines = Set-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_one_euro_beta' -Value $script:tuningFields.MouseLeftYawOneEuroBeta.Text.Trim()
                $lines = Set-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_one_euro_dcutoff_hz' -Value $script:tuningFields.MouseLeftYawOneEuroDcutoffHz.Text.Trim()
                $lines = Set-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_despike_enabled' -Value (ConvertTo-TomlBool $script:tuningChecks.MouseLeftYawDespikeEnabled.Checked)
                $lines = Set-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_despike_count_enabled' -Value (ConvertTo-TomlBool $script:tuningChecks.MouseLeftYawDespikeCountEnabled.Checked)
                $lines = Set-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_despike_window' -Value $script:tuningFields.MouseLeftYawDespikeWindow.Text.Trim()
                $lines = Set-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_despike_threshold_sigma' -Value $script:tuningFields.MouseLeftYawDespikeThresholdSigma.Text.Trim()
                $lines = Set-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_output_curve' -Value (ConvertTo-V3TomlString $script:tuningFields.MouseLeftYawOutputCurve.Text.Trim())
                $lines = Set-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_expo' -Value $script:tuningFields.MouseLeftYawExpo.Text.Trim()
                $lines = Set-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_actual_center' -Value $script:tuningFields.MouseLeftYawActualCenter.Text.Trim()
                $lines = Set-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_actual_max' -Value $script:tuningFields.MouseLeftYawActualMax.Text.Trim()
                $lines = Set-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_actual_expo' -Value $script:tuningFields.MouseLeftYawActualExpo.Text.Trim()
                $lines = Set-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_position_model' -Value (ConvertTo-V3TomlString $script:tuningFields.MouseLeftYawPositionModel.Text.Trim())
                $lines = Set-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_gimbal_frequency_hz' -Value $script:tuningFields.MouseLeftYawGimbalFrequencyHz.Text.Trim()
                $lines = Set-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_gimbal_damping_ratio' -Value $script:tuningFields.MouseLeftYawGimbalDampingRatio.Text.Trim()
                $lines = Set-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_gimbal_input_impulse' -Value $script:tuningFields.MouseLeftYawGimbalInputImpulse.Text.Trim()
                $lines = Set-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_gimbal_static_friction' -Value $script:tuningFields.MouseLeftYawGimbalStaticFriction.Text.Trim()
                $lines = Set-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_gimbal_dynamic_friction' -Value $script:tuningFields.MouseLeftYawGimbalDynamicFriction.Text.Trim()
                $lines = Set-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_gimbal_edge_bumper' -Value $script:tuningFields.MouseLeftYawGimbalEdgeBumper.Text.Trim()
                $lines = Set-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_gimbal_antiwindup_enabled' -Value (ConvertTo-TomlBool $script:tuningChecks.MouseLeftYawGimbalAntiwindupEnabled.Checked)
                $lines = Set-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_gimbal_antiwindup_start' -Value $script:tuningFields.MouseLeftYawGimbalAntiwindupStart.Text.Trim()
                $lines = Set-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_gimbal_antiwindup_min_gain' -Value $script:tuningFields.MouseLeftYawGimbalAntiwindupMinGain.Text.Trim()
                $lines = Set-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_input_gain_mode' -Value (ConvertTo-V3TomlString $script:tuningFields.MouseLeftYawInputGainMode.Text.Trim())
                $lines = Set-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_adaptive_slow_gain' -Value $script:tuningFields.MouseLeftYawAdaptiveSlowGain.Text.Trim()
                $lines = Set-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_adaptive_fast_gain' -Value $script:tuningFields.MouseLeftYawAdaptiveFastGain.Text.Trim()
                $lines = Set-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_adaptive_speed_low' -Value $script:tuningFields.MouseLeftYawAdaptiveSpeedLow.Text.Trim()
                $lines = Set-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_adaptive_speed_high' -Value $script:tuningFields.MouseLeftYawAdaptiveSpeedHigh.Text.Trim()
                $lines = Set-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_adaptive_curve' -Value $script:tuningFields.MouseLeftYawAdaptiveCurve.Text.Trim()
                $lines = Set-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_adaptive_tracker_ms' -Value $script:tuningFields.MouseLeftYawAdaptiveTrackerMs.Text.Trim()
                $lines = Set-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_gate_shape' -Value (ConvertTo-V3TomlString $script:tuningFields.MouseLeftYawGateShape.Text.Trim())
                $lines = Set-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_diagonal_scale' -Value $script:tuningFields.MouseLeftYawDiagonalScale.Text.Trim()
                $lines = Set-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_output_shaping_enabled' -Value (ConvertTo-TomlBool $script:tuningChecks.MouseLeftYawOutputShapingEnabled.Checked)
                $lines = Set-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_return_shaping_enabled' -Value (ConvertTo-TomlBool $script:tuningChecks.MouseLeftYawReturnShapingEnabled.Checked)
                $lines = Set-TomlArrayValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_output_shape_nodes' -Value (ConvertTo-StickShapeNodesText -Nodes (Get-StickShapeEditorNodes -Name 'MouseLeftYawOutputShape'))
                $lines = Set-TomlArrayValue -Lines $lines -Section 'mouse_left_stick' -Key 'yaw_return_shape_nodes' -Value (ConvertTo-StickShapeNodesText -Nodes (Get-StickShapeEditorNodes -Name 'MouseLeftYawReturnShape'))
            }
            $lines = Set-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'invert_throttle' -Value (ConvertTo-TomlBool $script:tuningChecks.MouseLeftInvertThrottle.Checked)
            $lines = Set-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'invert_yaw' -Value (ConvertTo-TomlBool $script:tuningChecks.MouseLeftInvertYaw.Checked)
            $lines = Set-TomlValue -Lines $lines -Section 'mouse_left_stick' -Key 'swap_axes' -Value (ConvertTo-TomlBool $script:tuningChecks.MouseLeftSwapAxes.Checked)
        }
        [System.IO.File]::WriteAllLines($profile.FullName, [string[]]$lines, [System.Text.Encoding]::ASCII)
        if ($null -ne $script:pendingTuningNames) {
            $script:pendingTuningNames.Clear()
        }

        $profile.Name = $profileName
        $profile.FrameRate = $script:tuningFields.FrameRate.Text.Trim()
        $profile.StopKey = $script:tuningFields.StopKey.Text.Trim()
        $profile.FreezeKey = $script:tuningFields.FreezeKey.Text.Trim()
        $profile.ControlMode = $controlMode
        if ($script:tuningFields.ContainsKey('RollGain')) {
            $profile.RollGain = $script:tuningFields.RollGain.Text.Trim()
            $profile.PitchGain = $script:tuningFields.PitchGain.Text.Trim()
            $profile.MaxOutput = $script:tuningFields.MaxOutput.Text.Trim()
            $profile.Deadband = $script:tuningFields.Deadband.Text.Trim()
            $profile.OutputCurve = $script:tuningFields.OutputCurve.Text.Trim()
            $profile.Expo = $script:tuningFields.Expo.Text.Trim()
            $profile.ReturnEnabled = ConvertTo-TomlBool $script:tuningChecks.ReturnEnabled.Checked
            $profile.ReturnRate = $script:tuningFields.ReturnRate.Text.Trim()
            $profile.ReturnIdle = $script:tuningFields.ReturnIdle.Text.Trim()
            $profile.ConstantReturnEnabled = ConvertTo-TomlBool $script:tuningChecks.ConstantReturnEnabled.Checked
            $profile.ConstantReturnRate = $script:tuningFields.ConstantReturnRate.Text.Trim()
            $profile.ElasticReturnEnabled = ConvertTo-TomlBool $script:tuningChecks.ElasticReturnEnabled.Checked
            $profile.ElasticReturnMode = $script:tuningFields.ElasticReturnMode.Text.Trim()
            $profile.ElasticReturnCoefficient = $script:tuningFields.ElasticReturnCoefficient.Text.Trim()
            $profile.ElasticReturnCurve = $script:tuningFields.ElasticReturnCurve.Text.Trim()
            $profile.InvertRoll = ConvertTo-TomlBool $script:tuningChecks.InvertRoll.Checked
            $profile.InvertPitch = ConvertTo-TomlBool $script:tuningChecks.InvertPitch.Checked
            $profile.SwapAxes = ConvertTo-TomlBool $script:tuningChecks.SwapAxes.Checked
            $profile.MouseRightStickEnabled = ConvertTo-TomlBool $script:tuningChecks.MouseRightStickEnabled.Checked
            if ($script:tuningFields.ContainsKey('InputFilter')) {
                $profile.Smoothing = $script:tuningFields.Smoothing.Text.Trim()
                $profile.InputFilter = $script:tuningFields.InputFilter.Text.Trim()
                $profile.OneEuroMinCutoffHz = $script:tuningFields.OneEuroMinCutoffHz.Text.Trim()
                $profile.OneEuroBeta = $script:tuningFields.OneEuroBeta.Text.Trim()
                $profile.OneEuroDcutoffHz = $script:tuningFields.OneEuroDcutoffHz.Text.Trim()
                $profile.DespikeEnabled = ConvertTo-TomlBool $script:tuningChecks.DespikeEnabled.Checked
                $profile.DespikeCountEnabled = ConvertTo-TomlBool $script:tuningChecks.DespikeCountEnabled.Checked
                $profile.DespikeWindow = $script:tuningFields.DespikeWindow.Text.Trim()
                $profile.DespikeThresholdSigma = $script:tuningFields.DespikeThresholdSigma.Text.Trim()
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
                $profile.OutputShapingEnabled = ConvertTo-TomlBool $script:tuningChecks.OutputShapingEnabled.Checked
                $profile.OutputShapeNodesText = ConvertTo-StickShapeNodesText -Nodes (Get-StickShapeEditorNodes -Name 'OutputShape')
                $profile.ReturnShapingEnabled = ConvertTo-TomlBool $script:tuningChecks.ReturnShapingEnabled.Checked
                $profile.ReturnShapeNodesText = ConvertTo-StickShapeNodesText -Nodes (Get-StickShapeEditorNodes -Name 'ReturnShape')
            }
        }
        if ($script:tuningFields.ContainsKey('AimSensitivityX')) {
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
        }
        if ($script:tuningFields.ContainsKey('ThrottleUpKey')) {
            $profile.KeyboardEnabled = ConvertTo-TomlBool $script:tuningChecks.KeyboardEnabled.Checked
            $profile.KeyboardInputSource = $script:tuningFields.KeyboardInputSource.Text.Trim()
            $profile.KeyboardRequireAnalog = ConvertTo-TomlBool $script:tuningChecks.KeyboardRequireAnalog.Checked
            $profile.BlockSelectedKeys = ConvertTo-TomlBool $script:tuningChecks.BlockSelectedKeys.Checked
            $profile.ThrottleUpKey = $script:tuningFields.ThrottleUpKey.Text.Trim()
            $profile.ThrottleDownKey = $script:tuningFields.ThrottleDownKey.Text.Trim()
            $profile.ThrottleCutKey = $script:tuningFields.ThrottleCutKey.Text.Trim()
            $profile.ThrottleRate = $script:tuningFields.ThrottleRate.Text.Trim()
            $profile.ThrottleReturnEnabled = ConvertTo-TomlBool $script:tuningChecks.ThrottleReturnEnabled.Checked
            $profile.ThrottleReturnRate = $script:tuningFields.ThrottleReturnRate.Text.Trim()
            $profile.YawLeftKey = $script:tuningFields.YawLeftKey.Text.Trim()
            $profile.YawRightKey = $script:tuningFields.YawRightKey.Text.Trim()
            $profile.YawPulse = $script:tuningFields.YawPulse.Text.Trim()
            $profile.YawSlewRate = $script:tuningFields.YawSlewRate.Text.Trim()
            $profile.InvertYaw = ConvertTo-TomlBool $script:tuningChecks.InvertYaw.Checked
            $profile.KeyboardAnalogKeycodeMode = $script:tuningFields.KeyboardAnalogKeycodeMode.Text.Trim()
            $profile.KeyboardAnalogDeadzone = $script:tuningFields.KeyboardAnalogDeadzone.Text.Trim()
            $profile.KeyboardAnalogCurve = $script:tuningFields.KeyboardAnalogCurve.Text.Trim()
            $profile.KeyboardAnalogMin = $script:tuningFields.KeyboardAnalogMin.Text.Trim()
            $profile.KeyboardAnalogMax = $script:tuningFields.KeyboardAnalogMax.Text.Trim()

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
            $profile.MouseLeftYawReturnEnabled = ConvertTo-TomlBool $script:tuningChecks.MouseLeftYawReturnEnabled.Checked
            $profile.MouseLeftYawReturnRate = $script:tuningFields.MouseLeftYawReturnRate.Text.Trim()
            $profile.MouseLeftYawReturnIdle = $script:tuningFields.MouseLeftYawReturnIdle.Text.Trim()
            $profile.MouseLeftYawConstantReturnEnabled = ConvertTo-TomlBool $script:tuningChecks.MouseLeftYawConstantReturnEnabled.Checked
            $profile.MouseLeftYawConstantReturnRate = $script:tuningFields.MouseLeftYawConstantReturnRate.Text.Trim()
            $profile.MouseLeftYawElasticReturnEnabled = ConvertTo-TomlBool $script:tuningChecks.MouseLeftYawElasticReturnEnabled.Checked
            $profile.MouseLeftYawElasticReturnMode = $script:tuningFields.MouseLeftYawElasticReturnMode.Text.Trim()
            $profile.MouseLeftYawElasticReturnCoefficient = $script:tuningFields.MouseLeftYawElasticReturnCoefficient.Text.Trim()
            $profile.MouseLeftYawElasticReturnCurve = $script:tuningFields.MouseLeftYawElasticReturnCurve.Text.Trim()
            if ($script:tuningFields.ContainsKey('MouseLeftYawInputFilter')) {
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
                $profile.MouseLeftYawOutputShapingEnabled = ConvertTo-TomlBool $script:tuningChecks.MouseLeftYawOutputShapingEnabled.Checked
                $profile.MouseLeftYawOutputShapeNodesText = ConvertTo-StickShapeNodesText -Nodes (Get-StickShapeEditorNodes -Name 'MouseLeftYawOutputShape')
                $profile.MouseLeftYawReturnShapingEnabled = ConvertTo-TomlBool $script:tuningChecks.MouseLeftYawReturnShapingEnabled.Checked
                $profile.MouseLeftYawReturnShapeNodesText = ConvertTo-StickShapeNodesText -Nodes (Get-StickShapeEditorNodes -Name 'MouseLeftYawReturnShape')
            }
            $profile.MouseLeftInvertThrottle = ConvertTo-TomlBool $script:tuningChecks.MouseLeftInvertThrottle.Checked
            $profile.MouseLeftInvertYaw = ConvertTo-TomlBool $script:tuningChecks.MouseLeftInvertYaw.Checked
            $profile.MouseLeftSwapAxes = ConvertTo-TomlBool $script:tuningChecks.MouseLeftSwapAxes.Checked
        }
        $profile.Display = $(if ($profile.FileName -eq (Get-DefaultProfileFileName)) { "$profileName [default]" } else { $profileName })

        $defaultSuffix = if ($profile.FileName -eq (Get-DefaultProfileFileName)) { ' [default]' } else { '' }
        $script:profileTitle.Text = "$profileName$defaultSuffix"
        $script:profileFile.Text = $profile.FileName
        $script:profilePath.Text = $profile.FullName
        if ($null -ne $script:profilesList) {
            $script:profilesList.Refresh()
        }
        if ($null -ne $script:statusText) {
            $script:statusText.Text = "Saved profile to $($profile.FileName)."
        }
    } catch {
        Write-LauncherError -Context 'Save profile' -Exception $_.Exception
    }
}

function Save-FastBoolTuningChanges {
    param([string[]]$Names)

    if ($null -eq $script:editingProfile -or $null -eq $Names -or $Names.Count -eq 0) {
        return $false
    }

    $uniqueNames = @($Names | Select-Object -Unique)
    foreach ($name in $uniqueNames) {
        if (-not $script:fastBoolSaveMap.ContainsKey($name) -or -not $script:tuningChecks.ContainsKey($name)) {
            return $false
        }
    }

    try {
        $lines = [string[]][System.IO.File]::ReadAllLines($script:editingProfile.FullName)
        foreach ($name in $uniqueNames) {
            $entry = $script:fastBoolSaveMap[$name]
            $value = ConvertTo-TomlBool $script:tuningChecks[$name].Checked
            $lines = Set-TomlValue -Lines $lines -Section $entry.Section -Key $entry.Key -Value $value
        }
        [System.IO.File]::WriteAllLines($script:editingProfile.FullName, [string[]]$lines, [System.Text.Encoding]::ASCII)

        foreach ($name in $uniqueNames) {
            $entry = $script:fastBoolSaveMap[$name]
            $value = ConvertTo-TomlBool $script:tuningChecks[$name].Checked
            if ($script:editingProfile.PSObject.Properties[$entry.Property]) {
                $script:editingProfile.PSObject.Properties[$entry.Property].Value = $value
            }
        }
        if ($null -ne $script:statusText) {
            $script:statusText.Text = "Saved profile to $($script:editingProfile.FileName)."
        }
        return $true
    } catch {
        Write-LauncherError -Context 'Fast save profile' -Exception $_.Exception
        return $false
    }
}

function Save-PendingTuningChanges {
    $names = @()
    if ($null -ne $script:pendingTuningNames -and $script:pendingTuningNames.Count -gt 0) {
        $names = @($script:pendingTuningNames)
        $script:pendingTuningNames.Clear()
    }

    if ($names.Count -gt 0 -and (Save-FastBoolTuningChanges -Names $names)) {
        return
    }
    Save-SelectedProfile
}

function Queue-TuningSave {
    param([string]$ChangedName = '')

    if ($script:loadingProfile) {
        return
    }
    if ($null -eq $script:saveTimer) {
        return
    }
    if (-not [string]::IsNullOrWhiteSpace($ChangedName)) {
        [void]$script:pendingTuningNames.Add($ChangedName)
    } elseif ($null -ne $script:pendingTuningNames) {
        $script:pendingTuningNames.Clear()
    }
    $script:saveTimer.Stop()
    $script:saveTimer.Start()
}

function Flush-PendingTuningSave {
    if ($null -eq $script:saveTimer) {
        return
    }
    if ($script:saveTimer.Enabled) {
        $script:saveTimer.Stop()
        Save-PendingTuningChanges
    }
}

# --- Profile list owner-draw ---
function Draw-V3ProfileItem {
    param(
        [System.Windows.Forms.DrawItemEventArgs]$EventArgs
    )

    if ($EventArgs.Index -lt 0) {
        return
    }

    try {
        $profile = $script:profilesList.Items[$EventArgs.Index]
        $g = $EventArgs.Graphics
        $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
        $bounds = $EventArgs.Bounds
        $selected = (($EventArgs.State -band [System.Windows.Forms.DrawItemState]::Selected) -eq [System.Windows.Forms.DrawItemState]::Selected)
        $isDefault = ($profile.FileName -eq (Get-DefaultProfileFileName))

        $clearBrush = New-Object System.Drawing.SolidBrush($script:dark.BgSurface)
        try { $g.FillRectangle($clearBrush, $bounds) } finally { $clearBrush.Dispose() }

        $outer = New-Object System.Drawing.Rectangle(($bounds.X + 6), ($bounds.Y + 3), ([Math]::Max(1, $bounds.Width - 12)), ([Math]::Max(1, $bounds.Height - 6)))
        $bgColor = if ($selected) { $script:dark.AccentSoft } else { $script:dark.BgElev }
        $borderColor = if ($selected) { $script:dark.Accent } else { $script:dark.Border }

        $path = [Gx12LauncherV3.DarkPanel]::RoundedRect($outer, 7)
        try {
            $brush = New-Object System.Drawing.SolidBrush($bgColor)
            $pen = New-Object System.Drawing.Pen($borderColor, 1)
            try {
                $g.FillPath($brush, $path)
                $g.DrawPath($pen, $path)
            } finally {
                $brush.Dispose()
                $pen.Dispose()
            }
        } finally {
            $path.Dispose()
        }

        $iconColor = if ($selected) { $script:dark.Accent } else { $script:dark.TextMuted }
        $iconFont = New-IconFont -Size 11.0
        $iconBrush = New-Object System.Drawing.SolidBrush($iconColor)
        try {
            $iconRect = New-Object System.Drawing.RectangleF(([single]($outer.X + 12)), ([single]($outer.Y + 12)), 18.0, 18.0)
            $g.DrawString(([string][char]0xE8B0), $iconFont, $iconBrush, $iconRect)
        } finally {
            $iconFont.Dispose()
            $iconBrush.Dispose()
        }

        $nameColor = if ($selected) { $script:dark.Text } else { $script:dark.Text }
        $fileColor = $script:dark.TextMuted
        $nameRect = New-Object System.Drawing.Rectangle(($outer.X + 40), ($outer.Y + 6), ([Math]::Max(1, $outer.Width - 76)), 22)
        $fileRect = New-Object System.Drawing.Rectangle(($outer.X + 40), ($outer.Y + 28), ([Math]::Max(1, $outer.Width - 76)), 18)
        $nameFont = New-DarkFont -Size 10.0 -Style ([System.Drawing.FontStyle]::Bold)
        $fileFont = New-DarkFont -Size 8.4
        try {
            $textFlags = [System.Windows.Forms.TextFormatFlags]::Left -bor [System.Windows.Forms.TextFormatFlags]::VerticalCenter -bor [System.Windows.Forms.TextFormatFlags]::EndEllipsis
            [System.Windows.Forms.TextRenderer]::DrawText($g, [string]$profile.Name, $nameFont, $nameRect, $nameColor, $textFlags)
            [System.Windows.Forms.TextRenderer]::DrawText($g, [string]$profile.FileName, $fileFont, $fileRect, $fileColor, $textFlags)
        } finally {
            $nameFont.Dispose()
            $fileFont.Dispose()
        }

        if ($isDefault) {
            $checkFont = New-IconFont -Size 10.0
            $checkBrush = New-Object System.Drawing.SolidBrush($script:dark.Success)
            try {
                $g.DrawString(([string][char]0xE73E), $checkFont, $checkBrush, ([single]($outer.Right - 26)), ([single]($outer.Y + 14)))
            } finally {
                $checkFont.Dispose()
                $checkBrush.Dispose()
            }
        }
    } catch {
        if (-not $script:profileDrawErrorReported) {
            $script:profileDrawErrorReported = $true
            if ($null -ne $script:statusText) {
                $script:statusText.Text = "Profile list paint fallback active: $($_.Exception.Message)"
            }
        }
        $EventArgs.DrawBackground()
        $profile = $script:profilesList.Items[$EventArgs.Index]
        $fallbackText = if ($null -ne $profile) { [string]$profile.Display } else { '' }
        if ([string]::IsNullOrWhiteSpace($fallbackText) -and $null -ne $profile) {
            $fallbackText = [string]$profile.FileName
        }
        [System.Windows.Forms.TextRenderer]::DrawText(
            $EventArgs.Graphics,
            $fallbackText,
            $EventArgs.Font,
            $EventArgs.Bounds,
            $script:dark.Text,
            ([System.Windows.Forms.TextFormatFlags]::Left -bor [System.Windows.Forms.TextFormatFlags]::VerticalCenter -bor [System.Windows.Forms.TextFormatFlags]::EndEllipsis)
        )
    }
}

# --- Profile list / placeholder editor ---
function Get-SelectedProfileFileName {
    if ($null -ne $script:profilesList -and $null -ne $script:profilesList.SelectedItem) {
        return $script:profilesList.SelectedItem.FileName
    }
    return $null
}

function Apply-ProfileFilter {
    param([string]$TargetFileName = $null)

    if ($null -eq $script:profilesList) {
        return
    }

    $query = ''
    if ($null -ne $script:profileSearch) {
        $query = $script:profileSearch.Text.Trim()
    }

    $defaultFile = Get-DefaultProfileFileName
    $profiles = @($script:allProfiles)
    if (-not [string]::IsNullOrWhiteSpace($query)) {
        $profiles = @($profiles | Where-Object {
            $_.Name.IndexOf($query, [System.StringComparison]::OrdinalIgnoreCase) -ge 0 -or
            $_.FileName.IndexOf($query, [System.StringComparison]::OrdinalIgnoreCase) -ge 0
        })
    }

    $script:profilesList.BeginUpdate()
    try {
        $script:profilesList.Items.Clear()
        foreach ($profile in $profiles) {
            [void]$script:profilesList.Items.Add($profile)
        }
    } finally {
        $script:profilesList.EndUpdate()
    }

    if ($script:profilesList.Items.Count -eq 0) {
        Update-Details
        return
    }

    $wanted = if ($TargetFileName) { $TargetFileName } else { $defaultFile }
    $index = 0
    for ($i = 0; $i -lt $script:profilesList.Items.Count; ++$i) {
        if ($script:profilesList.Items[$i].FileName -eq $wanted) {
            $index = $i
            break
        }
    }
    $script:profilesList.SelectedIndex = $index
}

function Refresh-Profiles {
    $previousSelection = Get-SelectedProfileFileName
    $script:allProfiles = @(Load-Profiles)
    Apply-ProfileFilter -TargetFileName $previousSelection
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
        if ($dialog.ShowDialog($script:form) -ne [System.Windows.Forms.DialogResult]::OK) {
            return
        }
        $selected = Set-ProfileDirectory -Path $dialog.SelectedPath
        $script:profileSearch.Text = ''
        $script:editingProfile = $null
        $script:statusText.Text = "Profile folder set to $selected."
        Refresh-Profiles
    } finally {
        $dialog.Dispose()
    }
}

function New-ProfileCopy {
    if ($null -eq $script:profilesList.SelectedItem) {
        return
    }
    Flush-PendingTuningSave
    $profile = $script:profilesList.SelectedItem
    $base = [System.IO.Path]::GetFileNameWithoutExtension($profile.FileName)
    $candidate = ''
    for ($i = 2; $i -le 99; ++$i) {
        $candidate = ('{0}-v{1}.toml' -f $base, $i)
        $candidatePath = Join-Path $profilesDir $candidate
        if (-not (Test-Path -LiteralPath $candidatePath)) {
            break
        }
        $candidate = ''
    }
    if ([string]::IsNullOrWhiteSpace($candidate)) {
        [System.Windows.Forms.MessageBox]::Show('Could not find a free copy filename.', 'GX12 Launcher V3') | Out-Null
        return
    }
    $newPath = Join-Path $profilesDir $candidate
    Copy-Item -LiteralPath $profile.FullName -Destination $newPath
    $lines = [string[]](Get-Content -LiteralPath $newPath)
    $newName = [System.IO.Path]::GetFileNameWithoutExtension($candidate)
    $lines = Set-TomlValue -Lines $lines -Section 'trainer' -Key 'name' -Value ('"{0}"' -f $newName)
    Set-Content -LiteralPath $newPath -Value $lines -Encoding ASCII
    $script:statusText.Text = "Created $candidate from $($profile.FileName)."
    Refresh-Profiles
    Apply-ProfileFilter -TargetFileName $candidate
}

function Update-Details {
    if ((-not $script:loadingProfile) -and $null -ne $script:saveTimer -and $script:saveTimer.Enabled) {
        $script:saveTimer.Stop()
        Save-PendingTuningChanges
    }

    $profile = if ($null -ne $script:profilesList) { $script:profilesList.SelectedItem } else { $null }

    $script:loadingProfile = $true
    try {
    if ($null -eq $profile) {
        $script:editingProfile = $null
        $script:profileTitle.Text = 'No profile selected'
        $script:profileFile.Text = ''
        $script:profilePath.Text = ''
        foreach ($field in $script:tuningFields.Values) {
            if ($field -is [Gx12Launcher.StickShapeEditor]) {
                $field.ClearNodes()
                continue
            }
            if ($field.PSObject.Properties['Text']) {
                $field.Text = ''
            }
        }
        foreach ($check in $script:tuningChecks.Values) {
            $check.Checked = $false
        }
        if ($null -ne $script:defaultCheck) {
            $script:defaultCheck.Checked = $false
        }
        Set-OverviewControlsEnabled -Enabled $false
        foreach ($name in ($script:rightStickCoreFieldNames + $script:rightStickReturnFieldNames + $script:rightStickCoreCheckNames)) {
            Set-V3NamedControlEnabled -Name $name -Enabled $false
        }
        foreach ($name in ($script:rightStickAdvancedFieldNames + $script:rightStickAdvancedCheckNames + $script:droneAimFieldNames + $script:droneAimCheckNames)) {
            Set-V3NamedControlEnabled -Name $name -Enabled $false
        }
        foreach ($name in ($script:leftStickKeyboardFieldNames + $script:leftStickMouseFieldNames + $script:leftStickMouseAdvancedFieldNames + $script:leftStickKeyboardCheckNames + $script:leftStickMouseCheckNames + $script:leftStickMouseAdvancedCheckNames)) {
            Set-V3NamedControlEnabled -Name $name -Enabled $false
        }
        if ($null -ne $script:leftStickOffRadio) {
            $script:leftStickOffRadio.Enabled = $false
            $script:leftStickOffRadio.Checked = $false
        }
        if ($null -ne $script:statusText) {
            $script:statusText.Text = 'No profile matches the current filter.'
        }
        return
    }

    Set-OverviewControlsEnabled -Enabled $true
    $script:editingProfile = $profile
    $isDefault = ($profile.FileName -eq (Get-DefaultProfileFileName))
    $defaultSuffix = if ($isDefault) { ' [default]' } else { '' }
    $script:profileTitle.Text = "$($profile.Name)$defaultSuffix"
    $script:profileFile.Text = $profile.FileName
    $script:profilePath.Text = $profile.FullName

    $controlMode = if ($profile.PSObject.Properties['ControlMode']) { [string]$profile.ControlMode } else { '' }
    if ([string]::IsNullOrWhiteSpace($controlMode)) { $controlMode = 'direct_mouse' }
    $controlMode = $controlMode.Trim('"')
    $script:tuningFields.ProfileName.Text = [string]$profile.Name
    $script:tuningFields.FrameRate.Text = [string]$profile.FrameRate
    $script:tuningFields.StopKey.Text = [string]$profile.StopKey
    $script:tuningFields.FreezeKey.Text = [string]$profile.FreezeKey
    $script:tuningChecks.WarThunderMode.Checked = ($controlMode -eq 'drone_mouse_aim')
    if ($script:tuningFields.ContainsKey('RollGain')) {
        $script:tuningFields.RollGain.Text = [string]$profile.RollGain
        $script:tuningFields.PitchGain.Text = [string]$profile.PitchGain
        $script:tuningFields.MaxOutput.Text = [string]$profile.MaxOutput
        $script:tuningFields.Deadband.Text = [string]$profile.Deadband
        $script:tuningFields.OutputCurve.Text = [string]$profile.OutputCurve
        $script:tuningFields.Expo.Text = [string]$profile.Expo
        $script:tuningFields.ReturnRate.Text = [string]$profile.ReturnRate
        $script:tuningFields.ReturnIdle.Text = [string]$profile.ReturnIdle
        $script:tuningFields.ConstantReturnRate.Text = [string]$profile.ConstantReturnRate
        $script:tuningFields.ElasticReturnMode.Text = [string]$profile.ElasticReturnMode
        $script:tuningFields.ElasticReturnCoefficient.Text = [string]$profile.ElasticReturnCoefficient
        $script:tuningFields.ElasticReturnCurve.Text = [string]$profile.ElasticReturnCurve
        $script:tuningChecks.MouseRightStickEnabled.Checked = ((ConvertTo-TomlBool $profile.MouseRightStickEnabled) -eq 'true')
        $script:tuningChecks.InvertRoll.Checked = ((ConvertTo-TomlBool $profile.InvertRoll) -eq 'true')
        $script:tuningChecks.InvertPitch.Checked = ((ConvertTo-TomlBool $profile.InvertPitch) -eq 'true')
        $script:tuningChecks.SwapAxes.Checked = ((ConvertTo-TomlBool $profile.SwapAxes) -eq 'true')
        $script:tuningChecks.ReturnEnabled.Checked = ((ConvertTo-TomlBool $profile.ReturnEnabled) -eq 'true')
        $script:tuningChecks.ConstantReturnEnabled.Checked = ((ConvertTo-TomlBool $profile.ConstantReturnEnabled) -eq 'true')
        $script:tuningChecks.ElasticReturnEnabled.Checked = ((ConvertTo-TomlBool $profile.ElasticReturnEnabled) -eq 'true')
        if ($script:tuningFields.ContainsKey('InputFilter')) {
            $script:tuningFields.Smoothing.Text = [string]$profile.Smoothing
            $script:tuningFields.InputFilter.Text = [string]$profile.InputFilter
            $script:tuningFields.OneEuroMinCutoffHz.Text = [string]$profile.OneEuroMinCutoffHz
            $script:tuningFields.OneEuroBeta.Text = [string]$profile.OneEuroBeta
            $script:tuningFields.OneEuroDcutoffHz.Text = [string]$profile.OneEuroDcutoffHz
            $script:tuningFields.DespikeWindow.Text = [string]$profile.DespikeWindow
            $script:tuningFields.DespikeThresholdSigma.Text = [string]$profile.DespikeThresholdSigma
            $script:tuningFields.ActualCenter.Text = [string]$profile.ActualCenter
            $script:tuningFields.ActualMax.Text = [string]$profile.ActualMax
            $script:tuningFields.ActualExpo.Text = [string]$profile.ActualExpo
            $script:tuningFields.PositionModel.Text = [string]$profile.PositionModel
            $script:tuningFields.GimbalFrequencyHz.Text = [string]$profile.GimbalFrequencyHz
            $script:tuningFields.GimbalDampingRatio.Text = [string]$profile.GimbalDampingRatio
            $script:tuningFields.GimbalInputImpulse.Text = [string]$profile.GimbalInputImpulse
            $script:tuningFields.GimbalStaticFriction.Text = [string]$profile.GimbalStaticFriction
            $script:tuningFields.GimbalDynamicFriction.Text = [string]$profile.GimbalDynamicFriction
            $script:tuningFields.GimbalEdgeBumper.Text = [string]$profile.GimbalEdgeBumper
            $script:tuningFields.GimbalAntiwindupStart.Text = [string]$profile.GimbalAntiwindupStart
            $script:tuningFields.GimbalAntiwindupMinGain.Text = [string]$profile.GimbalAntiwindupMinGain
            $script:tuningFields.InputGainMode.Text = [string]$profile.InputGainMode
            $script:tuningFields.AdaptiveSlowGain.Text = [string]$profile.AdaptiveSlowGain
            $script:tuningFields.AdaptiveFastGain.Text = [string]$profile.AdaptiveFastGain
            $script:tuningFields.AdaptiveSpeedLow.Text = [string]$profile.AdaptiveSpeedLow
            $script:tuningFields.AdaptiveSpeedHigh.Text = [string]$profile.AdaptiveSpeedHigh
            $script:tuningFields.AdaptiveCurve.Text = [string]$profile.AdaptiveCurve
            $script:tuningFields.AdaptiveTrackerMs.Text = [string]$profile.AdaptiveTrackerMs
            $script:tuningFields.GateShape.Text = [string]$profile.GateShape
            $script:tuningFields.DiagonalScale.Text = [string]$profile.DiagonalScale
            $script:tuningFields.OutputShape.LoadFromTomlValue([string]$profile.OutputShapeNodesText)
            $script:tuningFields.ReturnShape.LoadFromTomlValue([string]$profile.ReturnShapeNodesText)
            $script:tuningChecks.DespikeEnabled.Checked = ((ConvertTo-TomlBool $profile.DespikeEnabled) -eq 'true')
            $script:tuningChecks.DespikeCountEnabled.Checked = ((ConvertTo-TomlBool $profile.DespikeCountEnabled) -eq 'true')
            $script:tuningChecks.GimbalAntiwindupEnabled.Checked = ((ConvertTo-TomlBool $profile.GimbalAntiwindupEnabled) -eq 'true')
            $script:tuningChecks.OutputShapingEnabled.Checked = ((ConvertTo-TomlBool $profile.OutputShapingEnabled) -eq 'true')
            $script:tuningChecks.ReturnShapingEnabled.Checked = ((ConvertTo-TomlBool $profile.ReturnShapingEnabled) -eq 'true')
        }
        Update-RightStickControlState
    }
    if ($script:tuningFields.ContainsKey('AimSensitivityX')) {
        $script:tuningFields.AimSensitivityX.Text = [string]$profile.AimSensitivityX
        $script:tuningFields.AimSensitivityY.Text = [string]$profile.AimSensitivityY
        $script:tuningFields.AimReticleLimit.Text = [string]$profile.AimReticleLimit
        $script:tuningFields.AimDeadband.Text = [string]$profile.AimDeadband
        $script:tuningFields.AimReturnRate.Text = [string]$profile.AimReturnRate
        $script:tuningFields.AimSmoothing.Text = [string]$profile.AimSmoothing
        $script:tuningFields.AimRollGain.Text = [string]$profile.AimRollGain
        $script:tuningFields.AimYawGain.Text = [string]$profile.AimYawGain
        $script:tuningFields.AimPitchGain.Text = [string]$profile.AimPitchGain
        $script:tuningFields.AimRollMax.Text = [string]$profile.AimRollMax
        $script:tuningFields.AimYawMax.Text = [string]$profile.AimYawMax
        $script:tuningFields.AimPitchMax.Text = [string]$profile.AimPitchMax
        $script:tuningFields.AimSlewRate.Text = [string]$profile.AimSlewRate
        $script:tuningChecks.AimInvertX.Checked = ((ConvertTo-TomlBool $profile.AimInvertX) -eq 'true')
        $script:tuningChecks.AimInvertY.Checked = ((ConvertTo-TomlBool $profile.AimInvertY) -eq 'true')
        $script:droneAimExpanded = ($controlMode -eq 'drone_mouse_aim')
        Refresh-V3EditorSectionLayout
        Update-DroneAimControlState
    }
    if ($script:tuningFields.ContainsKey('ThrottleUpKey')) {
        if ($null -ne $script:leftStickOffRadio) {
            $script:leftStickOffRadio.Enabled = $true
            $script:leftStickOffRadio.ForeColor = $script:dark.Text
        }
        foreach ($name in @('KeyboardEnabled', 'MouseLeftEnabled')) {
            if ($script:tuningChecks.ContainsKey($name)) {
                $script:tuningChecks[$name].Enabled = $true
                $script:tuningChecks[$name].ForeColor = $script:dark.Text
            }
        }
        $script:tuningFields.KeyboardInputSource.Text = [string]$profile.KeyboardInputSource
        $script:tuningFields.ThrottleUpKey.Text = [string]$profile.ThrottleUpKey
        $script:tuningFields.ThrottleDownKey.Text = [string]$profile.ThrottleDownKey
        $script:tuningFields.ThrottleCutKey.Text = [string]$profile.ThrottleCutKey
        $script:tuningFields.ThrottleRate.Text = [string]$profile.ThrottleRate
        $script:tuningFields.ThrottleReturnRate.Text = [string]$profile.ThrottleReturnRate
        $script:tuningFields.YawLeftKey.Text = [string]$profile.YawLeftKey
        $script:tuningFields.YawRightKey.Text = [string]$profile.YawRightKey
        $script:tuningFields.YawPulse.Text = [string]$profile.YawPulse
        $script:tuningFields.YawSlewRate.Text = [string]$profile.YawSlewRate
        $script:tuningFields.KeyboardAnalogKeycodeMode.Text = [string]$profile.KeyboardAnalogKeycodeMode
        $script:tuningFields.KeyboardAnalogDeadzone.Text = [string]$profile.KeyboardAnalogDeadzone
        $script:tuningFields.KeyboardAnalogCurve.Text = [string]$profile.KeyboardAnalogCurve
        $script:tuningFields.KeyboardAnalogMin.Text = [string]$profile.KeyboardAnalogMin
        $script:tuningFields.KeyboardAnalogMax.Text = [string]$profile.KeyboardAnalogMax
        $script:tuningFields.MouseDeviceLeft.Text = [string]$profile.MouseDeviceLeft
        $script:tuningFields.MouseDeviceRight.Text = [string]$profile.MouseDeviceRight
        $script:tuningFields.MouseLeftThrottleRate.Text = [string]$profile.MouseLeftThrottleRate
        $script:tuningFields.MouseLeftThrottleReturnRate.Text = [string]$profile.MouseLeftThrottleReturnRate
        $script:tuningFields.MouseLeftYawGain.Text = [string]$profile.MouseLeftYawGain
        $script:tuningFields.MouseLeftYawMax.Text = [string]$profile.MouseLeftYawMax
        $script:tuningFields.MouseLeftYawDeadband.Text = [string]$profile.MouseLeftYawDeadband
        $script:tuningFields.MouseLeftYawSmoothing.Text = [string]$profile.MouseLeftYawSmoothing
        $script:tuningFields.MouseLeftYawSlewRate.Text = [string]$profile.MouseLeftYawSlewRate
        $script:tuningFields.MouseLeftYawReturnRate.Text = [string]$profile.MouseLeftYawReturnRate
        $script:tuningFields.MouseLeftYawReturnIdle.Text = [string]$profile.MouseLeftYawReturnIdle
        $script:tuningFields.MouseLeftYawConstantReturnRate.Text = [string]$profile.MouseLeftYawConstantReturnRate
        $script:tuningFields.MouseLeftYawElasticReturnMode.Text = [string]$profile.MouseLeftYawElasticReturnMode
        $script:tuningFields.MouseLeftYawElasticReturnCoefficient.Text = [string]$profile.MouseLeftYawElasticReturnCoefficient
        $script:tuningFields.MouseLeftYawElasticReturnCurve.Text = [string]$profile.MouseLeftYawElasticReturnCurve
        if ($script:tuningFields.ContainsKey('MouseLeftYawInputFilter')) {
            $script:tuningFields.MouseLeftYawInputFilter.Text = [string]$profile.MouseLeftYawInputFilter
            $script:tuningFields.MouseLeftYawOneEuroMinCutoffHz.Text = [string]$profile.MouseLeftYawOneEuroMinCutoffHz
            $script:tuningFields.MouseLeftYawOneEuroBeta.Text = [string]$profile.MouseLeftYawOneEuroBeta
            $script:tuningFields.MouseLeftYawOneEuroDcutoffHz.Text = [string]$profile.MouseLeftYawOneEuroDcutoffHz
            $script:tuningFields.MouseLeftYawDespikeWindow.Text = [string]$profile.MouseLeftYawDespikeWindow
            $script:tuningFields.MouseLeftYawDespikeThresholdSigma.Text = [string]$profile.MouseLeftYawDespikeThresholdSigma
            $script:tuningFields.MouseLeftYawOutputCurve.Text = [string]$profile.MouseLeftYawOutputCurve
            $script:tuningFields.MouseLeftYawExpo.Text = [string]$profile.MouseLeftYawExpo
            $script:tuningFields.MouseLeftYawActualCenter.Text = [string]$profile.MouseLeftYawActualCenter
            $script:tuningFields.MouseLeftYawActualMax.Text = [string]$profile.MouseLeftYawActualMax
            $script:tuningFields.MouseLeftYawActualExpo.Text = [string]$profile.MouseLeftYawActualExpo
            $script:tuningFields.MouseLeftYawPositionModel.Text = [string]$profile.MouseLeftYawPositionModel
            $script:tuningFields.MouseLeftYawGimbalFrequencyHz.Text = [string]$profile.MouseLeftYawGimbalFrequencyHz
            $script:tuningFields.MouseLeftYawGimbalDampingRatio.Text = [string]$profile.MouseLeftYawGimbalDampingRatio
            $script:tuningFields.MouseLeftYawGimbalInputImpulse.Text = [string]$profile.MouseLeftYawGimbalInputImpulse
            $script:tuningFields.MouseLeftYawGimbalStaticFriction.Text = [string]$profile.MouseLeftYawGimbalStaticFriction
            $script:tuningFields.MouseLeftYawGimbalDynamicFriction.Text = [string]$profile.MouseLeftYawGimbalDynamicFriction
            $script:tuningFields.MouseLeftYawGimbalEdgeBumper.Text = [string]$profile.MouseLeftYawGimbalEdgeBumper
            $script:tuningFields.MouseLeftYawGimbalAntiwindupStart.Text = [string]$profile.MouseLeftYawGimbalAntiwindupStart
            $script:tuningFields.MouseLeftYawGimbalAntiwindupMinGain.Text = [string]$profile.MouseLeftYawGimbalAntiwindupMinGain
            $script:tuningFields.MouseLeftYawInputGainMode.Text = [string]$profile.MouseLeftYawInputGainMode
            $script:tuningFields.MouseLeftYawAdaptiveSlowGain.Text = [string]$profile.MouseLeftYawAdaptiveSlowGain
            $script:tuningFields.MouseLeftYawAdaptiveFastGain.Text = [string]$profile.MouseLeftYawAdaptiveFastGain
            $script:tuningFields.MouseLeftYawAdaptiveSpeedLow.Text = [string]$profile.MouseLeftYawAdaptiveSpeedLow
            $script:tuningFields.MouseLeftYawAdaptiveSpeedHigh.Text = [string]$profile.MouseLeftYawAdaptiveSpeedHigh
            $script:tuningFields.MouseLeftYawAdaptiveCurve.Text = [string]$profile.MouseLeftYawAdaptiveCurve
            $script:tuningFields.MouseLeftYawAdaptiveTrackerMs.Text = [string]$profile.MouseLeftYawAdaptiveTrackerMs
            $script:tuningFields.MouseLeftYawGateShape.Text = [string]$profile.MouseLeftYawGateShape
            $script:tuningFields.MouseLeftYawDiagonalScale.Text = [string]$profile.MouseLeftYawDiagonalScale
            $script:tuningFields.MouseLeftYawOutputShape.LoadFromTomlValue([string]$profile.MouseLeftYawOutputShapeNodesText)
            $script:tuningFields.MouseLeftYawReturnShape.LoadFromTomlValue([string]$profile.MouseLeftYawReturnShapeNodesText)
            $script:tuningChecks.MouseLeftYawShapingEnabled.Checked = ((ConvertTo-TomlBool $profile.MouseLeftYawShapingEnabled) -eq 'true')
            $script:tuningChecks.MouseLeftYawDespikeEnabled.Checked = ((ConvertTo-TomlBool $profile.MouseLeftYawDespikeEnabled) -eq 'true')
            $script:tuningChecks.MouseLeftYawDespikeCountEnabled.Checked = ((ConvertTo-TomlBool $profile.MouseLeftYawDespikeCountEnabled) -eq 'true')
            $script:tuningChecks.MouseLeftYawGimbalAntiwindupEnabled.Checked = ((ConvertTo-TomlBool $profile.MouseLeftYawGimbalAntiwindupEnabled) -eq 'true')
            $script:tuningChecks.MouseLeftYawOutputShapingEnabled.Checked = ((ConvertTo-TomlBool $profile.MouseLeftYawOutputShapingEnabled) -eq 'true')
            $script:tuningChecks.MouseLeftYawReturnShapingEnabled.Checked = ((ConvertTo-TomlBool $profile.MouseLeftYawReturnShapingEnabled) -eq 'true')
        }
        $script:tuningChecks.KeyboardRequireAnalog.Checked = ((ConvertTo-TomlBool $profile.KeyboardRequireAnalog) -eq 'true')
        $script:tuningChecks.BlockSelectedKeys.Checked = ((ConvertTo-TomlBool $profile.BlockSelectedKeys) -eq 'true')
        $script:tuningChecks.ThrottleReturnEnabled.Checked = ((ConvertTo-TomlBool $profile.ThrottleReturnEnabled) -eq 'true')
        $script:tuningChecks.InvertYaw.Checked = ((ConvertTo-TomlBool $profile.InvertYaw) -eq 'true')
        $script:tuningChecks.MouseLeftRequireDevice.Checked = ((ConvertTo-TomlBool $profile.MouseLeftRequireDevice) -eq 'true')
        $script:tuningChecks.MouseLeftThrottleReturnEnabled.Checked = ((ConvertTo-TomlBool $profile.MouseLeftThrottleReturnEnabled) -eq 'true')
        $script:tuningChecks.MouseLeftInvertThrottle.Checked = ((ConvertTo-TomlBool $profile.MouseLeftInvertThrottle) -eq 'true')
        $script:tuningChecks.MouseLeftInvertYaw.Checked = ((ConvertTo-TomlBool $profile.MouseLeftInvertYaw) -eq 'true')
        $script:tuningChecks.MouseLeftSwapAxes.Checked = ((ConvertTo-TomlBool $profile.MouseLeftSwapAxes) -eq 'true')
        $script:tuningChecks.MouseLeftYawReturnEnabled.Checked = ((ConvertTo-TomlBool $profile.MouseLeftYawReturnEnabled) -eq 'true')
        $script:tuningChecks.MouseLeftYawConstantReturnEnabled.Checked = ((ConvertTo-TomlBool $profile.MouseLeftYawConstantReturnEnabled) -eq 'true')
        $script:tuningChecks.MouseLeftYawElasticReturnEnabled.Checked = ((ConvertTo-TomlBool $profile.MouseLeftYawElasticReturnEnabled) -eq 'true')

        $keyboardEnabled = ((ConvertTo-TomlBool $profile.KeyboardEnabled) -eq 'true')
        $mouseLeftEnabled = ((ConvertTo-TomlBool $profile.MouseLeftEnabled) -eq 'true')
        if ($mouseLeftEnabled) {
            $script:tuningChecks.MouseLeftEnabled.Checked = $true
        } elseif ($keyboardEnabled) {
            $script:tuningChecks.KeyboardEnabled.Checked = $true
        } else {
            $script:leftStickOffRadio.Checked = $true
        }
        Update-LeftStickControlState
    }
    if ($null -ne $script:defaultCheck) {
        $script:defaultCheck.Checked = $isDefault
    }
    [void](Validate-TuningControls)
    if ($null -ne $script:statusText) {
        $script:statusText.Text = "Editing $($profile.FileName). Changes auto-save."
    }
    } finally {
        $script:loadingProfile = $false
    }
}

function Set-RunStatus {
    param(
        [ValidateSet('Idle', 'Running', 'Stopped', 'Warn')]
        [string]$State,
        [string]$Text
    )

    if ($null -ne $script:statusText) {
        $script:statusText.Text = $Text
    }
    if ($null -eq $script:runStatusBadge) {
        return
    }
    switch ($State) {
        'Running' { $script:runStatusBadge.BackColor = $script:dark.Success; $script:runStatusBadge.Text = 'Running' }
        'Stopped' { $script:runStatusBadge.BackColor = $script:dark.TextFaint; $script:runStatusBadge.Text = 'Stopped' }
        'Warn'    { $script:runStatusBadge.BackColor = $script:dark.Warn; $script:runStatusBadge.Text = 'Attention' }
        default   { $script:runStatusBadge.BackColor = $script:dark.BgElev; $script:runStatusBadge.Text = 'Idle' }
    }
}

# --- Form construction ---
function Build-LauncherForm {
    $script:form = New-Object System.Windows.Forms.Form
    $script:form.Text = 'GX12 Mouse Launcher V3'
    $script:form.StartPosition = 'CenterScreen'
    $script:form.Size = New-Object System.Drawing.Size(1480, 940)
    $script:form.MinimumSize = New-Object System.Drawing.Size(1180, 720)
    $script:form.Font = New-DarkFont -Size 9.5
    $script:form.BackColor = $script:dark.BgBase
    $script:form.ForeColor = $script:dark.Text

    # --- Header band ---
    $header = New-Object System.Windows.Forms.Panel
    $header.Location = New-Object System.Drawing.Point(0, 0)
    $header.Size = New-Object System.Drawing.Size(1480, 48)
    $header.Dock = [System.Windows.Forms.DockStyle]::Top
    $header.BackColor = $script:dark.BgBase
    $script:form.Controls.Add($header)

    $appTitle = New-DarkLabel -Text 'GX12 Mouse Launcher' -X 22 -Y 12 -W 320 -H 24 -Size 12.0 -Style ([System.Drawing.FontStyle]::Bold)
    $header.Controls.Add($appTitle)

    $appBadge = New-DarkLabel -Text 'V3 - current' -X 232 -Y 16 -W 200 -H 18 -Size 8.6 -Color $script:dark.TextMuted
    $header.Controls.Add($appBadge)

    $script:closeButton = New-DarkButton -Text 'Close' -Icon 0xE8BB -Width 96 -Height 30 -Kind 'Ghost'
    $script:closeButton.Anchor = 'Top,Right'
    $script:closeButton.Location = New-Object System.Drawing.Point(($script:form.Width - 132), 9)
    $script:closeButton.Add_Click({ $script:form.Close() })
    $header.Controls.Add($script:closeButton)

    # --- Sidebar ---
    $script:sidebar = New-DarkPanel -X 14 -Y 60 -W 320 -H 820 -Radius 10 -BackColor $script:dark.BgSurface -BorderColor $script:dark.Border
    $script:sidebar.Anchor = 'Top,Bottom,Left'
    $script:form.Controls.Add($script:sidebar)

    $sidebarTitle = New-DarkLabel -Text 'Tuning profiles' -X 18 -Y 16 -W 180 -H 24 -Size 11.0 -Style ([System.Drawing.FontStyle]::Bold)
    $script:sidebar.Controls.Add($sidebarTitle)

    $sidebarHint = New-DarkLabel -Text "$(@(Load-Profiles).Count) profiles" -X 18 -Y 38 -W 200 -H 18 -Size 8.6 -Color $script:dark.TextMuted
    $script:sidebar.Controls.Add($sidebarHint)
    $script:sidebarHint = $sidebarHint

    $cloneButton = New-DarkButton -Text '' -Icon 0xE710 -Width 36 -Height 30 -Kind 'Ghost'
    $cloneButton.Location = New-Object System.Drawing.Point(238, 14)
    $cloneButton.Add_Click({ New-ProfileCopy })
    $script:sidebar.Controls.Add($cloneButton)

    $folderButton = New-DarkButton -Text '' -Icon 0xE838 -Width 36 -Height 30 -Kind 'Ghost'
    $folderButton.Location = New-Object System.Drawing.Point(278, 14)
    $folderButton.Add_Click({ Open-ProfileDirectory })
    $script:sidebar.Controls.Add($folderButton)

    $searchInput = New-DarkInput -X 18 -Y 64 -W 296 -H 32
    $searchInput.Frame.Anchor = 'Top,Left,Right'
    $script:profileSearch = $searchInput.TextBox
    $script:profileSearch.Add_TextChanged({ Apply-ProfileFilter -TargetFileName (Get-SelectedProfileFileName) })
    $searchIcon = New-DarkLabel -Text ([char]0xE721) -X 4 -Y 6 -W 18 -H 18 -Color $script:dark.TextMuted
    $searchIcon.Font = New-IconFont -Size 11.0
    $script:profileSearch.Location = New-Object System.Drawing.Point(($script:profileSearch.Location.X + 18), $script:profileSearch.Location.Y)
    $script:profileSearch.Width = $script:profileSearch.Width - 18
    $searchInput.Frame.Controls.Add($searchIcon)
    $script:sidebar.Controls.Add($searchInput.Frame)

    $script:profilesList = New-Object System.Windows.Forms.ListBox
    $script:profilesList.Location = New-Object System.Drawing.Point(12, 108)
    $script:profilesList.Size = New-Object System.Drawing.Size(296, 660)
    $script:profilesList.Anchor = 'Top,Bottom,Left,Right'
    $script:profilesList.BorderStyle = [System.Windows.Forms.BorderStyle]::None
    $script:profilesList.BackColor = $script:dark.BgSurface
    $script:profilesList.ForeColor = $script:dark.Text
    $script:profilesList.DrawMode = [System.Windows.Forms.DrawMode]::OwnerDrawFixed
    $script:profilesList.DisplayMember = 'Display'
    $script:profilesList.FormattingEnabled = $true
    $script:profilesList.ItemHeight = 56
    $script:profilesList.IntegralHeight = $false
    $script:profilesList.Add_DrawItem({ param($s, $e) Draw-V3ProfileItem -EventArgs $e })
    $script:profilesList.Add_SelectedIndexChanged({ Update-Details })
    $script:sidebar.Controls.Add($script:profilesList)

    $changeFolderButton = New-DarkButton -Text 'Change folder' -Icon 0xE8B7 -Width 296 -Height 36 -Kind 'Subtle'
    $changeFolderButton.Anchor = 'Bottom,Left,Right'
    $changeFolderButton.Location = New-Object System.Drawing.Point(12, 776)
    $changeFolderButton.Add_Click({ Choose-ProfileDirectory })
    $script:sidebar.Controls.Add($changeFolderButton)

    # --- Editor card ---
    $script:editorCard = New-DarkPanel -X 348 -Y 60 -W 860 -H 820 -Radius 10 -BackColor $script:dark.BgSurface -BorderColor $script:dark.Border
    $script:editorCard.Anchor = 'Top,Bottom,Left,Right'
    $script:form.Controls.Add($script:editorCard)

    $script:profileTitle = New-DarkLabel -Text 'No profile selected' -X 28 -Y 22 -W 700 -H 32 -Size 16.0 -Style ([System.Drawing.FontStyle]::Bold)
    $script:profileTitle.Anchor = 'Top,Left,Right'
    $script:editorCard.Controls.Add($script:profileTitle)

    $script:profileFile = New-DarkLabel -Text '' -X 28 -Y 56 -W 700 -H 22 -Size 9.6 -Color $script:dark.TextMuted
    $script:profileFile.Anchor = 'Top,Left,Right'
    $script:editorCard.Controls.Add($script:profileFile)

    $script:profilePath = New-DarkLabel -Text '' -X 28 -Y 78 -W 700 -H 22 -Size 9.0 -Color $script:dark.TextFaint
    $script:profilePath.Anchor = 'Top,Left,Right'
    $script:editorCard.Controls.Add($script:profilePath)

    $headerRule = New-Object System.Windows.Forms.Panel
    $headerRule.Location = New-Object System.Drawing.Point(28, 112)
    $headerRule.Size = New-Object System.Drawing.Size(800, 1)
    $headerRule.Anchor = 'Top,Left,Right'
    $headerRule.BackColor = $script:dark.Border
    $script:editorCard.Controls.Add($headerRule)

    $script:editorTabStrip = New-Object System.Windows.Forms.Panel
    $script:editorTabStrip.Location = New-Object System.Drawing.Point(20, 124)
    $script:editorTabStrip.Size = New-Object System.Drawing.Size(820, 36)
    $script:editorTabStrip.Anchor = 'Top,Left,Right'
    $script:editorTabStrip.BackColor = $script:dark.BgSurface
    $script:editorCard.Controls.Add($script:editorTabStrip)

    $script:editorScroll = New-Object System.Windows.Forms.Panel
    $script:editorScroll.Location = New-Object System.Drawing.Point(20, 166)
    $script:editorScroll.Size = New-Object System.Drawing.Size(820, 624)
    $script:editorScroll.Anchor = 'Top,Bottom,Left,Right'
    $script:editorScroll.BackColor = $script:dark.BgSurface
    $script:editorCard.Controls.Add($script:editorScroll)

    $overviewPage = New-V3EditorTabPage -Name 'Overview' -Text 'Overview'
    $rightPage = New-V3EditorTabPage -Name 'Right' -Text 'Right'
    $rightAdvancedPage = New-V3EditorTabPage -Name 'RightAdvanced' -Text 'Right Adv'
    $leftKeysPage = New-V3EditorTabPage -Name 'LeftKeys' -Text 'Left Keys'
    $leftMousePage = New-V3EditorTabPage -Name 'LeftMouse' -Text 'Left Mouse'
    $leftYawPage = New-V3EditorTabPage -Name 'LeftYaw' -Text 'Left Yaw'
    $droneAimPage = New-V3EditorTabPage -Name 'DroneAim' -Text 'Drone Aim'

    $script:overviewSection = New-V3OverviewSection -Parent $overviewPage -X 0 -Y 0 -W 792
    $script:rightStickSection = New-V3RightStickSection -Parent $rightPage -X 0 -Y 0 -W 792
    Hide-V3DirectSectionButton -Section $script:rightStickSection -Text 'Advanced'
    $script:rightStickAdvancedSection = New-V3TabSection -Parent $rightAdvancedPage -Title 'Right Stick Advanced' -W 792 -H 662
    if ($null -ne $script:rightStickAdvancedPanel) {
        $script:rightStickAdvancedSection.Controls.Add($script:rightStickAdvancedPanel)
        $script:rightStickAdvancedPanel.Location = New-Object System.Drawing.Point(22, 52)
        $script:rightStickAdvancedPanel.Visible = $true
        Set-V3TuningLabelText -Name 'AdaptiveSpeedLow' -Text 'Low'
    }

    $script:leftStickSection = New-V3LeftStickSection -Parent $leftKeysPage -X 0 -Y 0 -W 792
    Hide-V3DirectSectionButton -Section $script:leftStickSection -Text 'Yaw advanced'
    $script:leftStickMouseSection = New-V3TabSection -Parent $leftMousePage -Title 'Left Stick - Second Mouse' -W 792 -H 480
    if ($null -ne $script:leftStickMouseBox) {
        $script:leftStickMouseSection.Controls.Add($script:leftStickMouseBox)
        $script:leftStickMouseBox.Location = New-Object System.Drawing.Point(22, 52)
    }
    $script:leftStickYawSection = New-V3TabSection -Parent $leftYawPage -Title 'Left Stick - Yaw Advanced' -W 792 -H 632
    if ($null -ne $script:leftStickYawAdvancedPanel) {
        $script:leftStickYawSection.Controls.Add($script:leftStickYawAdvancedPanel)
        $script:leftStickYawAdvancedPanel.Location = New-Object System.Drawing.Point(22, 52)
        $script:leftStickYawAdvancedPanel.Visible = $true
        Compress-V3LeftYawAdvancedLayout
    }

    $script:droneAimExpanded = $true
    $script:droneAimSection = New-V3DroneAimSection -Parent $droneAimPage -X 0 -Y 0 -W 792
    Hide-V3DirectSectionButton -Section $script:droneAimSection -Text 'Show'
    if ($null -ne $script:droneAimContentPanel) {
        $script:droneAimContentPanel.Visible = $true
    }
    Select-V3EditorTab -Name 'Overview'
    Refresh-V3EditorSectionLayout

    # --- Run rail ---
    $script:runRail = New-DarkPanel -X 1222 -Y 60 -W 244 -H 820 -Radius 10 -BackColor $script:dark.BgSurface -BorderColor $script:dark.Border
    $script:runRail.Anchor = 'Top,Bottom,Right'
    $script:form.Controls.Add($script:runRail)

    $runTitle = New-DarkLabel -Text 'Run' -X 20 -Y 18 -W 120 -H 24 -Size 11.0 -Style ([System.Drawing.FontStyle]::Bold)
    $script:runRail.Controls.Add($runTitle)

    $script:runStatusBadge = New-Object System.Windows.Forms.Label
    $script:runStatusBadge.Location = New-Object System.Drawing.Point(140, 22)
    $script:runStatusBadge.Size = New-Object System.Drawing.Size(82, 22)
    $script:runStatusBadge.Font = New-DarkFont -Size 8.6 -Style ([System.Drawing.FontStyle]::Bold)
    $script:runStatusBadge.TextAlign = [System.Drawing.ContentAlignment]::MiddleCenter
    $script:runStatusBadge.ForeColor = $script:dark.BgBase
    $script:runStatusBadge.BackColor = $script:dark.BgElev
    $script:runStatusBadge.Text = 'Idle'
    $script:runRail.Controls.Add($script:runStatusBadge)

    $startTrainer = New-DarkButton -Text 'Start composite trainer' -Icon 0xE768 -Width 208 -Height 44 -Kind 'Primary'
    $startTrainer.Location = New-Object System.Drawing.Point(18, 60)
    $startTrainer.Add_Click({
        if ($null -ne $script:profilesList.SelectedItem) {
            Flush-PendingTuningSave
            Start-Gx12Process -ProfilePath $script:profilesList.SelectedItem.FullName -Mode 'CompositeTrainer'
            Set-RunStatus -State 'Running' -Text $script:statusText.Text
        } else {
            Set-RunStatus -State 'Warn' -Text 'Select a profile first.'
        }
    })
    $script:runRail.Controls.Add($startTrainer)

    $stopButton = New-DarkButton -Text 'Stop run' -Icon 0xE71A -Width 208 -Height 38 -Kind 'Danger'
    $stopButton.Location = New-Object System.Drawing.Point(18, 112)
    $stopButton.Add_Click({
        Stop-ActiveGx12Run
        Set-RunStatus -State 'Stopped' -Text 'Stopped active gx12mouse run.'
    })
    $script:runRail.Controls.Add($stopButton)

    $rule1 = New-Object System.Windows.Forms.Panel
    $rule1.Location = New-Object System.Drawing.Point(20, 164)
    $rule1.Size = New-Object System.Drawing.Size(204, 1)
    $rule1.BackColor = $script:dark.Border
    $script:runRail.Controls.Add($rule1)

    $setDefault = New-DarkButton -Text 'Set as default' -Icon 0xE734 -Width 208 -Height 36 -Kind 'Subtle'
    $setDefault.Location = New-Object System.Drawing.Point(18, 180)
    $setDefault.Add_Click({
        if ($null -ne $script:profilesList.SelectedItem) {
            Flush-PendingTuningSave
            Set-DefaultProfileFileName -FileName $script:profilesList.SelectedItem.FileName
            $selectedFileName = $script:profilesList.SelectedItem.FileName
            Set-RunStatus -State 'Idle' -Text "Default profile is now $selectedFileName."
            Refresh-Profiles
            Apply-ProfileFilter -TargetFileName $selectedFileName
        }
    })
    $script:runRail.Controls.Add($setDefault)

    $refreshButton = New-DarkButton -Text 'Refresh' -Icon 0xE72C -Width 208 -Height 36 -Kind 'Ghost'
    $refreshButton.Location = New-Object System.Drawing.Point(18, 268)
    $refreshButton.Add_Click({ Refresh-Profiles })
    $script:runRail.Controls.Add($refreshButton)

    $copyPath = New-DarkButton -Text 'Copy path' -Icon 0xE8C8 -Width 208 -Height 36 -Kind 'Ghost'
    $copyPath.Location = New-Object System.Drawing.Point(18, 310)
    $copyPath.Add_Click({
        if ($null -ne $script:profilesList.SelectedItem) {
            [System.Windows.Forms.Clipboard]::SetText($script:profilesList.SelectedItem.FullName)
            Set-RunStatus -State 'Idle' -Text "Copied path for $($script:profilesList.SelectedItem.FileName)."
        }
    })
    $script:runRail.Controls.Add($copyPath)

    $statusBox = New-DarkPanel -X 18 -Y 632 -W 208 -H 168 -Radius 8 -BackColor $script:dark.BgElev -BorderColor $script:dark.Border
    $statusBox.Anchor = 'Bottom,Left,Right'
    $script:runRail.Controls.Add($statusBox)

    $statusHeader = New-DarkLabel -Text 'Status' -X 14 -Y 12 -W 180 -H 18 -Size 9.5 -Style ([System.Drawing.FontStyle]::Bold)
    $statusBox.Controls.Add($statusHeader)

    $script:statusText = New-Object System.Windows.Forms.Label
    $script:statusText.Location = New-Object System.Drawing.Point(14, 36)
    $script:statusText.Size = New-Object System.Drawing.Size(180, 122)
    $script:statusText.Font = New-DarkFont -Size 9.0
    $script:statusText.ForeColor = $script:dark.TextMuted
    $script:statusText.BackColor = [System.Drawing.Color]::Transparent
    $script:statusText.Text = 'Composite trainer is the normal path. The selected profile stop key stops safely.'
    $statusBox.Controls.Add($script:statusText)

    # Backwards-compat for any imported backend code that pokes $script:status.
    $script:status = $script:statusText

    $script:saveTimer = New-Object System.Windows.Forms.Timer
    $script:saveTimer.Interval = 600
    $script:saveTimer.Add_Tick({
        $script:saveTimer.Stop()
        Save-PendingTuningChanges
    })

    function script:Update-V3Layout {
        $client = $script:form.ClientSize
        $headerH = 60
        $sideW = 320
        $railW = 244
        $padding = 14

        $script:sidebar.Location = New-Object System.Drawing.Point($padding, $headerH)
        $script:sidebar.Size = New-Object System.Drawing.Size($sideW, ($client.Height - $headerH - $padding))

        $script:runRail.Location = New-Object System.Drawing.Point(($client.Width - $railW - $padding), $headerH)
        $script:runRail.Size = New-Object System.Drawing.Size($railW, ($client.Height - $headerH - $padding))

        $editorX = $padding + $sideW + $padding
        $editorW = $client.Width - $editorX - $railW - $padding - $padding
        if ($editorW -lt 360) { $editorW = 360 }
        $script:editorCard.Location = New-Object System.Drawing.Point($editorX, $headerH)
        $script:editorCard.Size = New-Object System.Drawing.Size($editorW, ($client.Height - $headerH - $padding))

        if ($null -ne $script:editorTabStrip) {
            $script:editorTabStrip.Size = New-Object System.Drawing.Size(($editorW - 40), 36)
        }
        if ($null -ne $script:editorScroll) {
            $script:editorScroll.Size = New-Object System.Drawing.Size(($editorW - 40), [Math]::Max(240, ($script:editorCard.Height - 186)))
        }
        Refresh-V3EditorSectionLayout

        $script:closeButton.Left = $client.Width - 132
    }

    $script:form.Add_Resize({ Update-V3Layout })
    $script:form.Add_Shown({ Update-V3Layout })
    $script:form.Add_FormClosing({
        Flush-PendingTuningSave
        Stop-ActiveGx12Run
    })

    Update-V3Layout
    return $script:form
}

if ($SelfTest) {
    $profiles = @(Load-Profiles)
    "root=$root"
    "profiles_dir=$profilesDir"
    "exe_exists=$(Test-Path -LiteralPath $exePath)"
    "default_profile=$(Get-DefaultProfileFileName)"
    "profiles=$($profiles.Count)"
    $testLines = @('[mapper]', 'roll_gain = 10.0', '', '[logging]', 'csv = true')
    $testLines = Set-TomlValue -Lines $testLines -Section 'mapper' -Key 'roll_gain' -Value '12.5'
    $testLines = Set-TomlValue -Lines $testLines -Section 'mapper' -Key 'return_rate' -Value '0'
    if (($testLines -notcontains 'roll_gain = 12.5') -or ($testLines -notcontains 'return_rate = 0')) {
        throw 'Set-TomlValue self-test failed.'
    }
    "toml_update=ok"
    Initialize-LauncherExceptionHandlers
    [System.Windows.Forms.Application]::EnableVisualStyles()
    $testForm = Build-LauncherForm
    try {
        Refresh-Profiles
        foreach ($requiredField in @('ProfileName', 'FrameRate', 'StopKey', 'FreezeKey')) {
            if (-not $script:tuningFields.ContainsKey($requiredField)) {
                throw "Missing V3 overview field $requiredField."
            }
        }
        foreach ($requiredField in @('RollGain', 'PitchGain', 'MaxOutput', 'Deadband', 'OutputCurve', 'Expo', 'ReturnRate', 'ReturnIdle', 'ConstantReturnRate', 'ElasticReturnMode', 'ElasticReturnCoefficient', 'ElasticReturnCurve') + $script:rightStickAdvancedFieldNames + $script:droneAimFieldNames) {
            if (-not $script:tuningFields.ContainsKey($requiredField)) {
                throw "Missing V3 right-stick field $requiredField."
            }
        }
        if (-not $script:tuningChecks.ContainsKey('WarThunderMode')) {
            throw 'Missing V3 overview check WarThunderMode.'
        }
        foreach ($requiredCheck in @('MouseRightStickEnabled', 'InvertRoll', 'InvertPitch', 'SwapAxes', 'ReturnEnabled', 'ConstantReturnEnabled', 'ElasticReturnEnabled') + $script:rightStickAdvancedCheckNames + $script:droneAimCheckNames) {
            if (-not $script:tuningChecks.ContainsKey($requiredCheck)) {
                throw "Missing V3 right-stick check $requiredCheck."
            }
        }
        foreach ($requiredField in ($script:leftStickKeyboardFieldNames + $script:leftStickMouseFieldNames + $script:leftStickMouseAdvancedFieldNames)) {
            if (-not $script:tuningFields.ContainsKey($requiredField)) {
                throw "Missing V3 left-stick field $requiredField."
            }
        }
        foreach ($requiredCheck in ($script:leftStickKeyboardCheckNames + $script:leftStickMouseCheckNames + $script:leftStickMouseAdvancedCheckNames)) {
            if (-not $script:tuningChecks.ContainsKey($requiredCheck)) {
                throw "Missing V3 left-stick check $requiredCheck."
            }
        }
        if (-not (Validate-TuningControls)) {
            throw 'V3 validation failed during self-test.'
        }
        $tempSaveDir = Join-Path $root 'logs'
        if (-not (Test-Path -LiteralPath $tempSaveDir)) {
            New-Item -ItemType Directory -Path $tempSaveDir -Force | Out-Null
        }
        $tempSaveFileName = 'gx12-launcher-v3-overview-selftest-{0}.toml' -f ([System.Guid]::NewGuid().ToString('N'))
        $tempSavePath = Join-Path $tempSaveDir $tempSaveFileName
        $oldEditingProfile = $script:editingProfile
        $oldStatusText = $script:statusText.Text
        try {
            Set-Content -LiteralPath $tempSavePath -Encoding ASCII -Value @(
                '[trainer]',
                'name = "before"',
                'frame_rate_hz = 1000',
                '',
                '[safety]',
                'stop_key = "Esc"',
                'freeze_key = "F2"',
                '',
                '[control]',
                'mode = "direct_mouse"',
                '',
                '[mapper]',
                'roll_gain = 1.0',
                'pitch_gain = 1.0',
                'max_output = 512',
                'deadband = 0',
                'output_curve = "expo"',
                'expo = 0.0',
                'return_enabled = false',
                'return_rate = 0',
                'return_idle_ms = 0',
                'constant_return_enabled = false',
                'constant_return_rate = 0',
                'elastic_return_enabled = false',
                'elastic_return_mode = "progressive"',
                'elastic_return_coefficient = 0',
                'elastic_return_curve = 0',
                'smoothing = 0.0',
                'input_filter = "off"',
                'one_euro_min_cutoff_hz = 1.0',
                'one_euro_beta = 0.05',
                'one_euro_dcutoff_hz = 1.0',
                'despike_enabled = false',
                'despike_count_enabled = false',
                'despike_window = 5',
                'despike_threshold_sigma = 3.0',
                'actual_center = 0.45',
                'actual_max = 1.0',
                'actual_expo = 0.30',
                'position_model = "integrator"',
                'gimbal_frequency_hz = 5.0',
                'gimbal_damping_ratio = 1.15',
                'gimbal_input_impulse = 1.0',
                'gimbal_static_friction = 0.0',
                'gimbal_dynamic_friction = 0.0',
                'gimbal_edge_bumper = 0.0',
                'gimbal_antiwindup_enabled = true',
                'gimbal_antiwindup_start = 0.92',
                'gimbal_antiwindup_min_gain = 0.10',
                'input_gain_mode = "flat"',
                'adaptive_slow_gain = 0.65',
                'adaptive_fast_gain = 1.60',
                'adaptive_speed_low = 120.0',
                'adaptive_speed_high = 1800.0',
                'adaptive_curve = 1.0',
                'adaptive_tracker_ms = 35.0',
                'gate_shape = "axis"',
                'diagonal_scale = 1.0',
                'output_shaping_enabled = false',
                'output_shape_nodes = []',
                'return_shaping_enabled = false',
                'return_shape_nodes = []',
                'invert_roll = false',
                'invert_pitch = false',
                'swap_axes = false',
                '',
                '[mouse_aim]',
                'sensitivity_x = 1.0',
                'sensitivity_y = 1.0',
                'reticle_limit = 512',
                'reticle_deadband = 8',
                'reticle_return_rate = 0',
                'output_smoothing = 0.10',
                'roll_gain = 0.65',
                'yaw_gain = 0.55',
                'pitch_gain = 0.85',
                'roll_max = 420',
                'yaw_max = 360',
                'pitch_max = 420',
                'slew_rate = 9000',
                'invert_x = false',
                'invert_y = false',
                '',
                '[mouse_right_stick]',
                'enabled = true',
                '',
                '[mouse_devices]',
                'right = "auto"',
                'left = ""',
                '',
                '[mouse_left_stick]',
                'enabled = false',
                'require_device = true',
                'throttle_rate = 0.8',
                'throttle_return_enabled = false',
                'throttle_return_rate = 768.0',
                'yaw_gain = 35.0',
                'yaw_pulse = 512',
                'yaw_deadband = 0',
                'yaw_smoothing = 0.0',
                'yaw_slew_rate = 0.0',
                'yaw_return_enabled = false',
                'yaw_return_rate = 0',
                'yaw_return_idle_ms = 0',
                'yaw_constant_return_enabled = false',
                'yaw_constant_return_rate = 0',
                'yaw_elastic_return_enabled = false',
                'yaw_elastic_return_mode = "progressive"',
                'yaw_elastic_return_coefficient = 0',
                'yaw_elastic_return_curve = 0',
                'yaw_shaping_enabled = false',
                'yaw_input_filter = "off"',
                'yaw_one_euro_min_cutoff_hz = 1.0',
                'yaw_one_euro_beta = 0.05',
                'yaw_one_euro_dcutoff_hz = 1.0',
                'yaw_despike_enabled = false',
                'yaw_despike_count_enabled = false',
                'yaw_despike_window = 5',
                'yaw_despike_threshold_sigma = 3.0',
                'yaw_output_curve = "expo"',
                'yaw_expo = 0.0',
                'yaw_actual_center = 0.45',
                'yaw_actual_max = 1.0',
                'yaw_actual_expo = 0.30',
                'yaw_position_model = "integrator"',
                'yaw_gimbal_frequency_hz = 5.0',
                'yaw_gimbal_damping_ratio = 1.15',
                'yaw_gimbal_input_impulse = 1.0',
                'yaw_gimbal_static_friction = 0.0',
                'yaw_gimbal_dynamic_friction = 0.0',
                'yaw_gimbal_edge_bumper = 0.0',
                'yaw_gimbal_antiwindup_enabled = true',
                'yaw_gimbal_antiwindup_start = 0.92',
                'yaw_gimbal_antiwindup_min_gain = 0.10',
                'yaw_input_gain_mode = "flat"',
                'yaw_adaptive_slow_gain = 0.65',
                'yaw_adaptive_fast_gain = 1.60',
                'yaw_adaptive_speed_low = 120.0',
                'yaw_adaptive_speed_high = 1800.0',
                'yaw_adaptive_curve = 1.0',
                'yaw_adaptive_tracker_ms = 35.0',
                'yaw_gate_shape = "axis"',
                'yaw_diagonal_scale = 1.0',
                'yaw_output_shaping_enabled = false',
                'yaw_return_shaping_enabled = false',
                'yaw_output_shape_nodes = []',
                'yaw_return_shape_nodes = []',
                'invert_throttle = false',
                'invert_yaw = false',
                'swap_axes = false',
                '',
                '[keyboard_left_stick]',
                'enabled = false',
                'input_source = "gameinput"',
                'require_analog = false',
                'block_selected_keys = false',
                'throttle_up_key = "W"',
                'throttle_down_key = "S"',
                'throttle_cut_key = "Space"',
                'throttle_rate = 4096',
                'throttle_return_enabled = false',
                'throttle_return_rate = 0',
                'yaw_left_key = "A"',
                'yaw_right_key = "D"',
                'yaw_pulse = 512',
                'yaw_slew_rate = 4096',
                'invert_yaw = false',
                'analog_keycode_mode = "virtual_key_translate"',
                'analog_deadzone = 0.04',
                'analog_curve = 1.0',
                'analog_min = 0.0',
                'analog_max = 1.0'
            )
            $script:editingProfile = [pscustomobject]@{
                Name = 'before'
                Display = 'before'
                FileName = $tempSaveFileName
                FullName = $tempSavePath
                FrameRate = '1000'
                StopKey = 'Esc'
                FreezeKey = 'F2'
                ControlMode = 'direct_mouse'
                RollGain = '1.0'
                PitchGain = '1.0'
                MaxOutput = '512'
                Deadband = '0'
                OutputCurve = 'expo'
                Expo = '0.0'
                ReturnEnabled = 'false'
                ReturnRate = '0'
                ReturnIdle = '0'
                ConstantReturnEnabled = 'false'
                ConstantReturnRate = '0'
                ElasticReturnEnabled = 'false'
                ElasticReturnMode = 'progressive'
                ElasticReturnCoefficient = '0'
                ElasticReturnCurve = '0'
                Smoothing = '0.0'
                InputFilter = 'off'
                OneEuroMinCutoffHz = '1.0'
                OneEuroBeta = '0.05'
                OneEuroDcutoffHz = '1.0'
                DespikeEnabled = 'false'
                DespikeCountEnabled = 'false'
                DespikeWindow = '5'
                DespikeThresholdSigma = '3.0'
                ActualCenter = '0.45'
                ActualMax = '1.0'
                ActualExpo = '0.30'
                PositionModel = 'integrator'
                GimbalFrequencyHz = '5.0'
                GimbalDampingRatio = '1.15'
                GimbalInputImpulse = '1.0'
                GimbalStaticFriction = '0.0'
                GimbalDynamicFriction = '0.0'
                GimbalEdgeBumper = '0.0'
                GimbalAntiwindupEnabled = 'true'
                GimbalAntiwindupStart = '0.92'
                GimbalAntiwindupMinGain = '0.10'
                InputGainMode = 'flat'
                AdaptiveSlowGain = '0.65'
                AdaptiveFastGain = '1.60'
                AdaptiveSpeedLow = '120.0'
                AdaptiveSpeedHigh = '1800.0'
                AdaptiveCurve = '1.0'
                AdaptiveTrackerMs = '35.0'
                GateShape = 'axis'
                DiagonalScale = '1.0'
                OutputShapingEnabled = 'false'
                OutputShapeNodesText = '[]'
                ReturnShapingEnabled = 'false'
                ReturnShapeNodesText = '[]'
                InvertRoll = 'false'
                InvertPitch = 'false'
                SwapAxes = 'false'
                AimSensitivityX = '1.0'
                AimSensitivityY = '1.0'
                AimReticleLimit = '512'
                AimDeadband = '8'
                AimReturnRate = '0'
                AimSmoothing = '0.10'
                AimRollGain = '0.65'
                AimYawGain = '0.55'
                AimPitchGain = '0.85'
                AimRollMax = '420'
                AimYawMax = '360'
                AimPitchMax = '420'
                AimSlewRate = '9000'
                AimInvertX = 'false'
                AimInvertY = 'false'
                MouseRightStickEnabled = 'true'
                MouseDeviceRight = 'auto'
                MouseDeviceLeft = ''
                MouseLeftEnabled = 'false'
                MouseLeftRequireDevice = 'true'
                MouseLeftThrottleRate = '0.8'
                MouseLeftThrottleReturnEnabled = 'false'
                MouseLeftThrottleReturnRate = '768.0'
                MouseLeftYawGain = '35.0'
                MouseLeftYawMax = '512'
                MouseLeftYawDeadband = '0'
                MouseLeftYawSmoothing = '0.0'
                MouseLeftYawSlewRate = '0.0'
                MouseLeftYawReturnEnabled = 'false'
                MouseLeftYawReturnRate = '0'
                MouseLeftYawReturnIdle = '0'
                MouseLeftYawConstantReturnEnabled = 'false'
                MouseLeftYawConstantReturnRate = '0'
                MouseLeftYawElasticReturnEnabled = 'false'
                MouseLeftYawElasticReturnMode = 'progressive'
                MouseLeftYawElasticReturnCoefficient = '0'
                MouseLeftYawElasticReturnCurve = '0'
                MouseLeftYawShapingEnabled = 'false'
                MouseLeftYawInputFilter = 'off'
                MouseLeftYawOneEuroMinCutoffHz = '1.0'
                MouseLeftYawOneEuroBeta = '0.05'
                MouseLeftYawOneEuroDcutoffHz = '1.0'
                MouseLeftYawDespikeEnabled = 'false'
                MouseLeftYawDespikeCountEnabled = 'false'
                MouseLeftYawDespikeWindow = '5'
                MouseLeftYawDespikeThresholdSigma = '3.0'
                MouseLeftYawOutputCurve = 'expo'
                MouseLeftYawExpo = '0.0'
                MouseLeftYawActualCenter = '0.45'
                MouseLeftYawActualMax = '1.0'
                MouseLeftYawActualExpo = '0.30'
                MouseLeftYawPositionModel = 'integrator'
                MouseLeftYawGimbalFrequencyHz = '5.0'
                MouseLeftYawGimbalDampingRatio = '1.15'
                MouseLeftYawGimbalInputImpulse = '1.0'
                MouseLeftYawGimbalStaticFriction = '0.0'
                MouseLeftYawGimbalDynamicFriction = '0.0'
                MouseLeftYawGimbalEdgeBumper = '0.0'
                MouseLeftYawGimbalAntiwindupEnabled = 'true'
                MouseLeftYawGimbalAntiwindupStart = '0.92'
                MouseLeftYawGimbalAntiwindupMinGain = '0.10'
                MouseLeftYawInputGainMode = 'flat'
                MouseLeftYawAdaptiveSlowGain = '0.65'
                MouseLeftYawAdaptiveFastGain = '1.60'
                MouseLeftYawAdaptiveSpeedLow = '120.0'
                MouseLeftYawAdaptiveSpeedHigh = '1800.0'
                MouseLeftYawAdaptiveCurve = '1.0'
                MouseLeftYawAdaptiveTrackerMs = '35.0'
                MouseLeftYawGateShape = 'axis'
                MouseLeftYawDiagonalScale = '1.0'
                MouseLeftYawOutputShapingEnabled = 'false'
                MouseLeftYawOutputShapeNodesText = '[]'
                MouseLeftYawReturnShapingEnabled = 'false'
                MouseLeftYawReturnShapeNodesText = '[]'
                MouseLeftInvertThrottle = 'false'
                MouseLeftInvertYaw = 'false'
                MouseLeftSwapAxes = 'false'
                KeyboardEnabled = 'false'
                KeyboardInputSource = 'gameinput'
                KeyboardRequireAnalog = 'false'
                BlockSelectedKeys = 'false'
                ThrottleUpKey = 'W'
                ThrottleDownKey = 'S'
                ThrottleCutKey = 'Space'
                ThrottleRate = '4096'
                ThrottleReturnEnabled = 'false'
                ThrottleReturnRate = '0'
                YawLeftKey = 'A'
                YawRightKey = 'D'
                YawPulse = '512'
                YawSlewRate = '4096'
                InvertYaw = 'false'
                KeyboardAnalogKeycodeMode = 'virtual_key_translate'
                KeyboardAnalogDeadzone = '0.04'
                KeyboardAnalogCurve = '1.0'
                KeyboardAnalogMin = '0.0'
                KeyboardAnalogMax = '1.0'
            }
            $script:loadingProfile = $true
            try {
                $script:tuningFields.ProfileName.Text = 'overview selftest'
                $script:tuningFields.FrameRate.Text = '777'
                $script:tuningFields.StopKey.Text = 'F1'
                $script:tuningFields.FreezeKey.Text = 'F3'
                $script:tuningChecks.WarThunderMode.Checked = $true
                $script:tuningFields.RollGain.Text = '12.5'
                $script:tuningFields.PitchGain.Text = '13.5'
                $script:tuningFields.MaxOutput.Text = '420'
                $script:tuningFields.Deadband.Text = '5'
                $script:tuningFields.OutputCurve.Text = 'expo'
                $script:tuningFields.Expo.Text = '0.25'
                $script:tuningChecks.MouseRightStickEnabled.Checked = $true
                $script:tuningChecks.InvertRoll.Checked = $true
                $script:tuningChecks.InvertPitch.Checked = $false
                $script:tuningChecks.SwapAxes.Checked = $true
                $script:tuningChecks.ReturnEnabled.Checked = $true
                $script:tuningFields.ReturnRate.Text = '80'
                $script:tuningFields.ReturnIdle.Text = '120'
                $script:tuningChecks.ConstantReturnEnabled.Checked = $true
                $script:tuningFields.ConstantReturnRate.Text = '20'
                $script:tuningChecks.ElasticReturnEnabled.Checked = $true
                $script:tuningFields.ElasticReturnMode.Text = 'smoothstep'
                $script:tuningFields.ElasticReturnCoefficient.Text = '4.5'
                $script:tuningFields.ElasticReturnCurve.Text = '0.7'
                $script:tuningFields.Smoothing.Text = '0.15'
                $script:tuningFields.InputFilter.Text = 'one_euro'
                $script:tuningFields.OneEuroMinCutoffHz.Text = '1.2'
                $script:tuningFields.OneEuroBeta.Text = '0.08'
                $script:tuningFields.OneEuroDcutoffHz.Text = '1.4'
                $script:tuningChecks.DespikeEnabled.Checked = $true
                $script:tuningChecks.DespikeCountEnabled.Checked = $true
                $script:tuningFields.DespikeWindow.Text = '7'
                $script:tuningFields.DespikeThresholdSigma.Text = '3.5'
                $script:tuningFields.ActualCenter.Text = '0.46'
                $script:tuningFields.ActualMax.Text = '0.95'
                $script:tuningFields.ActualExpo.Text = '0.22'
                $script:tuningFields.PositionModel.Text = 'dynamic_gimbal'
                $script:tuningFields.GimbalFrequencyHz.Text = '6'
                $script:tuningFields.GimbalDampingRatio.Text = '1.2'
                $script:tuningFields.GimbalInputImpulse.Text = '1.1'
                $script:tuningFields.GimbalStaticFriction.Text = '0.2'
                $script:tuningFields.GimbalDynamicFriction.Text = '0.3'
                $script:tuningFields.GimbalEdgeBumper.Text = '0.4'
                $script:tuningChecks.GimbalAntiwindupEnabled.Checked = $true
                $script:tuningFields.GimbalAntiwindupStart.Text = '0.9'
                $script:tuningFields.GimbalAntiwindupMinGain.Text = '0.2'
                $script:tuningFields.InputGainMode.Text = 'adaptive'
                $script:tuningFields.AdaptiveSlowGain.Text = '0.7'
                $script:tuningFields.AdaptiveFastGain.Text = '1.5'
                $script:tuningFields.AdaptiveSpeedLow.Text = '130'
                $script:tuningFields.AdaptiveSpeedHigh.Text = '1700'
                $script:tuningFields.AdaptiveCurve.Text = '1.1'
                $script:tuningFields.AdaptiveTrackerMs.Text = '40'
                $script:tuningFields.GateShape.Text = 'octagon'
                $script:tuningFields.DiagonalScale.Text = '0.9'
                $script:tuningChecks.OutputShapingEnabled.Checked = $true
                $script:tuningFields.OutputShape.LoadFromTomlValue('[[0.25,0.2,0.1]]')
                $script:tuningChecks.ReturnShapingEnabled.Checked = $true
                $script:tuningFields.ReturnShape.LoadFromTomlValue('[[0.75,0.6,0.2]]')
                $script:tuningFields.AimSensitivityX.Text = '1.2'
                $script:tuningFields.AimSensitivityY.Text = '1.3'
                $script:tuningFields.AimReticleLimit.Text = '500'
                $script:tuningFields.AimDeadband.Text = '9'
                $script:tuningFields.AimReturnRate.Text = '15'
                $script:tuningFields.AimSmoothing.Text = '0.12'
                $script:tuningFields.AimRollGain.Text = '0.6'
                $script:tuningFields.AimYawGain.Text = '0.5'
                $script:tuningFields.AimPitchGain.Text = '0.8'
                $script:tuningFields.AimRollMax.Text = '400'
                $script:tuningFields.AimYawMax.Text = '350'
                $script:tuningFields.AimPitchMax.Text = '410'
                $script:tuningFields.AimSlewRate.Text = '8500'
                $script:tuningChecks.AimInvertX.Checked = $true
                $script:tuningChecks.AimInvertY.Checked = $false
                $script:tuningChecks.KeyboardEnabled.Checked = $true
                $script:tuningChecks.MouseLeftEnabled.Checked = $false
                $script:tuningFields.KeyboardInputSource.Text = 'wooting_analog'
                $script:tuningChecks.KeyboardRequireAnalog.Checked = $true
                $script:tuningChecks.BlockSelectedKeys.Checked = $true
                $script:tuningFields.ThrottleUpKey.Text = 'W'
                $script:tuningFields.ThrottleDownKey.Text = 'S'
                $script:tuningFields.ThrottleCutKey.Text = 'Space'
                $script:tuningFields.ThrottleRate.Text = '4096'
                $script:tuningChecks.ThrottleReturnEnabled.Checked = $true
                $script:tuningFields.ThrottleReturnRate.Text = '768'
                $script:tuningFields.YawLeftKey.Text = 'A'
                $script:tuningFields.YawRightKey.Text = 'D'
                $script:tuningFields.YawPulse.Text = '420'
                $script:tuningFields.YawSlewRate.Text = '2048'
                $script:tuningChecks.InvertYaw.Checked = $true
                $script:tuningFields.KeyboardAnalogKeycodeMode.Text = 'virtual_key_translate'
                $script:tuningFields.KeyboardAnalogDeadzone.Text = '0.05'
                $script:tuningFields.KeyboardAnalogCurve.Text = '1.2'
                $script:tuningFields.KeyboardAnalogMin.Text = '0.01'
                $script:tuningFields.KeyboardAnalogMax.Text = '0.98'
                $script:tuningFields.MouseDeviceLeft.Text = 'auto'
                $script:tuningFields.MouseDeviceRight.Text = 'auto'
                $script:tuningChecks.MouseLeftRequireDevice.Checked = $true
                $script:tuningFields.MouseLeftThrottleRate.Text = '0.4'
                $script:tuningChecks.MouseLeftThrottleReturnEnabled.Checked = $true
                $script:tuningFields.MouseLeftThrottleReturnRate.Text = '700'
                $script:tuningFields.MouseLeftYawGain.Text = '24'
                $script:tuningFields.MouseLeftYawMax.Text = '512'
                $script:tuningFields.MouseLeftYawDeadband.Text = '2'
                $script:tuningFields.MouseLeftYawSmoothing.Text = '0.1'
                $script:tuningFields.MouseLeftYawSlewRate.Text = '1200'
                $script:tuningChecks.MouseLeftYawReturnEnabled.Checked = $true
                $script:tuningFields.MouseLeftYawReturnRate.Text = '30'
                $script:tuningFields.MouseLeftYawReturnIdle.Text = '100'
                $script:tuningChecks.MouseLeftYawConstantReturnEnabled.Checked = $true
                $script:tuningFields.MouseLeftYawConstantReturnRate.Text = '10'
                $script:tuningChecks.MouseLeftYawElasticReturnEnabled.Checked = $true
                $script:tuningFields.MouseLeftYawElasticReturnMode.Text = 'linear'
                $script:tuningFields.MouseLeftYawElasticReturnCoefficient.Text = '8'
                $script:tuningFields.MouseLeftYawElasticReturnCurve.Text = '0'
                $script:tuningChecks.MouseLeftYawShapingEnabled.Checked = $true
                $script:tuningFields.MouseLeftYawInputFilter.Text = 'one_euro'
                $script:tuningFields.MouseLeftYawOneEuroMinCutoffHz.Text = '1.3'
                $script:tuningFields.MouseLeftYawOneEuroBeta.Text = '0.09'
                $script:tuningFields.MouseLeftYawOneEuroDcutoffHz.Text = '1.5'
                $script:tuningChecks.MouseLeftYawDespikeEnabled.Checked = $true
                $script:tuningChecks.MouseLeftYawDespikeCountEnabled.Checked = $true
                $script:tuningFields.MouseLeftYawDespikeWindow.Text = '7'
                $script:tuningFields.MouseLeftYawDespikeThresholdSigma.Text = '3.6'
                $script:tuningFields.MouseLeftYawOutputCurve.Text = 'actual'
                $script:tuningFields.MouseLeftYawExpo.Text = '0.2'
                $script:tuningFields.MouseLeftYawActualCenter.Text = '0.44'
                $script:tuningFields.MouseLeftYawActualMax.Text = '0.96'
                $script:tuningFields.MouseLeftYawActualExpo.Text = '0.25'
                $script:tuningFields.MouseLeftYawPositionModel.Text = 'dynamic_gimbal'
                $script:tuningFields.MouseLeftYawGimbalFrequencyHz.Text = '6.5'
                $script:tuningFields.MouseLeftYawGimbalDampingRatio.Text = '1.25'
                $script:tuningFields.MouseLeftYawGimbalInputImpulse.Text = '1.05'
                $script:tuningFields.MouseLeftYawGimbalStaticFriction.Text = '0.1'
                $script:tuningFields.MouseLeftYawGimbalDynamicFriction.Text = '0.2'
                $script:tuningFields.MouseLeftYawGimbalEdgeBumper.Text = '0.3'
                $script:tuningChecks.MouseLeftYawGimbalAntiwindupEnabled.Checked = $true
                $script:tuningFields.MouseLeftYawGimbalAntiwindupStart.Text = '0.91'
                $script:tuningFields.MouseLeftYawGimbalAntiwindupMinGain.Text = '0.21'
                $script:tuningFields.MouseLeftYawInputGainMode.Text = 'adaptive'
                $script:tuningFields.MouseLeftYawAdaptiveSlowGain.Text = '0.71'
                $script:tuningFields.MouseLeftYawAdaptiveFastGain.Text = '1.51'
                $script:tuningFields.MouseLeftYawAdaptiveSpeedLow.Text = '131'
                $script:tuningFields.MouseLeftYawAdaptiveSpeedHigh.Text = '1710'
                $script:tuningFields.MouseLeftYawAdaptiveCurve.Text = '1.2'
                $script:tuningFields.MouseLeftYawAdaptiveTrackerMs.Text = '41'
                $script:tuningFields.MouseLeftYawGateShape.Text = 'axis'
                $script:tuningFields.MouseLeftYawDiagonalScale.Text = '1.0'
                $script:tuningChecks.MouseLeftYawOutputShapingEnabled.Checked = $true
                $script:tuningFields.MouseLeftYawOutputShape.LoadFromTomlValue('[[0.3,0.25,0.1]]')
                $script:tuningChecks.MouseLeftYawReturnShapingEnabled.Checked = $true
                $script:tuningFields.MouseLeftYawReturnShape.LoadFromTomlValue('[[0.7,0.55,0.2]]')
                $script:tuningChecks.MouseLeftInvertThrottle.Checked = $true
                $script:tuningChecks.MouseLeftInvertYaw.Checked = $false
                $script:tuningChecks.MouseLeftSwapAxes.Checked = $true
            } finally {
                $script:loadingProfile = $false
            }
            Save-SelectedProfile
            $savedText = Get-Content -LiteralPath $tempSavePath -Raw
            foreach ($expected in @(
                'name = "overview selftest"',
                'frame_rate_hz = 777',
                'stop_key = "F1"',
                'freeze_key = "F3"',
                'mode = "drone_mouse_aim"',
                'roll_gain = 12.5',
                'pitch_gain = 13.5',
                'max_output = 420',
                'deadband = 5',
                'output_curve = "expo"',
                'expo = 0.25',
                'return_enabled = true',
                'return_rate = 80',
                'return_idle_ms = 120',
                'constant_return_enabled = true',
                'constant_return_rate = 20',
                'elastic_return_enabled = true',
                'elastic_return_mode = "smoothstep"',
                'elastic_return_coefficient = 4.5',
                'elastic_return_curve = 0.7',
                'input_filter = "one_euro"',
                'despike_enabled = true',
                'despike_count_enabled = true',
                'position_model = "dynamic_gimbal"',
                'input_gain_mode = "adaptive"',
                'gate_shape = "octagon"',
                'output_shaping_enabled = true',
                'output_shape_nodes = [[0.25,0.2,0.1]]',
                'return_shape_nodes = [[0.75,0.6,0.2]]',
                'invert_roll = true',
                'invert_pitch = false',
                'swap_axes = true',
                'sensitivity_x = 1.2',
                'reticle_limit = 500',
                'invert_x = true',
                'input_source = "wooting_analog"',
                'require_analog = true',
                'block_selected_keys = true',
                'throttle_up_key = "W"',
                'throttle_down_key = "S"',
                'throttle_cut_key = "Space"',
                'throttle_rate = 4096',
                'throttle_return_enabled = true',
                'throttle_return_rate = 768',
                'yaw_left_key = "A"',
                'yaw_right_key = "D"',
                'yaw_pulse = 420',
                'yaw_slew_rate = 2048',
                'analog_keycode_mode = "virtual_key_translate"',
                'analog_deadzone = 0.05',
                'analog_curve = 1.2',
                'analog_min = 0.01',
                'analog_max = 0.98',
                'left = "auto"',
                'right = "auto"',
                'throttle_rate = 0.4',
                'throttle_return_rate = 700',
                'yaw_gain = 24',
                'yaw_deadband = 2',
                'yaw_smoothing = 0.1',
                'yaw_slew_rate = 1200',
                'yaw_return_rate = 30',
                'yaw_return_idle_ms = 100',
                'yaw_constant_return_rate = 10',
                'yaw_elastic_return_mode = "linear"',
                'yaw_elastic_return_coefficient = 8',
                'yaw_shaping_enabled = true',
                'yaw_input_filter = "one_euro"',
                'yaw_output_curve = "actual"',
                'yaw_actual_center = 0.44',
                'yaw_position_model = "dynamic_gimbal"',
                'yaw_input_gain_mode = "adaptive"',
                'yaw_output_shape_nodes = [[0.3,0.25,0.1]]',
                'yaw_return_shape_nodes = [[0.7,0.55,0.2]]',
                'invert_throttle = true'
            )) {
                if ($savedText.IndexOf($expected, [System.StringComparison]::Ordinal) -lt 0) {
                    throw "V3 save self-test missing: $expected"
                }
            }
            Select-V3EditorTab -Name 'RightAdvanced'
            Refresh-V3EditorSectionLayout
            $testForm.CreateControl()
            $countBounds = $script:tuningChecks.DespikeCountEnabled.Bounds
            $outputBounds = $script:tuningFields.OutputShape.Bounds
            $overlapsOutputShape = (
                ($countBounds.Right -gt ($outputBounds.Left - 8)) -and
                ($countBounds.Left -lt $outputBounds.Right) -and
                ($countBounds.Bottom -gt $outputBounds.Top) -and
                ($countBounds.Top -lt $outputBounds.Bottom)
            )
            if ($overlapsOutputShape) {
                throw 'V3 Right Adv Count despikes overlaps the output shape editor.'
            }
            "right_adv_layout=ok"

            $sampleEnabled = @{}
            foreach ($sampleName in @('ProfileName', 'RollGain', 'DespikeWindow')) {
                $sampleEnabled[$sampleName] = $script:tuningFields[$sampleName].Enabled
            }
            $beforeCountChecked = $script:tuningChecks.DespikeCountEnabled.Checked
            $clickSw = [System.Diagnostics.Stopwatch]::StartNew()
            $script:tuningChecks.DespikeCountEnabled.Checked = -not $beforeCountChecked
            [System.Windows.Forms.Application]::DoEvents()
            $clickSw.Stop()
            if ($script:tuningChecks.DespikeCountEnabled.Checked -eq $beforeCountChecked) {
                throw 'V3 Right Adv Count despikes did not toggle.'
            }
            foreach ($sampleName in $sampleEnabled.Keys) {
                if ($script:tuningFields[$sampleName].Enabled -ne $sampleEnabled[$sampleName]) {
                    throw "V3 Right Adv toggle changed unrelated field enabled state: $sampleName"
                }
            }
            if ($clickSw.ElapsedMilliseconds -gt 500) {
                throw "V3 Right Adv toggle response took too long: $($clickSw.ElapsedMilliseconds) ms"
            }
            $saveSw = [System.Diagnostics.Stopwatch]::StartNew()
            Flush-PendingTuningSave
            $saveSw.Stop()
            if ($saveSw.ElapsedMilliseconds -gt 2000) {
                throw "V3 Right Adv toggle autosave took too long: $($saveSw.ElapsedMilliseconds) ms"
            }
            $toggleSavedText = Get-Content -LiteralPath $tempSavePath -Raw
            $expectedToggleValue = 'despike_count_enabled = {0}' -f (ConvertTo-TomlBool $script:tuningChecks.DespikeCountEnabled.Checked)
            if ($toggleSavedText.IndexOf($expectedToggleValue, [System.StringComparison]::Ordinal) -lt 0) {
                throw "V3 Right Adv toggle autosave missing: $expectedToggleValue"
            }
            "right_adv_toggle=ok click_ms=$($clickSw.ElapsedMilliseconds) save_ms=$($saveSw.ElapsedMilliseconds)"
        } finally {
            $script:editingProfile = $oldEditingProfile
            $script:statusText.Text = $oldStatusText
            Remove-Item -LiteralPath $tempSavePath -Force -ErrorAction SilentlyContinue
        }
        "partial_save=ok"
        Refresh-Profiles
        Apply-ProfileFilter -TargetFileName (Get-DefaultProfileFileName)
        Update-Details
        $script:loadingProfile = $true
        try {
            $script:tuningChecks.MouseRightStickEnabled.Checked = $true
            $script:tuningFields.OutputCurve.Text = 'expo'
            $script:tuningChecks.OutputShapingEnabled.Checked = $false
            $script:tuningChecks.ElasticReturnEnabled.Checked = $true
            $script:tuningChecks.ReturnShapingEnabled.Checked = $false
            Update-RightStickControlState
        } finally {
            $script:loadingProfile = $false
        }
        foreach ($shapeName in @('OutputShape', 'ReturnShape')) {
            if (-not $script:tuningFields.ContainsKey($shapeName) -or -not $script:shapeExpandButtons.ContainsKey($shapeName)) {
                throw "V3 shape editor missing: $shapeName"
            }
            if (-not $script:tuningFields[$shapeName].Enabled -or -not $script:shapeExpandButtons[$shapeName].Enabled) {
                throw "V3 inactive shape editor is not expandable: $shapeName"
            }
        }
        if ($script:tuningFields.OutputShape.ShapingEnabled) {
            throw 'V3 inactive output shape should be editable but visually inactive.'
        }
        "shape_editor_state=ok"
        Update-Details
        Select-V3EditorTab -Name 'RightAdvanced'
        Refresh-V3EditorSectionLayout
        $dialogTestWasVisible = $testForm.Visible
        if (-not $dialogTestWasVisible) {
            $testForm.CreateControl()
            $testForm.Show()
            $testForm.Refresh()
            [System.Windows.Forms.Application]::DoEvents()
        }
        $script:v3ShapeDialogSeen = $false
        $shapeDialogCloser = New-Object System.Windows.Forms.Timer
        $shapeDialogCloser.Interval = 100
        $shapeDialogCloser.Add_Tick({
            foreach ($openForm in @([System.Windows.Forms.Application]::OpenForms)) {
                if ($openForm -ne $script:form -and $openForm.Text -eq 'Stick Output Shaping') {
                    $script:v3ShapeDialogSeen = $true
                    $openForm.Close()
                }
            }
        })
        try {
            $shapeDialogCloser.Start()
            $script:shapeExpandButtons.OutputShape.PerformClick()
        } finally {
            $shapeDialogCloser.Stop()
            $shapeDialogCloser.Dispose()
            if (-not $dialogTestWasVisible) {
                $testForm.Hide()
            }
        }
        if (-not $script:v3ShapeDialogSeen) {
            throw 'V3 shape expand dialog did not open.'
        }
        "shape_expand_click=ok"
        if ($script:profilesList.Items.Count -gt 0) {
            $bitmap = New-Object System.Drawing.Bitmap(320, 60)
            $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
            try {
                $bounds = New-Object System.Drawing.Rectangle(0, 0, 300, 56)
                $args = New-Object System.Windows.Forms.DrawItemEventArgs(
                    $graphics,
                    $script:profilesList.Font,
                    $bounds,
                    0,
                    [System.Windows.Forms.DrawItemState]::Default
                )
                Draw-V3ProfileItem -EventArgs $args
                $selectedArgs = New-Object System.Windows.Forms.DrawItemEventArgs(
                    $graphics,
                    $script:profilesList.Font,
                    $bounds,
                    0,
                    [System.Windows.Forms.DrawItemState]::Selected
                )
                Draw-V3ProfileItem -EventArgs $selectedArgs
            } finally {
                $graphics.Dispose()
                $bitmap.Dispose()
            }
        }
        "ui_build=ok"
        if (-not [string]::IsNullOrWhiteSpace($RenderPreview)) {
            if (-not [string]::IsNullOrWhiteSpace($PreviewTab)) {
                if (-not $script:editorPages.ContainsKey($PreviewTab)) {
                    throw "Unknown V3 preview tab '$PreviewTab'."
                }
                Select-V3EditorTab -Name $PreviewTab
                Refresh-V3EditorSectionLayout
            }
            $previewPath = $RenderPreview
            if (-not [System.IO.Path]::IsPathRooted($previewPath)) {
                $previewPath = Join-Path $root $previewPath
            }
            $previewDir = Split-Path -Parent $previewPath
            if (-not [string]::IsNullOrWhiteSpace($previewDir) -and -not (Test-Path -LiteralPath $previewDir)) {
                New-Item -ItemType Directory -Path $previewDir -Force | Out-Null
            }
            $testForm.CreateControl()
            $testForm.PerformLayout()
            $testForm.Show()
            $testForm.Refresh()
            [System.Windows.Forms.Application]::DoEvents()
            Start-Sleep -Milliseconds 150
            [System.Windows.Forms.Application]::DoEvents()
            $preview = New-Object System.Drawing.Bitmap($testForm.Width, $testForm.Height)
            try {
                $rect = New-Object System.Drawing.Rectangle(0, 0, $preview.Width, $preview.Height)
                $testForm.DrawToBitmap($preview, $rect)
                $preview.Save($previewPath, [System.Drawing.Imaging.ImageFormat]::Png)
                "preview=$previewPath"
            } finally {
                $preview.Dispose()
                $testForm.Hide()
            }
        }
    } finally {
        $testForm.Dispose()
    }
    exit 0
}

Initialize-LauncherExceptionHandlers
[System.Windows.Forms.Application]::EnableVisualStyles()
$launcherForm = Build-LauncherForm
Refresh-Profiles
[System.Windows.Forms.Application]::Run($launcherForm)
