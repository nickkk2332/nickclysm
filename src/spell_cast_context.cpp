#include "spell_cast_context.h"

thread_local const spell_cast_context *spell_cast_context::current = nullptr;

spell_cast_context::spell_cast_context( const spell &spell,
                                        const global_variables::impl_t &values ) :
    identity( &spell ), values( values ), previous( current )
{
    current = this;
}

spell_cast_context::~spell_cast_context()
{
    current = previous;
}

const global_variables::impl_t &spell_cast_context::get( const spell &spell )
{
    for( const spell_cast_context *scope = current; scope; scope = scope->previous ) {
        if( scope->identity == &spell ) {
            return scope->values;
        }
    }
    static const global_variables::impl_t empty;
    return empty;
}
