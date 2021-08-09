#ifndef MESSAGES_H
#define MESSAGES_H

#include <QString>

/// 5 level priority list
/// Categorices message priorities
enum MsgPriority {
    PriorityVeryLow,
    PriorityLow,
    PriorityMedium,
    PriorityHigh,
    PriorityVeryHigh
};

/// Message notification struct
/// Contains all necesary data for a module to send notifications to the user
struct ModuleMsg {
    QString title;
    QString message;
    MsgPriority priority;
};

#endif // MESSAGES_H
