#include "inactivitywatcher.h"

#include <QCoreApplication>
#include <QEvent>

InactivityWatcher::InactivityWatcher(QObject *parent)
    : QObject(parent)
{
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, &InactivityWatcher::idle);
    QCoreApplication::instance()->installEventFilter(this);
}

void InactivityWatcher::setInterval(int milliseconds)
{
    m_interval = milliseconds;
    resume();
}

void InactivityWatcher::pause()
{
    m_paused = true;
    m_timer.stop();
}

void InactivityWatcher::resume()
{
    m_paused = false;
    if (m_interval > 0)
        m_timer.start(m_interval);
    else
        m_timer.stop();
}

bool InactivityWatcher::eventFilter(QObject *watched, QEvent *event)
{
    if (!m_paused && m_interval > 0) {
        switch (event->type()) {
        case QEvent::KeyPress:
        case QEvent::MouseButtonPress:
        case QEvent::MouseMove:
        case QEvent::Wheel:
            m_timer.start(m_interval);
            break;
        default:
            break;
        }
    }
    return QObject::eventFilter(watched, event);
}
