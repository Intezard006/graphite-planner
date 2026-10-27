#include "ui/operation.hpp"

#include <QRegularExpression>
#include <QStringList>

namespace graphite {

int parseDuration(const QString& raw) {
    const QString t = raw.trimmed().toLower();
    if (t.isEmpty()) return 0;
    long long seconds = 0;
    static const QRegularExpression clock(R"(^(\d+):(\d{1,2})(?::(\d{1,2}))?$)");
    static const QRegularExpression parts(R"(^(?:\s*(\d+)\s*(h|hr|hrs|hours?|m|min|mins|minutes?|s|sec|secs|seconds?))+\s*$)");
    if (t.contains(QRegularExpression("^\\d+$"))) {
        seconds = t.toLongLong() * 60;  // a bare number is minutes
    } else if (const QRegularExpressionMatch c = clock.match(t); c.hasMatch()) {
        if (c.capturedLength(3)) seconds = c.captured(1).toLongLong() * 3600 + c.captured(2).toLongLong() * 60 + c.captured(3).toLongLong();
        else seconds = c.captured(1).toLongLong() * 60 + c.captured(2).toLongLong();
    } else if (parts.match(t).hasMatch()) {
        static const QRegularExpression one(R"((\d+)\s*([a-z]+))");
        for (auto it = one.globalMatch(t); it.hasNext();) {
            const QRegularExpressionMatch m = it.next();
            const long long n = m.captured(1).toLongLong();
            const QChar unit = m.captured(2).front();
            seconds += unit == 'h' ? n * 3600 : unit == 'm' ? n * 60 : n;
        }
    } else {
        return 0;
    }
    return seconds > 0 && seconds <= 12 * 3600 ? static_cast<int>(seconds) : 0;
}

QString formatDuration(int s) {
    if (s < 0) s = 0;
    const int h = s / 3600, m = (s % 3600) / 60, sec = s % 60;
    if (h > 0) return QString("%1:%2:%3").arg(h).arg(m, 2, 10, QChar('0')).arg(sec, 2, 10, QChar('0'));
    return QString("%1:%2").arg(m).arg(sec, 2, 10, QChar('0'));
}

QString timerCommand(const QString& line, int* start) {
    static const QRegularExpression re(R"((?:^|\s)(/timer(?:\s+([^\n]*))?)\s*$)", QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch m = re.match(line);
    if (!m.hasMatch()) return QString();
    if (start) *start = static_cast<int>(m.capturedStart(1));
    const QString args = m.captured(2).trimmed();
    return args.isEmpty() ? QString("") : args;  // "" (not null): a bare "/timer"
}

}  // namespace graphite
