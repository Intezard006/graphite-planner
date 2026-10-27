// Progress bars: "{progress 45/120 pages from 30 by 5}" anywhere in a block draws a segmented
// gauge where it stands. The value runs from `from` (default 0) to the end; Alt+= / Alt+- in the
// block move it one step (`by`, default 1). The unit word is optional ("pages", "%", "problems").
//
//   TODO Read chapter 12 {progress 0/38 pages by 2}
//   ## DOING Problem set 4 {progress 3/10}
//
// On a task the marker follows (with the Tasks settings): the first step forward turns TODO into
// DOING, reaching the end turns it DONE, stepping back from the end opens it again.
#pragma once

#include <QImage>
#include <QKeyEvent>
#include <QString>

namespace graphite {

class Theme;

// Alt+= / Alt+- (and Alt++): +1 / -1, else 0. On a Mac Option+= types "≠" and Option+- "–", so
// key() is not Key_Equal there; the physical key (nativeVirtualKey) and the typed character are
// checked too. Linux: exactly Key_Equal / Key_Plus / Key_Minus with Alt.
int progressStepKey(const QKeyEvent* e);

struct Progress {
    double value = 0, end = 10, start = 0, step = 1;
    QString unit;
    int at = -1, length = 0;  // where the token is in the text
    int valueAt = -1, valueLength = 0;  // where its value is (a step rewrites only that)
    double ratio() const;     // 0..1
    bool full() const { return value >= end; }
};

// The first "{progress …}" in `text`; false when there is none (or it does not read).
bool findProgress(const QString& text, Progress* p);

// The token for `p`, as it is written: "{progress 45/120 pages from 30 by 5}" (defaults left out).
QString progressToken(const Progress& p);

// `text` with its progress moved `steps` steps (clamped to start..end). `finish` / `start`: let
// a task marker follow (see above). Returns `text` unchanged when there is no progress.
QString stepProgress(const QString& text, int steps, bool finish = true, bool start = true);

// "45 / 120 PAGES", "3 / 10"
QString progressNumbers(const Progress& p);

// The status line after a step: "Sync 38%  ▮▮▮▯▯▯▯▯▯▯  45 / 120 PAGES", or at the end
// "Sync ratio 100%  ·  Read chapter 12" ("Complete · …" without immersion). Empty: no progress.
QString progressMessage(const QString& text, bool eva);

// The gauge: `cells` segments, `h` px tall; warm while running, accent when full.
QImage drawProgress(const Progress& p, const Theme& th, qreal dpr, int w, int h);

}  // namespace graphite
