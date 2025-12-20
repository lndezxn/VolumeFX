param(
    [string]$Output = "assets/musics/test_tone.wav",
    [int]$SampleRate = 44100,
    [double]$DurationSeconds = 5.0,
    [double]$Frequency = 220.0,
    [double]$BaseAmplitude = 0.35,
    [double]$ModDepth = 0.6,
    [double]$ModFrequency = 1.0
)

$durationSeconds = [math]::Max($DurationSeconds, 0.1)
$sampleCount = [int]([math]::Round($SampleRate * $durationSeconds))
$depth = [math]::Min([math]::Max($ModDepth, 0.0), 0.95)
$parentDir = Split-Path -Path $Output -Parent
if ($parentDir -and -not (Test-Path $parentDir)) {
    New-Item -ItemType Directory -Path $parentDir -Force | Out-Null
}

$fs = $null
$bw = $null
try {
    $fs = [System.IO.File]::Create($Output)
    $bw = New-Object System.IO.BinaryWriter($fs)

    function WriteAscii {
        param([string]$text)
        $bw.Write([System.Text.Encoding]::ASCII.GetBytes($text))
    }

    WriteAscii "RIFF"
    $bw.Write([int](36 + $sampleCount * 2))
    WriteAscii "WAVE"
    WriteAscii "fmt "
    $bw.Write([int]16)
    $bw.Write([int16]1)
    $bw.Write([int16]1)
    $bw.Write([int]$SampleRate)
    $bw.Write([int]($SampleRate * 2))
    $bw.Write([int16]2)
    $bw.Write([int16]16)
    WriteAscii "data"
    $bw.Write([int]($sampleCount * 2))

    for ($i = 0; $i -lt $sampleCount; $i++) {
        $t = $i / [double]$SampleRate
        $lfo = 0.5 * (1.0 + [math]::Sin(2 * [math]::PI * $ModFrequency * $t))
        $amp = $BaseAmplitude * ((1.0 - $depth) + $depth * $lfo)
        $amp = [math]::Min([math]::Max($amp, 0.0), 1.0)
        $value = [int][math]::Round([math]::Sin(2 * [math]::PI * $Frequency * $t) * 32767 * $amp)
        $bw.Write([int16]$value)
    }
}
finally {
    if ($bw) { $bw.Dispose() }
    if ($fs) { $fs.Dispose() }
}
