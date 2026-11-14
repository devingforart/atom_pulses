#pragma once
#include <juce_core/juce_core.h>

namespace pulso::plugin::structured {
// HTTP 200 also represents a queued/in-progress background response.
// Never send partial content into musical validation or repair.
inline bool ready(const juce::var& envelope,bool connected,int httpStatus,
                  bool timedOut,bool cancelled) {
    return connected && httpStatus>=200 && httpStatus<300 && !timedOut && !cancelled &&
        envelope.isObject() && envelope.getProperty("status",{}).toString()=="completed";
}
}
