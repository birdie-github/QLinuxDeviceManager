#pragma once
#include "device.h"

struct AudioProfile {
    QString name, description;
    quint32 playback = 0, recording = 0;
    bool available = false, active = false;
};
struct AudioPort {
    QString name, description;
    int direction = 0; // PulseAudio direction mask: output=1, input=2.
    int availability = 0; // Unknown=0, no=1, yes=2.
};
struct AudioEndpoint {
    QString name, description, port, format, channelMap;
    quint32 rate = 0;
    int channels = 0, state = -1, resolutionBits = 0;
    bool input = false, isDefault = false, muted = false;
};
struct AudioCard {
    quint32 index = 0;
    QString name, description, path, driver, mixer, activeProfile;
    QVector<AudioProfile> profiles;
    QVector<AudioPort> ports;
    QVector<AudioEndpoint> endpoints;
};
struct AudioProperties {
    bool applicable = false;
    Attribute status;
    QString diagnostic, server, version;
    QVector<AudioCard> cards;
};

// Explicit Properties requests only. Queries the current local desktop session;
// never opens audio streams or starts a server. Owned results cross thread boundaries.
AudioProperties collectAudioProperties(const Device &device);
