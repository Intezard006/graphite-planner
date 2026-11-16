#pragma once
// The month calendar that /scheduled and /deadline open at the cursor (and a click on a
// block's date label). Painted by hand in the theme's colours; the day marks (exams, deadlines,
// scheduled tasks) are handed over when it opens, nothing is computed while it shows.
#include <QDate>
#include <QHash>
#include <QWidget>

#include "planner/tasks.hpp"

class QKeyEvent;

namespace graphite {

class CalendarPopup : public QWidget {
    Q_OBJECT
public:
    explicit CalendarPopup(QWidget* parent);
    // kind: "SCHEDULED" or "DEADLINE" (the title); start: the day selected first.
    void open(const QString& kind, const QDate& start, const QHash<QDate, DayMarks>& marks);
    // A key from the editor while the calendar shows: true when it was used (nearly all are).
    bool handleKey(QKeyEvent* e);

    QDate selected() const { return sel_; }
    QDate month() const { return QDate(sel_.year(), sel_.month(), 1); }
    QRect dayRect(const QDate& d) const;  // widget coordinates; null when not in the grid
    QRect chipRect(int k) const;          // the quick picks: Today, Tomorrow, Sat, Next Mon, +1 week
    QDate chipDate(int k) const;
    QString footer() const;               // what the line under the grid says

signals:
    void picked(const QDate& day);
    void cancelled();

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void leaveEvent(QEvent*) override;
    void wheelEvent(QWheelEvent* e) override;

private:
    void select(const QDate& d);
    void relayout();
    QDate gridStart() const;  // the Monday on or before the 1st
    QRect arrowRect(int dir) const;  // -1 previous month, +1 next
    void typed();             // apply typed_ as far as it reads

    QString kind_;
    QDate sel_;
    QDate hover_;
    QHash<QDate, DayMarks> marks_;
    QString typed_;
    int cell_ = 32, pad_ = 12, headH_ = 24, chipsH_ = 24, weekH_ = 20, footH_ = 40;
};

}  // namespace graphite
