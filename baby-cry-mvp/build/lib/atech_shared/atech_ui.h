/**
 * @file atech_ui.h
 * @brief Opinionated UI primitives for Atech displays.
 *
 * Goal: the LLM composes screens by calling these helpers instead of doing
 * setCursor/setTextColor/print/math by hand. Pre-baked layout + colors mean
 * generated UIs look consistent and don't depend on the LLM picking sensible
 * pixel positions.
 *
 * All helpers are templated on the display so they work across any
 * Adafruit-GFX-compatible driver (ST7735_TFT, ST7789_TFT). Widths come from
 * d.width(), so layouts span whichever screen they are drawn on.
 *
 * Auto-included in every generated main.cpp.
 */

#pragma once
#include <Arduino.h>
#include "atech_helpers.h"

// ---------------------------------------------------------------------------
// Atech display palette (RGB565) — Apple-inspired default
//
// Tuned to Apple's dark-mode system colors so generated screens look calm and
// premium out of the box: a true-black canvas, a single restrained accent, and
// a muted gray for secondary text (no rainbow of saturated primaries). Use
// these instead of raw hex literals so projects stay consistent and the design
// can be retuned in one place. The LLM may pick any of these, or use a raw
// RGB565 color only if a project explicitly needs a different palette.
//
// Semantic colors (OK/WARN/ALERT) are reserved for status — do NOT use them as
// decoration. In a nominal UI the only colors on screen should be ATECH_FG
// (white), ATECH_MUTED (gray), and ATECH_ACCENT (blue).
// ---------------------------------------------------------------------------

#define ATECH_BG        0x0000  // Background — true black (OLED-style, Apple dark)
#define ATECH_SURFACE   0x18E3  // Elevated card / grouped surface — #1C1C1E
#define ATECH_FG        0xFFFF  // Primary text / big values — white
#define ATECH_MUTED     0x8C72  // Secondary labels, captions — systemGray #8E8E93
#define ATECH_FAINT     0x630C  // Tertiary hints, disabled — #636366
#define ATECH_HAIRLINE  0x39C7  // Separators / thin outlines — #38383A
#define ATECH_ACCENT    0x0C3F  // The one accent / live value — systemBlue #0A84FF
#define ATECH_OK        0x368B  // Success / nominal — systemGreen #30D158
#define ATECH_WARN      0xFCE1  // Warning — systemOrange #FF9F0A
#define ATECH_ALERT     0xFA27  // Critical — systemRed #FF453A

namespace atech {

// ---------------------------------------------------------------------------
// Rate-limited render pattern
//
// Wraps the canonical canvas-mode loop body:
//     clear → draws → display
// in a rate-limited shell so the LLM does not have to write timing logic.
// Pass the display, a timer, the desired interval in ms, and the draw lambda.
// The lambda body is the ONLY thing the LLM has to think about.
//
// Use:
//     EveryTimer screenTimer;        // in global_variables
//     // in loop_code or task body:
//     atech::renderEvery(screenTimer, 33, screen, [&]() {
//         atech::drawHeader(screen, "Status");
//         // ... more draws ...
//     });
// ---------------------------------------------------------------------------

template <typename Display, typename DrawFn>
inline void renderEvery(EveryTimer& timer, unsigned long ms, Display& d, DrawFn draw) {
    if (timer.ready(ms)) {
        d.clear();
        draw();
        d.display();
    }
}

// ---------------------------------------------------------------------------
// drawHeader — quiet top title with a hairline divider (Apple-style).
//
//   atech::drawHeader(screen, "Temperature");
//
// The title reads as a small MUTED caption (like an iOS section header), with a
// thin ATECH_HAIRLINE rule beneath it — not a loud accent bar. This keeps the
// eye on the value below, not on the chrome.
// ---------------------------------------------------------------------------

template <typename Display>
inline void drawHeader(Display& d, const char* title) {
    d.setFont(NULL);
    d.setTextSize(1);
    d.setTextColor(ATECH_MUTED);
    d.setCursor(8, 4);
    d.print(title);
    d.drawLine(8, 14, d.width() - 8, 14, ATECH_HAIRLINE);
}

template <typename Display>
inline void drawHeader(Display& d, const String& title) { drawHeader(d, title.c_str()); }

// ---------------------------------------------------------------------------
// drawValueRow — one labeled value with units, on a numbered row.
//
//   atech::drawValueRow(screen, "Temp",    23.4, "°C", 1);
//   atech::drawValueRow(screen, "Humidity", 51.0, "%",  2);
//
// Rows are 1-indexed and start under the header. Label reads as a MUTED gray
// caption; the value is bright ATECH_FG (white) so it stands out — the iOS
// Settings pattern of secondary label + primary value. Units are ATECH_FAINT.
// ---------------------------------------------------------------------------

namespace _detail {
inline int rowY(int row) { return 20 + (row - 1) * 14; }
}  // namespace _detail

template <typename Display, typename Value>
inline void drawValueRow(Display& d, const char* label, Value value, const char* units, int row) {
    int y = _detail::rowY(row);
    d.setFont(NULL);
    d.setTextSize(1);
    d.setTextColor(ATECH_MUTED);
    d.setCursor(8, y);
    d.print(label);
    d.setTextColor(ATECH_FG);
    d.setCursor(64, y);
    d.print(value);
    if (units && units[0]) {
        d.setTextColor(ATECH_FAINT);
        d.print(' ');
        d.print(units);
    }
}

// Overload for floats with 1 decimal place — the common dashboard case.
template <typename Display>
inline void drawValueRow(Display& d, const char* label, float value, const char* units, int row) {
    int y = _detail::rowY(row);
    d.setFont(NULL);
    d.setTextSize(1);
    d.setTextColor(ATECH_MUTED);
    d.setCursor(8, y);
    d.print(label);
    d.setTextColor(ATECH_FG);
    d.setCursor(64, y);
    d.print(value, 1);
    if (units && units[0]) {
        d.setTextColor(ATECH_FAINT);
        d.print(' ');
        d.print(units);
    }
}

// ---------------------------------------------------------------------------
// drawCard — a rounded, filled surface to group content on (Apple's grouped
// "card" look). Draw this FIRST, then draw text/values on top of it.
//
//   atech::drawCard(screen, 8, 8, 144, 64);            // default surface
//   atech::drawCard(screen, 8, 8, 144, 64, ATECH_SURFACE);
//
// Uses a 10px corner radius — the softly-rounded rectangle that reads as iOS.
// ---------------------------------------------------------------------------

template <typename Display>
inline void drawCard(Display& d, int x, int y, int w, int h, uint16_t fill = ATECH_SURFACE) {
    d.fillRoundRect(x, y, w, h, 10, fill);
}

// ---------------------------------------------------------------------------
// drawProgressBar — rounded "pill" track showing pct in [0, 1].
//
//   atech::drawProgressBar(screen, 60, batteryPct);  // y=60, pct in [0,1]
//
// Full width minus an 8px margin each side, 8px tall. A ATECH_SURFACE track (no harsh
// outline) with a rounded ATECH_ACCENT fill — the iOS progress look.
// ---------------------------------------------------------------------------

template <typename Display>
inline void drawProgressBar(Display& d, int y, float pct, uint16_t fillColor = ATECH_ACCENT) {
    const int x = 8;
    const int w = d.width() - 16;
    const int h = 8;
    const int r = 4;  // fully-rounded ends (h/2)
    if (pct < 0.0f) pct = 0.0f;
    if (pct > 1.0f) pct = 1.0f;
    d.fillRoundRect(x, y, w, h, r, ATECH_SURFACE);
    int filled = (int)(pct * w);
    if (filled >= 2 * r) {
        d.fillRoundRect(x, y, filled, h, r, fillColor);
    } else if (filled > 0) {
        d.fillRect(x, y, filled, h, fillColor);
    }
}

// ---------------------------------------------------------------------------
// drawCenteredText — centered text on a given y, for titles, splashes, etc.
// Uses the default small font; for big text use setFont + drawCenteredText.
// ---------------------------------------------------------------------------

template <typename Display>
inline void drawCenteredText(Display& d, int y, const char* text, uint16_t color = ATECH_FG) {
    // Default font glyph is 6x8 pixels.
    int charCount = 0;
    for (const char* p = text; *p; ++p) ++charCount;
    int textWidth = charCount * 6;
    int x = (d.width() - textWidth) / 2;
    if (x < 0) x = 0;
    d.setFont(NULL);
    d.setTextSize(1);
    d.setTextColor(color);
    d.setCursor(x, y);
    d.print(text);
}


// ===========================================================================
// Widgets: title/status bars, timed status message, scrollable list, CSV
// table, word-wrapped text. They size themselves from the screen and the font,
// so they work on both screens and in portrait. Each takes an optional font
// (e.g. &FreeSans9pt7b; nullptr = the built-in 6x8 font) and leaves it set.
// The font is a template parameter so this header compiles in projects
// without a screen, where GFXfont does not exist.
// Canvas mode: call display() after drawing, as for any other drawing.
// ===========================================================================

#define ATECH_PANEL 0x2104  // dark grey for bars and alternate rows

namespace _detail {
// Cursor y that vertically centres one line of text in a row [top, top+h).
template <typename Display>
inline int16_t textYInRow(Display& d, int16_t top, int16_t h) {
    int16_t x1, y1;
    uint16_t w, th;
    d.textBounds("Ag", x1, y1, w, th);
    return top + ((int16_t)h - (int16_t)th) / 2 - y1;
}
}  // namespace _detail

// Shorten text with ".." until it fits maxW pixels in the current font.
template <typename Display>
inline String fitText(Display& d, const String& text, int16_t maxW) {
    if (d.textWidth(text) <= maxW) return text;
    String t = text;
    while (t.length() > 0 && d.textWidth(t + "..") > maxW) t.remove(t.length() - 1);
    return t + "..";
}

// Coloured bar across the top with a title. Returns its height.
template <typename Display, typename Font = decltype(nullptr)>
inline int16_t drawTitleBar(Display& d, const char* title, Font font = nullptr,
                            uint16_t bg = ATECH_ACCENT, uint16_t fg = ATECH_BG) {
    d.setFont(font);
    d.setTextSize(1);
    int16_t h = d.lineHeight() + 8;
    d.fillRect(0, 0, d.width(), h, bg);
    d.setTextColor(fg);
    d.setCursor(6, _detail::textYInRow(d, 0, h));
    d.print(fitText(d, String(title), d.width() - 12));
    return h;
}

// Bar across the bottom with a line of text (hints, status). Returns its height.
template <typename Display, typename Font = decltype(nullptr)>
inline int16_t drawStatusBar(Display& d, const char* text, Font font = nullptr,
                             uint16_t fg = ATECH_MUTED, uint16_t bg = ATECH_PANEL) {
    d.setFont(font);
    d.setTextSize(1);
    int16_t h = d.lineHeight() + 6;
    int16_t top = d.height() - h;
    d.fillRect(0, top, d.width(), h, bg);
    d.setTextColor(fg);
    d.setCursor(6, _detail::textYInRow(d, top, h));
    d.print(fitText(d, String(text), d.width() - 12));
    return h;
}

// A message that shows for a while, then clears itself ("Saved", "No card").
//   StatusMessage status;            // global
//   status.show("Photo saved");      // anywhere
//   drawStatusBar(screen, status.active() ? status.text() : "Tap: next");
class StatusMessage {
public:
    void show(const char* text, uint32_t ms = 1500) {
        _text = text;
        _until = millis() + ms;
    }
    bool active() const { return _text.length() && (int32_t)(_until - millis()) > 0; }
    const char* text() const { return _text.c_str(); }

private:
    String _text;
    uint32_t _until = 0;
};

// Selection and scroll position for drawList(). Keep one per list.
struct ListView {
    int selected = 0;
    int top = 0;
    void next(int count) { if (count > 0) selected = (selected + 1) % count; }
    void prev(int count) { if (count > 0) selected = (selected - 1 + count) % count; }
    void reset() { selected = 0; top = 0; }
};

// Scrollable list with the selected row highlighted and a scrollbar when it
// does not fit. `folder` (optional) marks rows drawn with a folder arrow
// instead of a file box. Returns the number of rows visible at once.
template <typename Display, typename Font = decltype(nullptr)>
inline int drawList(Display& d, const String* items, int count, ListView& view,
                    int16_t x, int16_t y, int16_t w, int16_t h,
                    Font font = nullptr, const bool* folder = nullptr,
                    uint16_t selBg = ATECH_ACCENT, uint16_t fg = ATECH_FG) {
    d.setFont(font);
    d.setTextSize(1);
    int16_t rowH = d.lineHeight() + 6;
    int visible = h / rowH;
    if (visible < 1) visible = 1;
    if (count <= 0) {
        d.setTextColor(ATECH_MUTED);
        d.setCursor(x + 6, _detail::textYInRow(d, y, rowH));
        d.print("(empty)");
        return visible;
    }
    if (view.selected >= count) view.selected = count - 1;
    if (view.selected < 0) view.selected = 0;
    if (view.selected < view.top) view.top = view.selected;
    if (view.selected >= view.top + visible) view.top = view.selected - visible + 1;
    if (view.top > count - visible) view.top = count - visible > 0 ? count - visible : 0;

    bool scrollbar = count > visible;
    int16_t listW = scrollbar ? w - 6 : w;
    int16_t icon = folder ? rowH / 2 + 4 : 0;
    for (int r = 0; r < visible && view.top + r < count; r++) {
        int i = view.top + r;
        int16_t ry = y + r * rowH;
        bool sel = (i == view.selected);
        uint16_t rowFg = sel ? ATECH_BG : fg;
        if (sel) d.fillRoundRect(x, ry + 1, listW, rowH - 2, 4, selBg);
        if (folder) {
            int16_t cy = ry + rowH / 2, s = rowH / 4;
            if (folder[i]) d.fillTriangle(x + 5, cy - s, x + 5, cy + s, x + 5 + s + 2, cy, rowFg);
            else d.drawRect(x + 5, cy - s, s + 2, 2 * s, rowFg);
        }
        d.setTextColor(rowFg);
        d.setCursor(x + 6 + icon, _detail::textYInRow(d, ry, rowH));
        d.print(fitText(d, items[i], listW - 12 - icon));
    }
    if (scrollbar) {
        int16_t trackH = visible * rowH;
        int16_t thumbH = trackH * visible / count;
        if (thumbH < 6) thumbH = 6;
        int16_t thumbY = y + (int32_t)(trackH - thumbH) * view.top / (count - visible);
        d.drawRect(x + w - 4, y, 4, trackH, ATECH_MUTED);
        d.fillRect(x + w - 4, thumbY, 4, thumbH, selBg);
    }
    return visible;
}

namespace _detail {
// One CSV line into fields, honouring "quoted, fields" and "" escapes.
inline int splitCsvLine(const String& line, String* out, int maxFields) {
    int n = 0;
    String cur;
    bool quoted = false;
    for (unsigned i = 0; i < line.length() && n < maxFields; i++) {
        char c = line[i];
        if (quoted) {
            if (c == '"' && i + 1 < line.length() && line[i + 1] == '"') { cur += '"'; i++; }
            else if (c == '"') quoted = false;
            else cur += c;
        } else if (c == '"') {
            quoted = true;
        } else if (c == ',') {
            out[n++] = cur;
            cur = "";
        } else if (c != '\r') {
            cur += c;
        }
    }
    if (n < maxFields && (cur.length() || line.endsWith(","))) out[n++] = cur;
    return n;
}

// Start offset of line `index` in text, or -1.
inline int lineStart(const String& text, int index) {
    int pos = 0;
    for (int i = 0; i < index; i++) {
        int nl = text.indexOf('\n', pos);
        if (nl < 0) return -1;
        pos = nl + 1;
    }
    return pos < (int)text.length() ? pos : -1;
}

inline String lineAt(const String& text, int start) {
    int nl = text.indexOf('\n', start);
    return text.substring(start, nl < 0 ? text.length() : nl);
}
}  // namespace _detail

// A CSV file as a table: the first line is the header row (always shown),
// then data rows from `firstRow` (0 = the first data row). Column widths
// follow the widest visible text in each column. Returns the number of data
// rows in the text, so the caller can page: firstRow += rowsPerPage.
template <typename Display, typename Font = decltype(nullptr)>
inline int drawCsvTable(Display& d, const String& csv, int firstRow,
                        int16_t x, int16_t y, int16_t w, int16_t h,
                        Font font = nullptr) {
    const int MAX_COLS = 8;
    d.setFont(font);
    d.setTextSize(1);
    int16_t rowH = d.lineHeight() + 4;
    int visibleData = h / rowH - 1;
    if (visibleData < 1) visibleData = 1;

    int total = 0;
    for (int p = 0; p < (int)csv.length();) {
        int nl = csv.indexOf('\n', p);
        if (nl < 0) nl = csv.length();
        if (nl > p) total++;
        p = nl + 1;
    }
    int dataRows = total > 0 ? total - 1 : 0;
    if (firstRow < 0) firstRow = 0;

    // Gather the header and the visible rows once, and size the columns.
    String cells[MAX_COLS];
    int16_t colW[MAX_COLS] = {0};
    int cols = 0;
    int shown = 0;
    for (int r = -1; r < visibleData; r++) {
        int start = _detail::lineStart(csv, r < 0 ? 0 : 1 + firstRow + r);
        if (start < 0) break;
        int n = _detail::splitCsvLine(_detail::lineAt(csv, start), cells, MAX_COLS);
        if (n > cols) cols = n;
        for (int c = 0; c < n; c++) {
            int16_t tw = d.textWidth(cells[c]) + 8;
            if (tw > colW[c]) colW[c] = tw;
        }
        shown++;
    }
    if (cols == 0) {
        d.setTextColor(ATECH_MUTED);
        d.setCursor(x + 4, _detail::textYInRow(d, y, rowH));
        d.print("(empty table)");
        return 0;
    }
    // Scale columns to the table width, keeping every column at least 24 px.
    int32_t sum = 0;
    for (int c = 0; c < cols; c++) sum += colW[c];
    for (int c = 0; c < cols; c++) {
        colW[c] = sum > 0 ? (int16_t)((int32_t)colW[c] * w / sum) : w / cols;
        if (colW[c] < 24) colW[c] = 24;
    }

    for (int r = -1; r < shown - 1; r++) {
        int start = _detail::lineStart(csv, r < 0 ? 0 : 1 + firstRow + r);
        if (start < 0) break;
        int n = _detail::splitCsvLine(_detail::lineAt(csv, start), cells, MAX_COLS);
        int16_t ry = y + (r + 1) * rowH;
        bool header = r < 0;
        uint16_t bg = header ? ATECH_ACCENT : ((r % 2) ? ATECH_PANEL : ATECH_BG);
        d.fillRect(x, ry, w, rowH, bg);
        d.setTextColor(header ? ATECH_BG : ATECH_FG);
        int16_t cx = x;
        for (int c = 0; c < cols && cx < x + w; c++) {
            if (c < n) {
                d.setCursor(cx + 4, _detail::textYInRow(d, ry, rowH));
                d.print(fitText(d, cells[c], colW[c] - 8));
            }
            cx += colW[c];
            if (c < cols - 1 && cx < x + w) d.drawLine(cx - 1, ry, cx - 1, ry + rowH - 1, ATECH_MUTED);
        }
    }
    return dataRows;
}

// Word-wrapped text in a box, wrapping by pixel width (not character count)
// and honouring newlines. Starts at wrapped line `firstLine`; returns the
// total number of wrapped lines, so the caller can scroll.
template <typename Display, typename Font = decltype(nullptr)>
inline int drawWrappedText(Display& d, const String& text, int firstLine,
                           int16_t x, int16_t y, int16_t w, int16_t h,
                           Font font = nullptr, uint16_t fg = ATECH_FG) {
    d.setFont(font);
    d.setTextSize(1);
    d.setTextColor(fg);
    int16_t lineH = d.lineHeight();
    int visible = h / lineH;
    int line = 0;
    int pos = 0;
    int len = text.length();
    while (pos <= len) {
        int nl = text.indexOf('\n', pos);
        if (nl < 0) nl = len;
        String para = text.substring(pos, nl);
        para.replace("\r", "");
        // Break the paragraph into lines that fit w.
        int start = 0;
        do {
            int end = para.length();
            if (d.textWidth(para.substring(start, end)) > w) {
                int lastSpace = -1;
                end = start;
                while (end < (int)para.length() && d.textWidth(para.substring(start, end + 1)) <= w) {
                    if (para[end] == ' ') lastSpace = end;
                    end++;
                }
                if (lastSpace > start && end < (int)para.length()) end = lastSpace + 1;
                if (end == start) end = start + 1;  // a single glyph wider than the box
            }
            if (line >= firstLine && line < firstLine + visible) {
                d.setCursor(x, _detail::textYInRow(d, y + (line - firstLine) * lineH, lineH));
                String out = para.substring(start, end);
                out.trim();
                d.print(out);
            }
            line++;
            start = end;
        } while (start < (int)para.length());
        pos = nl + 1;
    }
    return line;
}

}  // namespace atech

using atech::renderEvery;
using atech::drawHeader;
using atech::drawValueRow;
using atech::drawCard;
using atech::drawProgressBar;
using atech::drawCenteredText;
using atech::fitText;
using atech::drawTitleBar;
using atech::drawStatusBar;
using atech::StatusMessage;
using atech::ListView;
using atech::drawList;
using atech::drawCsvTable;
using atech::drawWrappedText;
