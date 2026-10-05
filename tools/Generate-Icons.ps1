$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$voiceResourceRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\resources'))
function Add-VoiceRoundedRectangle($voicePath, [single]$x, [single]$y, [single]$width, [single]$height, [single]$radius) {
    $diameter = 2 * $radius
    $voicePath.AddArc($x, $y, $diameter, $diameter, 180, 90)
    $voicePath.AddArc($x + $width - $diameter, $y, $diameter, $diameter, 270, 90)
    $voicePath.AddArc($x + $width - $diameter, $y + $height - $diameter, $diameter, $diameter, 0, 90)
    $voicePath.AddArc($x, $y + $height - $diameter, $diameter, $diameter, 90, 90)
    $voicePath.CloseFigure()
}
foreach ($voiceIconName in 'crystal-voice.png', 'iconDarkMode.png', 'iconLightMode.png') {
    $voiceBitmap = [Drawing.Bitmap]::new(256, 256)
    $voiceGraphics = [Drawing.Graphics]::FromImage($voiceBitmap)
    $voiceGraphics.SmoothingMode = [Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $voiceGraphics.Clear([Drawing.Color]::Transparent)
    if ($voiceIconName -eq 'crystal-voice.png') {
        $voicePath = [Drawing.Drawing2D.GraphicsPath]::new()
        Add-VoiceRoundedRectangle $voicePath 8 8 240 240 64
        $voiceBrush = [Drawing.SolidBrush]::new([Drawing.ColorTranslator]::FromHtml('#69dfc6'))
        $voiceGraphics.FillPath($voiceBrush, $voicePath)
        $voiceBrush.Dispose(); $voicePath.Dispose()
    }
    $voiceBarColour = if ($voiceIconName -eq 'iconDarkMode.png') { '#69dfc6' } else { '#10151c' }
    $voiceBrush = [Drawing.SolidBrush]::new([Drawing.ColorTranslator]::FromHtml($voiceBarColour))
    $voiceHeights = 54, 90, 132, 90, 54
    for ($voiceBar = 0; $voiceBar -lt 5; $voiceBar++) {
        $voicePath = [Drawing.Drawing2D.GraphicsPath]::new()
        Add-VoiceRoundedRectangle $voicePath (65 + 28 * $voiceBar) (128 - $voiceHeights[$voiceBar] / 2) 14 $voiceHeights[$voiceBar] 7
        $voiceGraphics.FillPath($voiceBrush, $voicePath)
        $voicePath.Dispose()
    }
    $voiceBitmap.Save((Join-Path $voiceResourceRoot $voiceIconName), [Drawing.Imaging.ImageFormat]::Png)
    $voiceBrush.Dispose(); $voiceGraphics.Dispose(); $voiceBitmap.Dispose()
}
