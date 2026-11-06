#include "execution_fatal.h"

#include <set>

#include "avatar.h"
#include "creature.h"
#include "execution_runtime.h"
#include "game.h"
#include "monster.h"
#include "npc.h"

namespace
{
thread_local std::set<const Creature *> finalizing;
} // namespace

bool execution_fatal::pending( const Creature &who )
{
    return g && g->executions().death_pending( who );
}

bool execution_fatal::finalized( const Creature &who )
{
    if( const avatar *player = who.as_avatar() ) {
        return player->death_finalized();
    }
    if( const monster *mon = who.as_monster() ) {
        return mon->death_finalized();
    }
    if( const npc *person = who.as_npc() ) {
        return person->death_finalized();
    }
    return who.is_dead_state();
}

bool execution_fatal::before_death( Creature &who, map *, Creature * )
{
    if( pending( who ) && g->executions().restoring() ) {
        return true;
    }
    return g && g->executions().settle_fatal( who, "direct_death" );
}

void execution_fatal::before_damage( Creature &who, bool harmful )
{
    if( harmful && pending( who ) && !g->executions().restoring() &&
        !g->executions().owns_fatal_gravity( who ) ) {
        g->executions().settle_fatal( who, "damaged" );
    }
}

bool execution_fatal::before_motion( Creature &who )
{
    if( pending( who ) && !g->executions().restoring() &&
        !g->executions().owns_fatal_motion( who ) ) {
        g->executions().settle_fatal( who, "external_motion" );
        return false;
    }
    return true;
}

bool execution_fatal::before_gravity( Creature &who )
{
    if( pending( who ) && !g->executions().owns_fatal_gravity( who ) ) {
        if( !g->executions().restoring() ) {
            g->executions().settle_fatal( who, "external_physics" );
        }
        return false;
    }
    return true;
}

void execution_fatal::before_removal( const Creature &who, const std::string &reason )
{
    if( g && !g->executions().restoring() ) {
        g->executions().settle_for_removal( who, reason );
    }
}

execution_fatal::death_guard::death_guard( const Creature &who ) : who( who ),
    first( finalizing.insert( &who ).second )
{
}

execution_fatal::death_guard::~death_guard()
{
    if( first ) {
        finalizing.erase( &who );
    }
}

bool execution_fatal::death_guard::entered() const
{
    return first;
}
