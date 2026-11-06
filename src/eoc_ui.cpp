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

std::function<std::optional<std::vector<size_t>>( const std::string &,
        const std::vector<eoc_ui::checklist_option> &, int, int )>
        eoc_ui::checklist_test_answer;

namespace
{

std::string button_label( const eoc_ui::text_button &button )
{
    return "[ " + button.name + " ]";
}

} // namespace

std::vector<point> eoc_ui::layout_buttons( const std::vector<text_button> &buttons, int width )
{
    std::vector<point> places;
    point at( 2, 0 );
    for( const eoc_ui::text_button &button : buttons ) {
        const int label_width = utf8_width( button_label( button ) );
        if( at.x + label_width > width - 2 && at.x > 2 ) {
            at = point( 2, at.y + 1 );
        }
        places.push_back( at );
        at.x += label_width + 2;
    }
    return places;
}

namespace
{

// The buttons from line `top` on, the selected one highlighted and greyed-out ones dark. A label
// wider than the window is cut.
void draw_buttons( const catacurses::window &w, int top, int width,
                   const std::vector<eoc_ui::text_button> &buttons, const std::vector<point> &places,
                   size_t selected )
{
    for( size_t i = 0; i < buttons.size(); i++ ) {
        nc_color color = buttons[i].enabled ? c_white : c_dark_gray;
        if( i == selected ) {
            color = hilite( color );
        }
        trim_and_print( w, places[i] + point( 0, top ), width - 2 - places[i].x, color,
                        button_label( buttons[i] ) );
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
    std::vector<point> button_places;
    // the button area: a separator, then the lines of buttons
    int button_lines = 0;

    ui_adaptor ui;
    const auto screen_resize_cb = [&]( ui_adaptor & ui ) {
        width = std::min( TERMX, std::max( 60, TERMX * 2 / 3 ) );
        text_w = std::max( 1, width - 3 );
        lines = foldstring( text, text_w );
        button_places = layout_buttons( buttons, width );
        button_lines = buttons.empty() ? 0 : button_places.back().y + 2;
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
            draw_buttons( w, separator + 1, width, buttons, button_places,
                          any_enabled ? selected : buttons.size() );
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

std::optional<std::vector<size_t>> eoc_ui::checklist( const std::string &title,
                                const std::vector<checklist_option> &options, int min, int max )
{
    if( test_mode ) {
        if( !checklist_test_answer ) {
            return std::nullopt;
        }
        std::optional<std::vector<size_t>> answer = checklist_test_answer( title, options, min, max );
        if( !answer ) {
            return std::nullopt;
        }
        std::sort( answer->begin(), answer->end() );
        answer->erase( std::unique( answer->begin(), answer->end() ), answer->end() );
        // The menu can't confirm what this would.
        const int count = static_cast<int>( answer->size() );
        if( count < min || count > max ||
        std::any_of( answer->begin(), answer->end(), [&options]( size_t i ) {
        return i >= options.size() || !options[i].enabled;
        } ) ) {
            return std::nullopt;
        }
        return answer;
    }

    std::vector<bool> ticked;
    ticked.reserve( options.size() );
    for( const checklist_option &option : options ) {
        ticked.push_back( option.enabled && option.checked );
    }
    const bool has_descriptions = std::any_of( options.begin(), options.end(),
    []( const checklist_option & option ) {
        return !option.description.empty();
    } );
    std::string how_many;
    if( min == max ) {
        how_many = string_format( n_gettext( "Tick %d option.", "Tick %d options.", min ), min );
    } else if( min == 0 ) {
        how_many = string_format( n_gettext( "Tick up to %d option.", "Tick up to %d options.", max ),
                                  max );
    } else {
        how_many = string_format( _( "Tick %1$d to %2$d options." ), min, max );
    }
    const int confirm = static_cast<int>( options.size() );
    int selected = 0;
    while( true ) {
        const int count = static_cast<int>( std::count( ticked.begin(), ticked.end(), true ) );
        uilist menu;
        menu.title = title;
        menu.text = string_format( _( "%1$s  Ticked: %2$d." ), how_many, count );
        menu.desc_enabled = has_descriptions;
        // Greyed-out options can still be highlighted, to read their description.
        menu.hilight_disabled = true;
        menu.allow_cancel = true;
        for( size_t i = 0; i < options.size(); i++ ) {
            // Once enough are ticked, the others can't be.
            const bool can_toggle = options[i].enabled && ( ticked[i] || count < max );
            const std::string line = string_format( "[%s] %s", ticked[i] ? "x" : " ",
                                                    options[i].name );
            menu.addentry_desc( static_cast<int>( i ), can_toggle,
                                options[i].key ? static_cast<int>( *options[i].key ) : MENU_AUTOASSIGN, line,
                                options[i].description );
        }
        menu.addentry_desc( confirm, count >= min && count <= max, MENU_AUTOASSIGN, _( "Confirm" ),
                            std::string() );
        menu.selected = selected;
        menu.query();
        if( menu.ret == confirm ) {
            std::vector<size_t> chosen;
            for( size_t i = 0; i < ticked.size(); i++ ) {
                if( ticked[i] ) {
                    chosen.push_back( i );
                }
            }
            return chosen;
        }
        if( menu.ret < 0 || menu.ret > confirm ) {
            return std::nullopt;
        }
        ticked[menu.ret] = !ticked[menu.ret];
        selected = menu.selected;
    }
}
