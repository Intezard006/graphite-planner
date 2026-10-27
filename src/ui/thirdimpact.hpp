// Third Impact: the day scrapped. A sea of LCL (random characters in orange) rises from the
// bottom of the window, "THIRD IMPACT" stands on it while the journal under it is wiped
// (`atFlood`, called once, when the sea covers everything), then it drains away and leaves the
// empty page. About 3.5 s at 30 frames a second, painting only while it runs; a click or a key
// skips to the end (the wipe still happens).
#pragma once

#include <QElapsedTimer>
#include <QImage>
#include <QTimer>
#include <QWidget>
#include <functional>

namespace graphite {

class ThirdImpact : public QWidget {
    Q_OBJECT
public:
    ThirdImpact(QWidget* host, const QString& caption, std::function<void()> atFlood);
    static constexpr int kRise = 1400, kHold = 1300, kDrain = 900;  // ms
    void skip();  // straight to the end (tests, a click, a key)

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    bool eventFilter(QObject*, QEvent*) override;

private:
    void makeSea();     // the sea: a full grid of characters, drawn once
    void churn(int n);  // n random cells get a new character
    void flood();
    void finish();
    qreal level(qint64 ms) const;  // the sea's surface, 0 = the top edge, 1 = the bottom

    QString caption_;
    std::function<void()> atFlood_;
    bool flooded_ = false;
    QElapsedTimer clock_;
    QTimer frame_;
    QImage sea_;
    QFont glyphFont_;
    int cellW_ = 8, cellH_ = 14, cols_ = 0, rows_ = 0;
    quint32 seed_ = 0x9e3779b9u;
    quint32 next();
};

}  // namespace graphite
