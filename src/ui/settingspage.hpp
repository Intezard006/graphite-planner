// Settings, as a page: it takes the place of the open page (Ctrl+, / Esc go back) and lists
// the options in sections, under the same header the notes have.
//
// Each row is a heading, a segmented slider, a set of choices, an action (Enter), or plain
// information. ↑/↓ choose a row, ←/→ change it (applied at once), Enter runs an action. The
// rows, and what they do, come from the window.
#pragma once

#include <QScrollArea>
#include <QStringList>
#include <functional>

namespace graphite {

struct SettingRow {
    enum Kind { Heading, Slider, Choice, Action, Info } kind = Choice;
    QString name;
    QStringList choices;                 // Choice
    int steps = 0;                       // Slider: values 0 .. steps
    std::function<int()> get;            // Slider / Choice: the current value
    std::function<void(int)> set;
    std::function<QString(int)> label;   // Slider: the value as text ("11.5 pt")
    std::function<void()> run;           // Action
    std::function<QString()> value;      // Info / Action: text in the control column
    QString note;                        // small print under the row
};

class SettingsPage : public QScrollArea {
    Q_OBJECT
public:
    explicit SettingsPage(QWidget* parent = nullptr);
    void setRows(const QList<SettingRow>& rows);
    int currentRow() const;  // index into the rows (headings included)
    void select(int row);

    // Keys, for tests too: ↑ ↓ ← → Enter.
    void move(int delta);
    void change(int delta);
    void activate();

signals:
    void closeRequested();  // Esc

protected:
    void keyPressEvent(QKeyEvent*) override;
    bool focusNextPrevChild(bool next) override;
    void resizeEvent(QResizeEvent*) override;

private:
    class Content;
    Content* content_ = nullptr;
};

}  // namespace graphite
