#include "eoc_ui.h"

#include <algorithm>

#include "cached_options.h"
#include "catacharset.h"
#include "color.h"
#include "cursesdef.h"
#include "input_context.h"
#include "output.h"
#include "point.h"
#include "string_formatter.h"
#include "translations.h"
#include "ui_manager.h"
#include "uilist.h"

std::function<std::optional<size_t>( const std::string &, const std::string &,
                                     const std::vector<eoc_ui::text_button> & )>
eoc_ui::show_text_test_answer;

namespace
{

// The buttons on one line, the selected one highlighted and greyed-out ones dark.
void draw_buttons( const catacurses::window &w, int y, int width,
                   const std::vector<eoc_ui::text_button> &buttons, size_t selected )
{
    int x = 2;
    for( size_t i = 0; i < buttons.size(); i++ ) {
        const std::string label = "[ " + buttons[i].name + " ]";
        const int label_width = utf8_width( label );
        if( x + label_width > width - 2 && x > 2 ) {
            // No room left on the line; the rest can't be shown.
            break;
        }
        nc_color color = buttons[i].enabled ? c_white : c_dark_gray;
        if( i == selected ) {
            color = hilite( color );
        }
        trim_and_print( w, point( x, y ), width - 2 - x, color, label );
        x += label_width + 2;
    }
}

// The next button that can be pressed, from `from` in direction `step` (1 or -1), wrapping.
size_t next_enabled( const std::vector<eoc_ui::text_button> &buttons, size_t from, int step )
{
    const int count = static_cast<int>( buttons.size() );
    for( int n = 1; n <= count; n++ ) {
        const int i = ( ( static_cast<int>( from ) + step * n ) % count + count ) % count;
        if( buttons[i].enabled ) {
            return static_cast<size_t>( i );
        }
    }
    return from;
}

} // namespace

std::optional<size_t> eoc_ui::show_text( const std::string &title, const std::string &text,
        const std::vector<text_button> &buttons )
{
    if( test_mode ) {
        if( !show_text_test_answer ) {
            return std::nullopt;
        }
        const std::optional<size_t> answer = show_text_test_answer( title, text, buttons );
        // A greyed-out button can't be pressed.
        if( answer && ( *answer >= buttons.size() || !buttons[*answer].enabled ) ) {
            return std::nullopt;
        }
        return answer;
    }

    input_context ctxt( "EOC_SHOW_TEXT" );
    ctxt.register_action( "UP" );
    ctxt.register_action( "DOWN" );
    ctxt.register_action( "PAGE_UP" );
    ctxt.register_action( "PAGE_DOWN" );
    ctxt.register_action( "LEFT" );
    ctxt.register_action( "RIGHT" );
    ctxt.register_action( "NEXT_TAB" );
    ctxt.register_action( "PREV_TAB" );
    ctxt.register_action( "CONFIRM" );
    ctxt.register_action( "QUIT" );
    ctxt.register_action( "HELP_KEYBINDINGS" );
    ctxt.register_action( "SCROLL_UP" );
    ctxt.register_action( "SCROLL_DOWN" );

    const bool any_enabled = std::any_of( buttons.begin(), buttons.end(),
    []( const text_button & b ) {
        return b.enabled;
    } );
    size_t selected = 0;
    if( any_enabled && !buttons[0].enabled ) {
        selected = next_enabled( buttons, 0, 1 );
    }

    catacurses::window w;
    int width = 0;
    int height = 0;
    // the text area: one column of border on each side, and the buttons' line at the bottom
    int text_w = 0;
    int text_h = 0;
    std::vector<std::string> lines;
    int beg_line = 0;
    int max_beg_line = 0;
    const int button_lines = buttons.empty() ? 0 : 2;

    ui_adaptor ui;
    const auto screen_resize_cb = [&]( ui_adaptor & ui ) {
        width = std::min( TERMX, std::max( 60, TERMX * 2 / 3 ) );
        text_w = std::max( 1, width - 3 );
        lines = foldstring( text, text_w );
        const int wanted = static_cast<int>( lines.size() ) + 2 + button_lines;
        height = std::min( TERMY, std::max( 8, wanted ) );
        text_h = std::max( 1, height - 2 - button_lines );
        max_beg_line = std::max( 0, static_cast<int>( lines.size() ) - text_h );
        beg_line = std::min( beg_line, max_beg_line );
        w = catacurses::newwin( height, width, point( ( TERMX - width ) / 2,
                                ( TERMY - height ) / 2 ) );
        ui.position_from_window( w );
    };
    screen_resize_cb( ui );
    ui.on_screen_resize( screen_resize_cb );
    ui.on_redraw( [&]( const ui_adaptor & ) {
        werase( w );
        draw_border( w, BORDER_COLOR, title );
        for( int line = beg_line, y = 1; line < std::min<int>( beg_line + text_h, lines.size() );
             ++line, ++y ) {
            nc_color color = c_white;
            print_colored_text( w, point( 1, y ), color, color, lines[line] );
        }
        scrollbar().offset_x( width - 1 ).offset_y( 1 ).content_size( lines.size() )
        .viewport_pos( beg_line ).viewport_size( text_h ).apply( w );
        if( !buttons.empty() ) {
            const int separator = height - 1 - button_lines;
            for( int x = 1; x < width - 1; x++ ) {
                mvwputch( w, point( x, separator ), BORDER_COLOR, LINE_OXOX );
            }
            draw_buttons( w, height - 2, width, buttons, any_enabled ? selected : buttons.size() );
        }
        wnoutrefresh( w );
    } );

    while( true ) {
        ui_manager::redraw();
        const std::string action = ctxt.handle_input();
        if( action == "UP" || action == "SCROLL_UP" ) {
            beg_line = std::max( beg_line - 1, 0 );
        } else if( action == "DOWN" || action == "SCROLL_DOWN" ) {
            beg_line = std::min( beg_line + 1, max_beg_line );
        } else if( action == "PAGE_UP" ) {
            beg_line = std::max( beg_line - text_h, 0 );
        } else if( action == "PAGE_DOWN" ) {
            beg_line = std::min( beg_line + text_h, max_beg_line );
        } else if( ( action == "RIGHT" || action == "NEXT_TAB" ) && any_enabled ) {
            selected = next_enabled( buttons, selected, 1 );
        } else if( ( action == "LEFT" || action == "PREV_TAB" ) && any_enabled ) {
            selected = next_enabled( buttons, selected, -1 );
        } else if( action == "CONFIRM" ) {
            if( any_enabled ) {
                return selected;
            }
            return std::nullopt;
        } else if( action == "QUIT" ) {
            return std::nullopt;
        }
    }
}
