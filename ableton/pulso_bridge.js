autowatch = 1;
outlets = 2;
var releaseTask = null;
var instrumentMap = {
    "kick_drum": "Drum Rack / Kick",
    "snare_clap": "Drum Rack / Snare-Clap",
    "hi_hats": "Drum Rack / Hats",
    "timpani": "Orchestral Percussion / Timpani",
    "taiko_ensemble": "Orchestral Percussion / Taiko",
    "latin_percussion": "Drum Rack / Latin Percussion",
    "shakers": "Drum Rack / Shakers",
    "cymbals": "Drum Rack / Cymbals",
    "piano": "Piano & Keys",
    "harp": "Harp",
    "violin_1": "Strings / Violin",
    "violin_2": "Strings / Violin",
    "viola": "Strings / Viola",
    "cello": "Strings / Cello",
    "contrabass": "Strings / Contrabass",
    "flute": "Woodwinds / Flute",
    "oboe": "Woodwinds / Oboe",
    "clarinet": "Woodwinds / Clarinet",
    "bass_clarinet": "Woodwinds / Bass Clarinet",
    "bassoon": "Woodwinds / Bassoon",
    "french_horns": "Brass / French Horn",
    "trumpets": "Brass / Trumpet",
    "trombones": "Brass / Trombone",
    "choir": "Choir",
    "mallets": "Mallets",
    "electric_bass": "Bass / Electric Bass",
    "sub_synth": "Synth Bass",
    "analog_pad": "Pad",
    "poly_synth": "Poly Synth",
    "lead_synth": "Lead Synth",
    "guitar": "Guitar",
    "ambient_texture": "Texture"
};

function bang() {
    requestVariation();
}

function msg_int(value) {
    if (value !== 0) requestVariation();
}

function sendByte(value) {
    outlet(0, value);
}

function requestVariation() {
    // Reserved control command: note 127, MIDI channel 16.
    sendByte(159);
    sendByte(127);
    sendByte(100);
    releaseTask = new Task(function () {
        sendByte(143);
        sendByte(127);
        sendByte(0);
    }, this);
    releaseTask.schedule(40);
    outlet(1, "variation requested");
}

function getlivecontext() {
    try {
        var song = new LiveAPI("live_set");
        var tempo = song.get("tempo");
        outlet(1, "tempo " + tempo);
    } catch (error) {
        outlet(1, "Live API unavailable: " + error.message);
    }
}

// Receives a manifest entry from a future drag/import host and resolves it to one
// stable rack category. It deliberately does not guess or silently load an unrelated
// Ableton preset when a user library does not contain the requested rack.
function resolvepart(catalogId) {
    var resolved = instrumentMap[catalogId];
    outlet(1, resolved ? "rack " + catalogId + " " + resolved : "rack_missing " + catalogId);
}

function importmanifest(path) {
    try {
        var file = new File(path, "read");
        if (!file.isopen) {
            outlet(1, "manifest_error cannot_open " + path);
            return;
        }
        var text = "";
        while (file.position < file.eof) text += file.readline();
        file.close();
        var manifest = JSON.parse(text);
        if (!manifest.parts || !(manifest.parts instanceof Array)) {
            outlet(1, "manifest_error invalid_schema");
            return;
        }
        outlet(1, "manifest_begin " + manifest.parts.length);
        for (var index = 0; index < manifest.parts.length; ++index) {
            var part = manifest.parts[index];
            var rack = instrumentMap[part.catalog_id];
            outlet(1, rack
                ? ["assign", part.track_name, part.catalog_id, rack]
                : ["rack_missing", part.track_name, part.catalog_id]);
        }
        outlet(1, "manifest_end");
    } catch (error) {
        outlet(1, "manifest_error " + error.message);
    }
}
