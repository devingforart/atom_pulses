param(
    [Parameter(Mandatory = $true)]
    [string]$MidiPath,
    [string]$PlanPath
)

$ErrorActionPreference = 'Stop'

Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;

public sealed class PulsoMidiNote {
    public int Track;
    public string TrackName = "";
    public int Channel;
    public int Pitch;
    public int Velocity;
    public long Start;
    public long End;
}

public sealed class PulsoMidiData {
    public int TicksPerQuarter;
    public List<PulsoMidiNote> Notes = new List<PulsoMidiNote>();
}

public static class PulsoMidiReader {
    private static int U16(BinaryReader r) {
        return (r.ReadByte() << 8) | r.ReadByte();
    }
    private static long U32(BinaryReader r) {
        return ((long)r.ReadByte() << 24) | ((long)r.ReadByte() << 16) |
               ((long)r.ReadByte() << 8) | r.ReadByte();
    }
    private static long Var(BinaryReader r) {
        long value = 0;
        byte b;
        do { b = r.ReadByte(); value = (value << 7) | (uint)(b & 0x7f); }
        while ((b & 0x80) != 0);
        return value;
    }
    private static string Key(int channel, int pitch) { return channel + ":" + pitch; }

    public static PulsoMidiData Read(string path) {
        var result = new PulsoMidiData();
        using (var r = new BinaryReader(File.OpenRead(path))) {
            if (Encoding.ASCII.GetString(r.ReadBytes(4)) != "MThd")
                throw new InvalidDataException("Missing MIDI header");
            var headerLength = U32(r);
            U16(r);
            var tracks = U16(r);
            var division = U16(r);
            if ((division & 0x8000) != 0) throw new InvalidDataException("SMPTE MIDI is unsupported");
            result.TicksPerQuarter = division;
            if (headerLength > 6) r.ReadBytes((int)headerLength - 6);

            for (var trackIndex = 0; trackIndex < tracks; ++trackIndex) {
                if (Encoding.ASCII.GetString(r.ReadBytes(4)) != "MTrk")
                    throw new InvalidDataException("Missing MIDI track chunk");
                var trackLength = U32(r);
                var end = r.BaseStream.Position + trackLength;
                var tick = 0L;
                var running = -1;
                var name = "Track " + trackIndex;
                var active = new Dictionary<string, Stack<PulsoMidiNote>>();
                while (r.BaseStream.Position < end) {
                    tick += Var(r);
                    var first = r.ReadByte();
                    int status;
                    int data1 = -1;
                    if (first < 0x80) { status = running; data1 = first; }
                    else { status = first; if (status < 0xf0) running = status; }
                    if (status < 0) throw new InvalidDataException("Invalid running status");
                    if (status == 0xff) {
                        var type = r.ReadByte();
                        var length = Var(r);
                        var payload = r.ReadBytes((int)length);
                        if (type == 0x03) name = Encoding.UTF8.GetString(payload);
                        continue;
                    }
                    if (status == 0xf0 || status == 0xf7) { r.ReadBytes((int)Var(r)); continue; }
                    var kind = status & 0xf0;
                    var channel = status & 0x0f;
                    if (data1 < 0) data1 = r.ReadByte();
                    var oneByte = kind == 0xc0 || kind == 0xd0;
                    var data2 = oneByte ? 0 : r.ReadByte();
                    if (kind == 0x90 && data2 > 0) {
                        var note = new PulsoMidiNote { Track = trackIndex, TrackName = name,
                            Channel = channel, Pitch = data1, Velocity = data2, Start = tick, End = tick };
                        var key = Key(channel, data1);
                        Stack<PulsoMidiNote> stack;
                        if (!active.TryGetValue(key, out stack)) { stack = new Stack<PulsoMidiNote>(); active[key] = stack; }
                        stack.Push(note);
                    } else if (kind == 0x80 || (kind == 0x90 && data2 == 0)) {
                        Stack<PulsoMidiNote> stack;
                        if (active.TryGetValue(Key(channel, data1), out stack) && stack.Count > 0) {
                            var note = stack.Pop(); note.End = Math.Max(note.Start + 1, tick); result.Notes.Add(note);
                        }
                    }
                }
                foreach (var stack in active.Values)
                    while (stack.Count > 0) { var note = stack.Pop(); note.End = Math.Max(note.Start + 1, tick); result.Notes.Add(note); }
                foreach (var note in result.Notes.Where(n => n.Track == trackIndex)) note.TrackName = name;
                r.BaseStream.Position = end;
            }
        }
        return result;
    }
}
'@

$resolvedMidi = (Resolve-Path -LiteralPath $MidiPath).Path
$midi = [PulsoMidiReader]::Read($resolvedMidi)
$ppq = $midi.TicksPerQuarter
$barTicks = 4 * $ppq

$sections = @()
if ($PlanPath -and (Test-Path -LiteralPath $PlanPath)) {
    $plan = Get-Content -LiteralPath $PlanPath -Raw -Encoding UTF8 | ConvertFrom-Json
    $sections = @($plan.sections | ForEach-Object {
        [pscustomobject]@{ name = $_.name; start = [int]$_.start_bar; bars = [int]$_.bars }
    })
}

function Get-Median([double[]]$Values) {
    if ($Values.Count -eq 0) { return 0.0 }
    $sorted = @($Values | Sort-Object)
    $middle = [math]::Floor($sorted.Count / 2)
    if (($sorted.Count % 2) -eq 1) { return [double]$sorted[$middle] }
    return ([double]$sorted[$middle - 1] + [double]$sorted[$middle]) / 2.0
}

$tracks = @($midi.Notes | Group-Object Track | ForEach-Object {
    $notes = @($_.Group | Sort-Object Start,Pitch)
    $activeBars = @($notes | ForEach-Object { [math]::Floor($_.Start / $barTicks) } | Sort-Object -Unique)
    $fingerprints = @{}
    foreach ($bar in $activeBars) {
        $items = @($notes | Where-Object { [math]::Floor($_.Start / $barTicks) -eq $bar } | ForEach-Object {
            $onset = $_.Start % $barTicks
            $duration = $_.End - $_.Start
            "${onset}:$($_.Pitch % 12):$duration"
        })
        $fingerprint = $items -join ','
        if (!$fingerprints.ContainsKey($fingerprint)) { $fingerprints[$fingerprint] = 0 }
        $fingerprints[$fingerprint]++
    }
    $mostRepeated = if ($fingerprints.Count) { ($fingerprints.Values | Measure-Object -Maximum).Maximum } else { 0 }
    $onsets = @($notes | Group-Object Start)
    $chordAttacks = @($onsets | Where-Object Count -ge 2)
    $sectionCounts = [ordered]@{}
    foreach ($section in $sections) {
        $startTick = $section.start * $barTicks
        $endTick = ($section.start + $section.bars) * $barTicks
        $sectionCounts[$section.name] = @($notes | Where-Object { $_.Start -ge $startTick -and $_.Start -lt $endTick }).Count
    }
    [pscustomobject]@{
        track = [int]$_.Name
        name = $notes[0].TrackName
        notes = $notes.Count
        first_bar = if ($activeBars.Count) { $activeBars[0] } else { 0 }
        last_bar = if ($activeBars.Count) { $activeBars[-1] } else { 0 }
        active_bars = $activeBars.Count
        activity_ratio = [math]::Round($activeBars.Count / 192.0, 3)
        pitch_min = ($notes.Pitch | Measure-Object -Minimum).Minimum
        pitch_max = ($notes.Pitch | Measure-Object -Maximum).Maximum
        unique_pitches = @($notes.Pitch | Sort-Object -Unique).Count
        unique_pitch_classes = @(($notes.Pitch | ForEach-Object { $_ % 12 }) | Sort-Object -Unique).Count
        median_duration_beats = [math]::Round((Get-Median @($notes | ForEach-Object { ($_.End - $_.Start) / [double]$ppq })), 3)
        exact_sixteenth_grid_ratio = [math]::Round(@($notes | Where-Object { ($_.Start % ($ppq / 4)) -eq 0 }).Count / [double]$notes.Count, 3)
        chord_attack_ratio = [math]::Round($chordAttacks.Count / [double][math]::Max(1, $onsets.Count), 3)
        exact_bar_repetition_ratio = [math]::Round($mostRepeated / [double][math]::Max(1, $activeBars.Count), 3)
        section_notes = [pscustomobject]$sectionCounts
    }
})

$sectionSummary = @($sections | ForEach-Object {
    $startTick = $_.start * $barTicks
    $endTick = ($_.start + $_.bars) * $barTicks
    $sectionNotes = @($midi.Notes | Where-Object { $_.Start -ge $startTick -and $_.Start -lt $endTick })
    $activeParts = @($sectionNotes.Track | Sort-Object -Unique).Count
    [pscustomobject]@{
        name = $_.name
        start_bar = $_.start
        bars = $_.bars
        notes = $sectionNotes.Count
        notes_per_bar = [math]::Round($sectionNotes.Count / [double][math]::Max(1, $_.bars), 2)
        active_parts = $activeParts
    }
})

[pscustomobject]@{
    midi = $resolvedMidi
    ticks_per_quarter = $ppq
    notes = $midi.Notes.Count
    tracks = $tracks
    sections = $sectionSummary
} | ConvertTo-Json -Depth 8
