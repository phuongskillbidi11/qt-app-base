#pragma once

#include <QString>

class QLabel;
class QPushButton;
class QTableWidget;
class QWidget;

// Shared look for every data table and button bar in the app, so the tabs do not each
// invent their own spacing. Colours and radii stay in theme_*.qss — nothing here writes
// an inline stylesheet.
namespace UiStyle {

// LAYOUT-FIXES 6: alternating rows, no grid, right-sized header, Consolas so numeric
// columns line up.
void tuneTable(QTableWidget *table);

// Right-align a column and give it the monospace face, for numeric fields such as Pin,
// CardNo, DoorID, EventType, InOutState and VerifyMode. The values stay raw numbers —
// they are deliberately not mapped to words.
void alignNumericColumn(QTableWidget *table, int column);

// LAYOUT-FIXES 6: centred placeholder shown over an empty table, hidden as soon as it
// has rows. Follows the viewport when it resizes and never eats mouse events.
QLabel *attachEmptyHint(QTableWidget *table, const QString &text);

// LAYOUT-FIXES 5: fixed 96x28 so a button bar does not stretch each button across the
// window. Pass accent = true for the one primary action in a bar.
void makeBarButton(QPushButton *button, bool accent = false);

// Re-apply QSS after a dynamic property (accent / danger / flat / state / hint) changes.
// Qt does not restyle on its own, so without this the property is set and nothing looks
// different.
void repolish(QWidget *widget);

}  // namespace UiStyle
