autowatch = 1;
outlets = 2;
var releaseTask = null;

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
