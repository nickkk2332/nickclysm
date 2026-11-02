#pragma once
#ifndef CATA_SRC_EOC_UI_H
#define CATA_SRC_EOC_UI_H

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <vector>

// The windows EOCs show the player: u_show_text.
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
 * Tests can't show windows: in test mode show_text returns this function's answer to its
 * arguments, or closes when it is unset.
 */
extern std::function<std::optional<size_t>( const std::string &title, const std::string &text,
        const std::vector<text_button> &buttons )> show_text_test_answer;

} // namespace eoc_ui

#endif // CATA_SRC_EOC_UI_H
