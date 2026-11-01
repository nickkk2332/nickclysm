#pragma once
#ifndef CATA_SRC_PLANTING_MEMO_H
#define CATA_SRC_PLANTING_MEMO_H

// While one of these is alive, warm_enough_to_plant (weather.h) samples the weather once
// per overmap tile and seed type and reuses that for every other tile of the same overmap
// tile. The temperatures it checks depend only on the overmap tile, the seed and the time,
// so a scope must not span a change of turn, weather generator or game seed: a farm scan,
// which can ask about every tile of a plot in one turn, is the intended scope.
class scoped_planting_memo
{
    public:
        scoped_planting_memo();
        ~scoped_planting_memo();
        scoped_planting_memo( const scoped_planting_memo & ) = delete;
        scoped_planting_memo &operator=( const scoped_planting_memo & ) = delete;
};

#endif // CATA_SRC_PLANTING_MEMO_H
