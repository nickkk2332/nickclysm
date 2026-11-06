#pragma once
#ifndef CATA_SRC_SPELL_CAST_CONTEXT_H
#define CATA_SRC_SPELL_CAST_CONTEXT_H

#include "global_vars.h"

class spell;

/** Explicit, object-bound context for a synchronous cast and its declared extras.
 * Unrelated callback/proc spells do not inherit an ambient caster context.
 * The ability scheduler persists the input; this guard never outlives the call.
 */
class spell_cast_context
{
    public:
        spell_cast_context( const spell &spell, const global_variables::impl_t &values );
        ~spell_cast_context();
        spell_cast_context( const spell_cast_context & ) = delete;
        spell_cast_context &operator=( const spell_cast_context & ) = delete;
        static const global_variables::impl_t &get( const spell &spell );
    private:
        const spell *identity;
        global_variables::impl_t values;
        const spell_cast_context *previous;
        static thread_local const spell_cast_context *current;
};

#endif // CATA_SRC_SPELL_CAST_CONTEXT_H
