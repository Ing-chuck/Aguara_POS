#ifndef INACTIVITYWATCHER_H
#define INACTIVITYWATCHER_H

#include <QObject>
#include <QTimer>

/// Emits idle() when nobody has touched the keyboard or mouse for a while,
/// so an unattended till doesn't stay open.
class InactivityWatcher : public QObject
{
    Q_OBJECT
public:
    explicit InactivityWatcher(QObject *parent = nullptr);

    /// 0 turns it off.
    void setInterval(int milliseconds);
    void pause();
    void resume();

signals:
    void idle();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QTimer m_timer;
    int m_interval = 0;
    bool m_paused = false;
};

#endif // INACTIVITYWATCHER_H
