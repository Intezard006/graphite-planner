// Graphite Essentials: a book of pages Graphite supplies to every tree (the lab tables guide,
// the week's timetable, and whatever comes later). Each page is written once per tree, the
// first time the tree is opened by a Graphite that has it; after that the page is yours:
// edits stay, and a deleted page is not brought back (Settings › Essentials restores them).
// A page still exactly as an earlier Graphite wrote it (`previous`) is brought up to date.
#pragma once

#include <QList>
#include <QString>
#include <QStringList>

namespace graphite {

QString essentialsBook();  // the folder name, "Graphite Essentials"

struct EssentialPage {
    QString title;  // the file is <title>.md
    QString text;
    QStringList previous;  // MD5 (hex) of the texts earlier versions supplied
};
QList<EssentialPage> essentialPages();

}  // namespace graphite
