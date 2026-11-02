#pragma once
#ifndef CATA_SRC_EOC_UI_H
#define CATA_SRC_EOC_UI_H

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "point.h"

// The windows EOCs show the player: u_show_text and run_eoc_checklist.
namespace eoc_ui
{

struct text_button {
    std::string name;
    // false: greyed out, it can't be pressed
    bool enabled = true;
};

/**
 * A scrollable window with @p title, @p text and a row of @p buttons.
 * @returns the index of the button pressed, or nullopt when the window was closed without one
 */
std::optional<size_t> show_text( const std::string &title, const std::string &text,
                                 const std::vector<text_button> &buttons );

/**
 * Where show_text puts each of @p buttons in a window @p width wide: the column, and the line of
 * the button area. A button that doesn't fit on a line starts the next one, so every button the
 * player can pick is shown.
 */
std::vector<point> layout_buttons( const std::vector<text_button> &buttons, int width );

/**
 * Tests can't show windows: in test mode show_text returns this function's answer to its
 * arguments, or closes when it is unset.
 */
extern std::function<std::optional<size_t>( const std::string &title, const std::string &text,
        const std::vector<text_button> &buttons )> show_text_test_answer;

struct checklist_option {
    std::string name;
    std::string description;
    std::optional<char> key;
    // false: greyed out, it can't be ticked
    bool enabled = true;
    // ticked when the menu opens
    bool checked = false;
};

/**
 * A menu where the player ticks between @p min and @p max of @p options, then confirms.
 * @returns the indices of the ticked options, or nullopt when the menu was cancelled
 */
std::optional<std::vector<size_t>> checklist( const std::string &title,
                                const std::vector<checklist_option> &options, int min, int max );

/**
 * In test mode checklist returns this function's answer to its arguments, or cancels when it is
 * unset. An answer that ticks a greyed-out option, or too few or too many, counts as cancelling,
 * as the menu would not let the player confirm it.
 */
extern std::function<std::optional<std::vector<size_t>>( const std::string &title,
        const std::vector<checklist_option> &options, int min, int max )> checklist_test_answer;

} // namespace eoc_ui

#endif // CATA_SRC_EOC_UI_H
