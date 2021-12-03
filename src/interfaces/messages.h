#ifndef MESSAGES_H
#define MESSAGES_H

#include <QString>

/// 5 level priority list
/// Categorices message priorities
enum MsgPriority {
    PriorityVeryLow,
    PriorityLow,
    PriorityNormal,
    PriorityHigh,
    PriorityVeryHigh
};

/// Message notification struct
/// Contains all necesary data for a module to send notifications to the user
struct ModuleMsg {
    ModuleMsg(QString sender, QString ID, QString title, QString body
              , MsgPriority prio = MsgPriority::PriorityNormal, double lifetime = -1) {
        senderName = sender;
        id = ID;
        priority = prio;
        lifetime_seconds = lifetime;
        this->title = title;
        message = body;
    }
    // Message metadata
    QString senderName;         // the combination of sender name
    QString id;                 // and id must be unique
    MsgPriority priority;
    double lifetime_seconds;    // how long should this message last
                                // negative values means forever

    // Message content
    QString title;
    QString message;
};

#endif // MESSAGES_H
