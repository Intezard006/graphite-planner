#include "notes/essentials.hpp"

#include "planner/timetable.hpp"

#include <QFile>

namespace graphite {

QString essentialsBook() { return QStringLiteral("Graphite Essentials"); }

QList<EssentialPage> essentialPages() {
    auto resource = [](const QString& name) {
        QFile f(":/essentials/" + name);
        return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
    };
    return {{"Lab tables", resource("Lab tables.md"), {"641a98801c672081cb30cb5298d6aabc"}},  // 0.20: "= plot"
            {"Graphs", resource("Graphs.md"), {}},
            {"Timetable", timetableTemplate(), {}}};
}

}  // namespace graphite
