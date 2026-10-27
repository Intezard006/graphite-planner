// Background blur strength for transparent mode, set through Hyprland.
//
// Hyprland 0.56 has no per-window blur size, only the global decoration.blur.size/passes. So
// while transparent mode is on, the chosen level is applied globally (every blurred window
// follows), and the user's own values are put back when it is turned off or the app quits.
// The originals are saved first, so a crash is repaired on the next start.
//
// Changes go out as `hyprctl eval 'hl.config({...})'`, detached and debounced: nothing runs
// while idle. Only on a real Wayland session under Hyprland (never in headless tests).
#pragma once

#include <QObject>
#include <QTimer>
#include <QWidget>

namespace graphite {

class BlurControl : public QObject {
    Q_OBJECT
public:
    static constexpr int kMaxLevel = 10;

    explicit BlurControl(QObject* parent = nullptr);
    ~BlurControl() override;

    bool available() const { return available_; }
    int level() const { return level_; }
    void setLevel(int level);   // 0..kMaxLevel; applied live when active, remembered
    void setActive(bool on);    // transparent mode on/off
    bool active() const { return active_; }

    // What a level means to Hyprland (size, passes): public for tests.
    static void levelToBlur(int level, int* size, int* passes);

    // Puts back values left behind by a run that did not exit cleanly.
    static void recoverAfterCrash();

signals:
    void levelChanged(int level);

private:
    void push();  // send the current level (or the originals) to Hyprland
    static bool readOption(const char* name, int* value);

    bool available_ = false;
    bool active_ = false;
    int level_ = 5;
    int origSize_ = -1, origPasses_ = -1;
    QTimer debounce_;
};

// The slider: a segmented meter, "BLUR ▮▮▮▮▮▯▯▯▯▯▯". Click, drag or scroll.
class BlurMeter : public QWidget {
    Q_OBJECT
public:
    explicit BlurMeter(QWidget* parent = nullptr);
    void setLevel(int level);
    int level() const { return level_; }
    QSize sizeHint() const override;

signals:
    void levelPicked(int level);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;

private:
    int levelAt(int x) const;
    int segmentsLeft() const;
    int level_ = 5;
};

}  // namespace graphite
